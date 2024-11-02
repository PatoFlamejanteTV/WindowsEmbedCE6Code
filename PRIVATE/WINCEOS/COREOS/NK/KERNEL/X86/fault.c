//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//
//
// Use of this source code is subject to the terms of the Microsoft shared
// source or premium shared source license agreement under which you licensed
// this source code. If you did not accept the terms of the license agreement,
// you are not authorized to use this source code. For the terms of the license,
// please see the license agreement between you and Microsoft or, if applicable,
// see the SOURCE.RTF on your install media or the root of your tools installation.
// THE SOURCE CODE IS PROVIDED "AS IS", WITH NO WARRANTIES OR INDEMNITIES.
//

/*+
    fault.c - iX86 fault handlers
 */
#include "kernel.h"

// disable short jump warning.
#pragma warning(disable:4414)

///#define LIGHTS(n)   mov dword ptr ss:[0AA001010h], ~(n)&0xFF

extern BOOL HandleException(PTHREAD pth, int id, ulong addr);
extern void NextThread(void);
extern void KCNextThread(void);
extern KTSS MainTSS;
extern void Reschedule(void);
extern void RunThread(void);
extern void DumpTctx(PTHREAD pth, int id, ulong addr, int level);

extern ULONGLONG   *g_pGDT;

extern DWORD ProcessorFeatures;

FXSAVE_AREA g_InitialFPUState;

#ifdef NKPROF
PTHREAD pthFakeStruct;
#endif

//
//  CR0 bit definitions for numeric coprocessor
//
#define MP_MASK     0x00000002
#define EM_MASK     0x00000004
#define TS_MASK     0x00000008
#define NE_MASK     0x00000020

#define NPX_CW_PRECISION_MASK   0x300
#define NPX_CW_PRECISION_24     0x000
#define NPX_CW_PRECISION_53     0x200
#define NPX_CW_PRECISION_64     0x300


#define VA_TO_PD_IDX(va)        ((DWORD) (va) >> 22)        // (va)/4M == PDE idx
#define PD_IDX_TO_VA(idx)       ((DWORD) (idx) << 22)      // PDE idx * 4M == va base


#define PERFORMCALLBACK     -30    // MUST be -PerformCallback Win32Methods in kwin32.c 
                                    // 30 == -(APISet 0, method 30)

#define THREAD_CTX_ES  (THREAD_CONTEXT_OFFSET+8)
ERRFALSE(8 == offsetof(CPUCONTEXT, TcxEs));
#define THREAD_CTX_EDI  (THREAD_CONTEXT_OFFSET+16)
ERRFALSE(16 == offsetof(CPUCONTEXT, TcxEdi));

#define Naked void __declspec(naked)

#define SANATIZE_SEG_REGS(eax, ax)       \
    _asm { cld } \
    _asm { mov eax, KGDT_R3_DATA }  \
    _asm { mov ds, ax } \
    _asm { mov es, ax } \
    _asm { mov eax, KGDT_PCR }  \
    _asm { mov fs, ax }


// #define ONE_ENTRY

#pragma warning(disable:4035)               // Disable warning about no return value

//
// The Physical to Virtual mapping table is supplied by OEM.
//
extern PADDRMAP g_pOEMAddressTable;

BOOL MDValidateRomChain (ROMChain_t *pROMChain)
{
    PADDRMAP pAddrMap;
    DWORD dwEnd;
    
    for ( ; pROMChain; pROMChain = pROMChain->pNext) {
        for (pAddrMap = g_pOEMAddressTable; pAddrMap->dwSize; pAddrMap ++) {
            dwEnd = pAddrMap->dwVA + pAddrMap->dwSize;
            if (IsInRange (pROMChain->pTOC->physfirst, pAddrMap->dwVA, dwEnd)) {
                if (IsInRange (pROMChain->pTOC->physlast, pAddrMap->dwVA, dwEnd)) {
                    // good XIP, break inner loop and go on to the next region
                    break;
                }
                // bad
                NKDbgPrintfW (L"MDValidateRomChain: XIP (%8.8lx -> %8.8lx) span accross multiple memory region\r\n",
                        pROMChain->pTOC->physfirst, pROMChain->pTOC->physlast);
                return FALSE;
            }
        }
        if (!pAddrMap->dwSize) {
            NKDbgPrintfW (L"MDValidateRomChain: XIP (%8.8lx -> %8.8lx) doesn't exist in OEMAddressTable \r\n",
                        pROMChain->pTOC->physfirst, pROMChain->pTOC->physlast);
            return FALSE;
        }
    }
    return TRUE;
}



//------------------------------------------------------------------------------
// LoadPageTable: handle Page Fault exception
//      addr: address that causes the fault
//      dwErrCode: the error code. (reference: x86 spec)
//                  bit 0 (P): if page mapping is present
//                  bit 1 (R/W): Read or write access 
//                  bit 2 (U/S): user or supervisor mode
//------------------------------------------------------------------------------
#define PFCODE_PRESENT      0x01        // present
#define PFCODE_WRITE        0x02        // trying to write
#define PFCODE_USER_MODE    0x04        // in user mode

PPAGEDIRECTORY GetCurPD (void)
{
    DWORD dwPfn;
    _asm {
        mov eax, cr3
        mov dwPfn, eax
    }
    
    return (PPAGEDIRECTORY) Pfn2Virt (dwPfn);
}

BOOL LoadPageTable (DWORD addr, DWORD dwErrCode)
{
    DWORD idxPD      = VA2PDIDX (addr);
    DWORD dwEntry    = g_ppdirNK->pte[idxPD] & ~PG_ACCESSED_MASK;

    if ((addr >= VM_SHARED_HEAP_BASE) && dwEntry) {

        PPAGEDIRECTORY ppdir = GetCurPD ();

        if (addr < VM_KMODE_BASE) {
            dwEntry = (PFCODE_USER_MODE & dwErrCode)
                    ? ((dwEntry & ~PG_WRITE_MASK) | PG_USER_MASK)   // user mode - r/o
                    : ((dwEntry & ~PG_USER_MASK) | PG_WRITE_MASK);  // kernel mode - r/w
        }

        if ((ppdir->pte[idxPD] & ~PG_ACCESSED_MASK) != dwEntry) {
            ppdir->pte[idxPD] = dwEntry;
            OEMCacheRangeFlush (0, 0, CACHE_SYNC_FLUSH_TLB);
            return TRUE;
        }
    }
    DEBUGMSG (ZONE_VIRTMEM, (L"LoadPageTable failed, addr = %8.8lx, dwErrCode = %8.8lx\r\n", addr, dwErrCode));
    return FALSE;
    
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
PVOID 
Pfn2Virt(
    DWORD pfn
    ) 
{
    PADDRMAP pAddrMap;

    for (pAddrMap = g_pOEMAddressTable; pAddrMap->dwSize; pAddrMap ++) {
        if ((pfn >= pAddrMap->dwPA) && (pfn < pAddrMap->dwPA + pAddrMap->dwSize))
            return (LPVOID) (pfn - pAddrMap->dwPA + pAddrMap->dwVA);
    }

    DEBUGMSG(ZONE_PHYSMEM, (TEXT("Phys2Virt() : PFN (0x%08X) not found!\r\n"), pfn));
    return NULL;
}


#pragma warning(default:4035)               // Turn warning back on


//------------------------------------------------------------------------------
//
// Function:
//  CommonFault
//
// Description:
//  CommonFault is jumped to by the specific fault handlers for unhandled
//  exceptions which are then dispatched to the C routine HandleException.
//
// At entry:
//  ESP     points to stack frame containing PUSHAD, ERROR CODE, EIP, CS,
//          EFLAGS, (and optionally Old ESP, Old SS).  Normally this is the
//          last part of the thread structure, the saved context.  In the case
//          of a nested exception the context has been saved on the ring 0
//          stack.  We will create a fake thread structure on the stack to hold
//          the captured context.  The remaining segment registers are added by
//          this routine.
//
//  ECX     is the faulting address which is passed to HandleException
//
//  ESI     is the exception id which is passed to HandleException
//
//  Return:
//   CommonFault jumps to Reschedule or resumes execution based on the return
//   value of HandleException.
//
//------------------------------------------------------------------------------
Naked 
CommonFault()
{
    SANATIZE_SEG_REGS(eax, ax)

    _asm {
        mov     ebx, dword ptr [g_pKData]   // eax = g_pKData
        dec     [ebx].cNest                 // decrement g_ptrKData->cNest
        jnz     short cf20                  // nested fault
        lea     esp, [ebx-4]
        mov     edi, [ebx].pCurThd
cf10:   sti
        push    ecx
        push    esi
        push    edi
        call    HandleException
        add     esp, 3*4
        test    eax, eax
        jnz     short NoReschedule
        jmp     Reschedule
NoReschedule:
        jmp     RunThread

// Nested exception. Create a fake thread structure on the stack
cf20:   push    ds
        push    es
        push    fs
        push    gs
        sub     esp, THREAD_CONTEXT_OFFSET
        mov     edi, esp            // (edi) = ptr to fake thread struct
        jmp     short cf10
    }
}


//------------------------------------------------------------------------------
//
// Do a reschedule.
//
//  (edi) = ptr to current thread or 0 to force a context reload
//  (ebx) = ptr to KData
//
//------------------------------------------------------------------------------
Naked 
Reschedule()
{
    __asm {
rsd10:
        sti
        cmp     word ptr ([ebx].bResched), 1
        jne     short rsd11
        mov     word ptr ([ebx].bResched), 0
        call    NextThread
rsd11:
        cmp     dword ptr ([ebx].dwKCRes), 1
        jne     short rsd12
        mov     dword ptr ([ebx].dwKCRes), 0
        call    KCNextThread

        cmp     dword ptr ([ebx].dwKCRes), 1
        je      short rsd10

rsd12:
        mov     eax, [RunList.pth]
        test    eax, eax
        jz      short rsd50           // nothing to run
        cmp     eax, edi
        jne     short rsd20
        jmp     RunThread           // redispatch the same thread

// Switch to a new thread's process context.
// Switching to a new thread. Update current process and address space
// information. Edit the ring0 stack pointer in the TSS to point to the
// new thread's register save area.
//
//      (eax) = ptr to thread structure
//      (ebx) = ptr to KData

rsd20:  mov     edi, eax                // Save thread pointer
        mov     esi, (THREAD)[eax].dwId // (esi) = thread id
        push    edi
        call    SetCPUASID              // Sets dwCurProcId for us!
        pop     ecx                     // Clean up stack

        mov     [ebx].ahSys[SH_CURTHREAD*4], esi // set the current thread id
        mov     [ebx].pCurThd, edi               //   and the current thread pointer
        mov     ecx, [edi].tlsPtr       // (ecx) = thread local storage ptr
        mov     [ebx].lpvTls, ecx       // set TLS pointer

        cmp     edi, [ebx].pCurFPUOwner
        jne     SetTSBit
        clts
        jmp     MuckWithFSBase

SetTSBit:
        mov     eax, CR0
        test    eax, TS_MASK
        jnz     MuckWithFSBase
        or      eax, TS_MASK
        mov     CR0, eax

MuckWithFSBase:
        mov     edx, dword ptr [g_pGDT]
        add     edx, KGDT_PCR
        sub     ecx, FS_LIMIT+1         // (ecx) = ptr to NK_PCR base
        mov     word ptr [edx+2], cx    // set low word of FS base
        shr     ecx, 16
        mov     byte ptr [edx+4], cl    // set third byte of FS base
        mov     byte ptr [edx+7], ch    // set high byte of FS base

        push    fs
        pop     fs

        lea     ecx, [edi].ctx.TcxSs+4  // (ecx) = ptr to end of context save area
        mov     [MainTSS].Esp0, ecx
        jmp     RunThread               // Run thread pointed to by edi

// No threads ready to run. Call OEMIdle to shutdown the cpu.
// (ebx) = g_pKData
rsd50:  cli

        cmp     word ptr ([ebx].bResched), 1
        je      short DoReschedule
        call    OEMIdle
        mov     byte ptr ([ebx].bResched), 1
        jmp     Reschedule
DoReschedule:
        sti
        jmp     Reschedule
    }
}



//------------------------------------------------------------------------------
// (esi) = ISR to call, registers all saved to thread structure (or stack, if nested)
//------------------------------------------------------------------------------
Naked 
CommonIntDispatch()
{
    SANATIZE_SEG_REGS(eax, ax)

    _asm {
        mov     ebx, dword ptr [g_pKData]
        dec     [ebx].cNest
        jnz     short cid20         // nested fault
        lea     esp, [ebx-4]
        mov     edi, [ebx].pCurThd
cid10:
#ifdef NKPROF
        //
        // On profiling builds, log the ISR entry event to CeLog
        //
        mov     eax, 80000000h      // mark as ISR entry
        push    eax                 // Arg 0, cNest + SYSINTR_xxx
        call    CELOG_Interrupt
        pop     eax                 // cleanup the stack from the call
#endif // NKPROF

        sti

        call    esi

        push    eax                                 // push argument == SYSINTR returned
        call    OEMNotifyIntrOccurs                 // notify OEM interrupt occurred
        pop     ecx                                 // dummy pop

        cli

#ifdef NKPROF
        //
        // On profiling builds, log the ISR exit event to CeLog
        //
        push    eax                 // Save original SYSINTR return value.
        bswap   eax                 // Reverse endian
        mov     ah, [ebx].cNest     // Nesting level (0 = no nesting, -1 = nested once)
        neg     ah                  // Nesting level (0 = no nesting,  1 = nested once)
        bswap   eax                 // Reverse endian
        push    eax                 // Arg 0, cNest + SYSINTR_xxx
        call    CELOG_Interrupt
        pop     eax                 // cleanup the stack from the call
        pop     eax                 // restore original SYSINTR value
#endif // NKPROF

        test    eax, eax
        jz      short RunThread     // SYSINTR_NOP: nothing more to do

#ifdef NKPROF
        cmp     eax, SYSINTR_PROFILE
        jne     short cid13
        call    ProfilerHit
        jmp     RunThread           // Continue on our merry way...
cid13:
#endif // NKPROF

        cmp     eax, SYSINTR_RESCHED
        je      short cid15
        lea     ecx, [eax-SYSINTR_DEVICES]
        cmp     ecx, SYSINTR_MAX_DEVICES
        jae     short cid15         // force a reschedule for good measure

// A device interrupt has been signaled. Set the appropriate bit in the pending
// events mask and set the reschedule flag. The device event will be signaled
// by the scheduler.
//  (ebx) = g_pKData
//  (ecx) = SYSINTR

        mov     eax, 1              // (eax) = 1
        cmp     ecx, 32             // ISR# >= 32?
        jae     cid18               // take care of ISR# >= 32 if true

        // ISR# < 32
        shl     eax, cl             // (eax) = 1 << ISR#
        or      [ebx].aPend1, eax   // update PendEvent1
cid15:  or      [ebx].bResched, 1   // must reschedule
        jmp     RunThread

        // ISR# >= 32
cid18:  sub     cl, 32              // ISR# -= 32
        shl     eax, cl             // (eax) = 1 << (ISR#-32)
        or      [ebx].aPend2, eax   // update PendEvent2
        or      [ebx].bResched, 1   // must reschedule
        jmp     RunThread

// Nested exception. Create a fake thread structure on the stack
cid20:  push    ds
        push    es
        push    fs
        push    gs
        sub     esp, THREAD_CONTEXT_OFFSET
        mov     edi, esp            // (edi) = ptr to fake thread struct
#ifdef NKPROF
        mov     dword ptr (pthFakeStruct), edi
#endif        
        jmp     short cid10
    }
}




//------------------------------------------------------------------------------
//
// Continue thread execution.
//
//  (edi) = ptr to Thread structure
//  (ebx) = ptr to KData
//
//------------------------------------------------------------------------------
Naked 
RunThread()
{
    _asm {
        cli
        cmp     word ptr ([ebx].bResched), 1
        jne short NotReschedule
        jmp     Reschedule
NotReschedule:
        inc     [ebx].cNest
        lea     esp, [edi].ctx.TcxGs
        pop     gs
        pop     fs
        pop     es
        pop     ds
        popad
        add     esp, 4
        iretd
        cli
        hlt
    }
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
Naked 
PageFault()
{
    _asm {
        pushad

        SANATIZE_SEG_REGS(eax, ax)

        mov     ebx, dword ptr [g_pKData]
        mov     edi, cr2
        test    edi, edi
        jns     short pf05 

        // Address > 2GB, kmode only
        mov     esi, [esp+32]
        and     esi, 1
        jnz     short pf50              // prevelige vialoation, get out now
        
pf05:
        dec     [ebx].cNest             // count kernel reentrancy level
        mov     esi, esp                // (esi) = original stack pointer
        jnz     short pf10
        lea     esp, [ebx-4]            // switch to kernel stack (&KData-4)

//  Process a page fault for the "user" address space (0 to 0x7FFFFFFF)
//
//  (edi) = Faulting address
//  (ebx) = ptr to KData
//  (esi) = Original ESP

pf10:   cmp     dword ptr ([ebx].dwInDebugger), 0   // see if debugger active
        jne     short pf20                          // if so, skip turning on of interrupts
        sti                                         // enable interrupts

pf20:   push    [esi+32]
        push    edi
        call    LoadPageTable
        cli
        test    eax, eax
        jz      short pf40          // page not found in the Virtual memory tree
        cmp     word ptr ([ebx].bResched), 1
        je      short pf60          // must reschedule now
        inc     [ebx].cNest         // back out of kernel one level
        mov     esp, esi            // restore stack pointer
        popad
        add     esp, 4
        iretd

        
//  This one was not a good one!  Jump to common fault handler
//
//  (edi) = faulting address

pf40:   inc     [ebx].cNest         // back out of kernel one level
        mov     esp, esi            // restore stack pointer
pf50:   mov     ecx, edi            // (ecx) = fault effective address
        mov     esi, 0Eh
        jmp     CommonFault

// The reschedule flag was set and we are at the first nest level into the kernel
// so we must reschedule now.

pf60:   mov     edi, [ebx].pCurThd  // (edi) = ptr to current THREAD
        jmp     Reschedule
    }
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
Naked 
GeneralFault()
{
    _asm {
        pushad
        xor     ecx, ecx            // (ecx) = 0 (fault effective address)
        mov     esi, 13
        jmp     CommonFault
    }
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
Naked 
InvalidOpcode(void)
{
    __asm {
        push    eax
        pushad
        mov     esi, 6
        xor     ecx, ecx            // (ecx) = 0 (fault effective address)
        jmp     CommonFault
    }
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
Naked 
ZeroDivide(void)
{
    __asm {
        push    eax
        pushad
        xor     esi, esi            // (esi) = 0 (divide by zero fault)
        xor     ecx, ecx            // (ecx) = 0 (fault effective address)
        jmp     CommonFault
    }
}

const BYTE PosTable[256] = {
    0,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,5,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,
    6,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,5,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,
    7,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,5,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,
    6,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,5,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,
    8,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,5,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,
    6,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,5,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,
    7,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,5,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,
    6,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1,5,1,2,1,3,1,2,1,4,1,2,1,3,1,2,1
};




//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void __declspec(naked) 
GetHighPos(
    DWORD foo
    ) 
{
    _asm {
        mov ecx, dword ptr [esp + 4]
        push ebx
        lea ebx, PosTable

        mov dl, 0xff
        xor eax, eax
        mov al, cl
        xlatb
        test al, al
        jne res

        shr ecx, 8
        add dl, 8
        mov al, cl
        xlatb
        test al, al
        jne res

        shr ecx, 8
        add dl, 8
        mov al, cl
        xlatb
        test al, al
        jne res

        shr ecx, 8
        add dl, 8
        mov al, cl
        xlatb
        test al, al
        jne res

        mov al, 9
res:
        add al, dl
        pop ebx
        ret
    }
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
Naked 
Int1Fault(void)
{
    __asm {
        push    eax                             // Save orig EAX as fake error code

        cmp     word ptr [esp + 6], 0FFFFh      // Is it an API call fault?
        je      skip_debug                      // Yes - handle page fault first
        mov     eax, [esp + 4]                  // (eax) = faulting EIP
        and     dword ptr [esp + 12], not 0100h  // Clear TF if set
        test    byte ptr [esp + 8], 3           // Are we trying to SS ring 0?
        jz      skip_debug                      // Yes - get out quick
        mov     eax, dword ptr [esp]            // Restore original EAX

        pushad
        mov     esi, 1
        xor     ecx, ecx            // (ecx) = 0 (fault effective address)
        jmp     CommonFault

skip_debug:
        pop     eax
        iretd
    }
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
Naked 
Int2Fault(void)
{
    __asm {
        push    eax                 // Fake error code
        pushad
        mov     esi, 2
        xor     ecx, ecx            // (ecx) = 0 (fault effective address)
        jmp     CommonFault
    }
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
Naked 
Int3Fault(void)
{
    __asm {

        dec     dword ptr [esp]     // Back up EIP
        push    eax                 // Fake error code

        pushad
        mov     esi, 3
        xor     ecx, ecx            // (ecx) = 0 (fault effective address)
        jmp     CommonFault
    }
}


#pragma warning(disable:4035 4733)
//------------------------------------------------------------------------------
//
// Function: 
//      void UpdateRegistrationPtr (NK_PCR *pcr);
//
// Description:
//      update registration pointer on thread switch
//
Naked UpdateRegistrationPtr (NK_PCR *pcr)
{
    _asm {
        mov     ecx, [esp+4]                    // (ecx) = pcr
        mov     edx, dword ptr [g_pGDT]
        add     edx, KGDT_PCR                   // (edx) = FS entry in global descriptor table
        mov     word ptr [edx+2], cx            // set low word of FS base
        shr     ecx, 16
        mov     byte ptr [edx+4], cl            // set third byte of FS base
        mov     byte ptr [edx+7], ch            // set high byte of FS base
        push    fs
        pop     fs                              // cause fs to reload

        ret
    }
}

//------------------------------------------------------------------------------
//
// Function: 
//      void UpdateRegistrationPtrWithTLS 
//
// Description:
//      update registration pointer on stack switch
//      AT ENTRANCE (ecx) = TLSPTR
//      NOTE: ONLY USE ECX and EDX, FOR THIS IS CALLED DIRECT FROM ASSEMBLY
//
Naked UpdateRegistrationPtrWithTLS (LPDWORD tls)
{
    _asm {
        mov     edx, dword ptr [g_pGDT]
        add     edx, KGDT_PCR                   // (edx) = FS entry in global descriptor table
        sub     ecx, FS_LIMIT+1
        mov     word ptr [edx+2], cx            // set low word of FS base
        shr     ecx, 16
        mov     byte ptr [edx+4], cl            // set third byte of FS base
        mov     byte ptr [edx+7], ch            // set high byte of FS base
        push    fs
        pop     fs                              // cause fs to reload

        ret
    }
}

//------------------------------------------------------------------------------
//
// Function: 
//      void CallUModeFunction 
//
// Description:
//      call user mode function
//      AT ENTRANCE (edi) = target SP, (eax) = function to call, retun
//                          address must have been setup correctly on
//                          target stack (ususally SYSCALL_RETURN).
//
//      NOTE: THIS IS CALLED DIRECT FROM ASSEMBLY, edi must have been 
//            saved correctly per calling convention.
//            USE ESI
//
Naked CallUModeFunction (void)
{
    _asm {
        // (edi) = newSP (SYSCALL_RETURN already pushed onto target stack)
        // (eax) = function to call

        // switch TLS to nonsecure stack
        mov     esi, [g_pKData]
        mov     edx, [esi].pCurThd              // (edx) = pCurThread
        mov     ecx, [edx].tlsNonSecure         // (ecx) = pCurThread->tlsNonSecure
        mov     [edx].tlsPtr, ecx               // pCurThread->tlsPtr = ecx              
        mov     [esi].lpvTls, ecx               // update global tls ptr
        
        // update fs, (ecx) = tlsptr
        call    UpdateRegistrationPtrWithTLS    // UpdateRegistrationPtrWithTLS uses only ecx and edx
        
        // setup far return stack
        push    KGDT_R3_DATA | 3                // SS of ring 3
        push    edi                             // target ESP
        push    KGDT_R3_CODE | 3                // CS of ring 3
        push    eax                             // function to call
        
        // return to user code
        retf
    }
}


//------------------------------------------------------------------------------
//
// Function: 
//      DWORD NKPerformCallback (PCALLBACKINFO pcbi, ...);
//
// Description:
//      dirct call to perform callback to user code
//
DWORD __declspec(naked) NKPerformCallBack (PCALLBACKINFO pcbi, ...)
{
    _asm {
        mov     ecx, esp                            // save esp
        sub     esp, size CALLSTACK                 // reserve space for callstack (pNewcstk)
        
        // setup argument to PerfomCallback
        mov     eax, fs:[0]                         // (eax) = registration pointer
        mov     [esp].dwPrevSP, ecx                 // pcstk->dwPrevSP == SP at the point of call
        mov     [esp].regs[REG_OFST_EXCPLIST], eax  // save registration pointer on callstack
        
        push    esp                                 // arg - pNewcstk

        call    NKPrepareCallback

        pop     ecx                                 // pop argument

        // (eax) = function to call
        // NOTE: pcstk->dwNewSP has the target SP, 0 if kernel mode call (call direct, no callstack setup)
        cmp     [esp].dwNewSP, 0
        je      short PCBCallDirect

        // callback to user code

        // save all Callee Saved Registers
        mov     [esp].regs[REG_OFST_ESP], esp        // .\    v
        mov     [esp].regs[REG_OFST_EBP], ebp        // ..\    v
        mov     [esp].regs[REG_OFST_EBX], ebx        // ...> save callee saved registers
        mov     [esp].regs[REG_OFST_ESI], esi        // ../
        mov     [esp].regs[REG_OFST_EDI], edi        // ./

        mov     edi, [esp].dwNewSP                   // (edi) = target SP

        // (edi) = target SP, (eax) = function to call
        jmp     short CallUModeFunction

    PCBCallDirect:
        // direct call, no callstack setup
        add     esp, size CALLSTACK                 // restore SP
        jmp     eax                                 // just jump to EAX, for arg/retaddr are all setup correctly
        
    }
}

//------------------------------------------------------------------------------
//
// System call trap handler.
//
// Pop the iret frame from the stack, switch back to the caller's stack, enable interrupts,
// and dispatch the system call.
//
//      CPU State:  ring1 stack & CS, interrupts disabled.
//                  eax == rtn value if PSL return
//
//
//      top of Stack when called from user-mode:
//                  EIP         (the API call we're trying to make)
//                  CS          KGDT_R3_CODE
//                  EFLAGS      
//                  ESP         (caller's ESP)
//                  SS          KGDT_R3_DATA
//
//      top of stack when called from k-mode
//                  EIP         (the API call we're trying to make)
//                  CS          KGDT_R1_CODE
//                  EFLAGS      
//
//
//------------------------------------------------------------------------------
PFNVOID __declspec(naked) Int20SyscallHandler (void)
{
    __asm {
        // The following three instructions are only executed once on Init time.
        // It sets up the KData PSL return function pointer and returns the
        // the 'real' address of the Int20 handler (sc00 in this case).
        mov eax, dword ptr [g_pKData]
        mov [eax].pAPIReturn, offset APICallReturn
        mov eax, offset sc00
        ret


sc00:   
        SANATIZE_SEG_REGS(ecx, cx)

        pop     ecx                     // (ecx) = EIP of "int SYSCALL"
        sub     ecx, FIRST_METHOD+2     // (ecx) = iMethod * APICALL_SCALE
        cmp     ecx, -APICALL_SCALE     // check callback return
        jne     short NotCbRtn           

        // possibly api/callback return, check to make sure we're in callback
        mov     ecx, dword ptr [g_pKData]
        mov     ecx, [ecx].pCurThd      // (ecx) = pCurThread
        mov     ecx, [ecx].pcstkTop     // (ecx) = pCurThread->pcstkTop
        test    ecx, ecx                // pCurThread->pcstkTop?
        jnz     short trapReturn

        // pcstkTop is NULL, not in callback/APIcall
        // we let it fall though here for 0 is
        // an invalid API and we'll raise an exception as a result.

NotCbRtn:
        // (ecx) = iMethod << 1
        sar     ecx, 1                  // (ecx) == iMethod
        pop     eax                     // (eax) == caller's CS
        and     al, 0FCh
        cmp     al, KGDT_R1_CODE
        je      short KPSLCall          // caller was in kernel mode

        // caller was in user mode - switch stack
        mov     eax, dword ptr [g_pKData]
        mov     eax, [eax].pCurThd      // (ecx) = pCurThread
        mov     edx, [eax].pcstkTop     // (edx) = pCurThread->pcstkTop
        test    edx, edx                // first trip into kernel?
        jne     short UPSLCallCmn

        // 1st trip into kernel
        mov     edx, [eax].tlsSecure    // (edx) = pCurThread->tlsSecure
        sub     edx, SECURESTK_RESERVE  // (edx) = sp to use
        
UPSLCallCmn:

        // update TlsPtr and ESP
        //      (eax) = pCurThread
        //      (edx) = new SP
        //      (ecx) = iMethod
        //
        // NOTE: must update ESP before we start writing to the secure stack
        //       or we'll fail to DemandCommit stack pages

        // save ecx for we need a register to work with
        push    ecx

        // update TLSPTR
        mov     ecx, [eax].tlsSecure    // (ecx) = pCurThread->tlsSecure
        mov     [eax].tlsPtr, ecx       // pCurThread->tlsPtr = pCurThread->tlsSecure
        mov     eax, dword ptr [g_pKData]
        mov     [eax].lpvTls, ecx       // set KData's TLS pointer

        // restore ecx
        pop     ecx


        // switch stack
        mov     esp, [esp+4]            // retrieve SP
        xchg    edx, esp                // switch stack, (edx) = old stack pointer
        mov     eax, CST_MODE_FROM_USER // we're calling from user mode

PSLCommon:
        //
        // (eax) - mode
        // (ecx) - iMethod
        // (edx) - prevSP
        // (esp) - new SP
        //

        // enable interrupts
        sti

        //
        //  we're free to use stack from this point
        // 

        // save info in callstack

        sub     esp, size CALLSTACK             // reserve space for callstack, (esp) = pcstk

        // save esi/edi on callstack
        mov     [esp].regs[REG_OFST_ESI], esi   // 
        mov     [esp].regs[REG_OFST_EDI], edi   //
        mov     [esp].iMethod, ecx              // pcstk->iMethod = iMethod

        mov     esi, esp                        // (esi) == pcstk
        
        mov     [esi].dwPrevSP, edx             // pcstk->dwPrevSP = old-SP
//        mov     [esi].retAddr, 0                // retaddr to be filled in ObjectCall, for it's accessing user stack
        mov     [esi].dwPrcInfo, eax            // pcstk->dwPrcInfo == current mode

        // don't touch fs:[0] here, for it's point to user tls. do it in Object call, after validating stack.
        //mov     edx, fs:dword ptr [0]         // exception information
        //mov     [esi].extra, edx              // pcstk->extra = fs:[0]

        // save the rest of registers required for exception recovery
        mov     [esi].regs[REG_OFST_EBP], ebp   // 
        mov     [esi].regs[REG_OFST_EBX], ebx   // 
        mov     [esi].regs[REG_OFST_ESP], esp   // save esp for SEH handling

        mov     ebx, dword ptr [g_pKData]       // (ebx) = ptr to KData

        test    eax, eax                        // are we called from kernel mode?
        jz      short DoObjectCall      

        // called from user mode, stack changed, update fs
        mov     ecx, [ebx].pCurThd
        mov     ecx, [ecx].tlsPtr               // (ecx) = arg to UpdateRegistrationPtrWithTLS
        call    UpdateRegistrationPtrWithTLS    // update FS

        mov     fs:dword ptr [0], REGISTRATION_RECORD_PSL_BOUNDARY            // mark PSL boundary in exception chain (on secure stack)

DoObjectCall:
        // (esi) = pcstk
        push    esi                             // arg0 == pcstk
        
        call    ObjectCall                      // (eax) = api function address

        pop     ecx                             // get rid of arg0
        
        mov     edi, [esi].dwNewSP              // (edi) == new SP if user-mode server

        test    edi, edi

        // eax = function to call, edi = target SP if non-zero
        jnz     short CallUModeFunction

        call    eax                             // & call the api function in KMODE
APICallReturn:

        //  (edx:eax) = function return value
        //  (ebx) = ptr to KData

        // save return value in esi:edi (orginal esi/edi are saved in pcstk)
        mov     edi, edx                        // (edi) = high part of return value
        mov     esi, eax                        // (esi) = low part of return value

        call    ServerCallReturn        
        // return address at pcstk->retAddr
    
        // restore eax (edx will be restored later for we still need a register to use)
        mov     eax, esi

        mov     esi, esp                        // (esi) = pcstk

        cmp     [esi].dwPrcInfo, 0              // which mode to return to (0 == kernel-mode)

        jne     short UPSLRtn           

        // returning to KMode caller, just restore registers and return
        mov     edx, edi                        // restore EDX (high part of return value)
        mov     esp, [esi].dwPrevSP             // restore SP
        mov     ebx, [esi].regs[REG_OFST_EBX]   // restore EBX
        mov     edi, [esi].regs[REG_OFST_EDI]   // restore EDI
        mov     esi, [esi].regs[REG_OFST_ESI]   // restore ESI
        ret

UPSLRtn:        
        // returning to user mode process
        // (ebx) = ptr to KData
        // (esi) = pcstk

        // update fs, (ecx) = tlsptr
        mov     ecx, [esi].pOldTls              // (ecx) = pcstk->pOldTls
        call    UpdateRegistrationPtrWithTLS    // UpdateRegistrationPtrWithTLS uses only ecx and edx

        // restore edx, edi, ebx
        mov     edx, edi                        // restore EDX
        mov     edi, [esi].regs[REG_OFST_EDI]   // restore EDI
        mov     ebx, [esi].regs[REG_OFST_EBX]   // restore ebx

        // setup far return stack
        mov     ecx, [esi].dwPrevSP             // (ecx) = previous SP
        add     ecx, 4                          // (ecx) = previous SP, without return address
        push    KGDT_R3_DATA | 3                // SS of ring 3
        push    ecx                             // target ESP
        push    KGDT_R3_CODE | 3                // CS of ring 3
        push    [esi].retAddr                   // return address

        mov     esi, [esi].regs[REG_OFST_ESI]   // restore ESI

        // return to user code
        retf

KPSLCall:
        // caller was in kernel mode
        add     esp, 4                  // discard the EFLAGS
        
        xor     eax, eax                // we're in kernel mode

        // is this a callback?
        cmp     ecx, PERFORMCALLBACK
        mov     edx, esp                // prevSP set to the same as ESP

        // jump to PSL common code if not a callback
        jne     short PSLCommon

        // callback - enable interrupt and jump to NKPerformCallback
        sti
        jmp     short NKPerformCallBack


////////////////////////////////////////////////////////////////////////////////////////
// api/callback returns
//      (edx:eax) = return value
//      (ecx) = pCurThread->pcstkTop
//
trapReturn:
        // callback return from user mode
        mov     esp, ecx                        // (esp) = pCurThread->pcstkTop
        sti                                     // interrupt okay now

        // switch to secure stack    
        mov     ebx, dword ptr [g_pKData]       // (ebx) = g_pKData
        mov     esi, [ebx].pCurThd              // (esi) = pCurThread
        mov     edi, edx                        // save edx in edi
        mov     ecx, [esi].tlsSecure            // (ecx) = pCurThread->tlsSecure
        mov     [esi].tlsPtr, ecx               // pCurThread->tlsPtr = pCurThread->tlsSecure
        mov     [ebx].lpvTls, ecx               // set KData's TLS pointer

        // update fs to pointer to secure stack
        call    UpdateRegistrationPtrWithTLS    // assembly function, only use ecx and edx

        // restore ebp, edx the rest will be restored later
        mov     edx, edi
        mov     ebp, [esp].regs[REG_OFST_EBP]

        // jump to common return handler
        jmp     short APICallReturn

    }
}

DWORD  __declspec (naked) MDCallUserHAPI (PHDATA phd, PDHCALL_STRUCT phc)
{
    _asm {
        mov  edx, esp               // edx = old sp
        sub  esp, size CALLSTACK    // esp = room for pcstk

        // save callee saved registers and exception chain
        mov  eax, fs:[0]
        mov  [esp].regs[REG_OFST_EBP], ebp
        mov  [esp].regs[REG_OFST_EBX], ebx
        mov  [esp].regs[REG_OFST_ESI], esi
        mov  [esp].regs[REG_OFST_EDI], edi
        mov  [esp].regs[REG_OFST_ESP], esp
        mov  [esp].regs[REG_OFST_EXCPLIST], eax

        mov  esi, esp               // (esi) = pcstk
        mov  ecx, [edx]             // ecx = return address
        mov  [esi].dwPrevSP, edx    // pcstk->dwPrevSP = original SP
        mov  [esi].retAddr, ecx     // pcstk->retAddr  = return address

        push esi                    // arg3 to SetupCallToUserServer == pcstk
        push dword ptr [edx+8]      // arg2 == phc
        push dword ptr [edx+4]      // arg1 == phd
        call SetupCallToUserServer

        
        // eax == function to call upon return
        mov  edi, [esi].dwNewSP     // (edi) = target SP

        test edi, edi
        
        // edi = target SP, eax = function to call
        jne  short CallUModeFunction

        // k-mode function, call directy (error, in this case)
        mov  esp, esi               // (esp) = pcstk
        call eax

        // jump to direct return
        // (edx):(eax) = return value
        mov  ebx, dword ptr [g_pKData]
        mov  ecx, [ebx].pAPIReturn
        jmp  ecx
    }
}

DWORD __declspec (naked) MDCallKernelHAPI (FARPROC pfnAPI, DWORD cArgs, LPVOID phObj, REGTYPE *pArgs)
{
    _asm {
        push ebp
        mov  ebp, esp

        mov  ecx, cArgs      // (ecx) = cArgs
        test ecx, ecx
        jz   short docall

        cld
        mov  eax, esi        // save esi in eax
        mov  edx, ecx        // (edx) = # of arguments
        mov  esi, pArgs      // (esi) = pArgs = source of movsd
        shl  edx, 2          // (edx) = total size of variable args
        sub  esp, edx        // (esp) = room for arguments on stack
        mov  edx, edi        // save edi in edx
        mov  edi, esp        // (edi) = destination of movsd

        rep  movsd           // copy the arguments

        // restore esi, edi
        mov  edi, edx
        mov  esi, eax

    docall:
        push phObj           // push the handle object
        call pfnAPI

        // eax already the return value.
        mov  esp, ebp
        pop  ebp
        ret
           
    }
}

// jump to pfn, with arguments pointed by pArgs
Naked MDSwitchToUserCode (FARPROC pfn, REGTYPE *pArgs)
{
    _asm {
        mov     eax, [esp+4]            // (eax) = pfn
        mov     edi, [esp+8]            // (edi) = pArgs == target SP
        jmp     short CallUModeFunction // jump to common code to call UMode function
    }
}


//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
int __declspec(naked) 
KCall(
    PKFN pfn, 
    ...
    )
{
    __asm {
        push    ebp
        mov     ebp, esp
        push    edi
        mov     eax, 12[ebp]    // (eax) = arg0
        mov     edx, 16[ebp]    // (edx) = arg1
        mov     ecx, 20[ebp]    // (ecx) = arg2
        mov     edi, dword ptr [g_pKData]
        cmp     [edi].cNest, 1  // nested?
        mov     edi, 8[ebp]     // (edi) = function address
        jne     short kcl50     // Already in non-preemtible state
        int KCALL_INT           // trap to ring0 for non-preemtible stuff
        pop     edi
        pop     ebp             // restore original EBP
        ret

kcl50:  push    ecx             // push Arg2
        push    edx             // push Arg1
        push    eax             // push Arg0
        call    edi             // invoke function
        add     esp, 3*4        // remove args from stack
        pop     edi
        pop     ebp             // restore original EBP
        ret
    }
}
#pragma warning(default:4035 4733)         // Turn warning back on




//------------------------------------------------------------------------------
// (edi) = funciton to call, eax, edx, and ecx are arguments to KCall
// NOTE: int22 is a Ring1 trap gate (i.e. no user mode access) thus
//       no sanitize registers here
//------------------------------------------------------------------------------
Naked 
Int22KCallHandler(void)
{
    __asm {
        push    eax                 // fake Error Code
        pushad
        mov     esi, esp            // save ptr to register save area
        mov     ebx, dword ptr [g_pKData]
        lea     esp, [ebx-4]        // switch to kernel stack
        dec     [ebx].cNest
        sti
        push    ecx                 // push Arg2
        push    edx                 // push Arg1
        push    eax                 // push Arg0
        call    edi                 // invoke non-preemtible function
        mov     [esi+7*4], eax      // save return value into PUSHAD frame
        xor     edi, edi            // force Reschedule to reload the thread's state
        cli

        cmp     word ptr ([ebx].bResched), 1
        jne     short NotResched1
        jmp     Reschedule
NotResched1:
        cmp     dword ptr ([ebx].dwKCRes), 1
        jne     short NotResched2
        jmp     Reschedule
NotResched2:
        mov     esp, esi
        inc     [ebx].cNest
        popad
        add     esp, 4              // throw away the error code
        iretd
    }
}


//------------------------------------------------------------------------------
// rdmsr - C wrapper for the assembly call. Must be in RING 0 to make this call!
// Assumes that the rdmsr instruction is supported on this CPU; you must check
// using the CPUID instruction before calling this function.
//------------------------------------------------------------------------------
static void rdmsr(
    DWORD dwAddr,       // Address of MSR being read
    DWORD *lpdwValHigh, // Receives upper 32 bits of value, can be NULL
    DWORD *lpdwValLow   // Receives lower 32 bits of value, can be NULL
    )
{
    DWORD dwValHigh, dwValLow;

    _asm {
        ;// RDMSR: address read from ECX, data returned in EDX:EAX
        mov     ecx, dwAddr
        rdmsr
        mov     dwValHigh, edx
        mov     dwValLow, eax
    }

    if (lpdwValHigh) {
        *lpdwValHigh = dwValHigh;
    }
    if (lpdwValLow) {
        *lpdwValLow = dwValLow;
    }
}


//------------------------------------------------------------------------------
// NKrdmsr - C wrapper for the rdmsr assembly call.  Handles getting into
// ring 0 but does not check whether MSRs are supported or whether the
// particular MSR address being read from is supported.
//------------------------------------------------------------------------------
BOOL
NKrdmsr(
    DWORD dwAddr,       // Address of MSR being read
    DWORD *lpdwValHigh, // Receives upper 32 bits of value, can be NULL
    DWORD *lpdwValLow   // Receives lower 32 bits of value, can be NULL
    )
{
    if (InSysCall()) {
        rdmsr(dwAddr, lpdwValHigh, lpdwValLow);
    } else {
        KCall((PKFN)rdmsr, dwAddr, lpdwValHigh, lpdwValLow);
    }

    return TRUE;
}


//------------------------------------------------------------------------------
// wrmsr - C wrapper for the assembly call. Must be in RING 0 to make this call!
// Assumes that the wrmsr instruction is supported on this CPU; you must check
// using the CPUID instruction before calling this function.
//------------------------------------------------------------------------------
static void wrmsr(
    DWORD dwAddr,       // Address of MSR being written
    DWORD dwValHigh,    // Upper 32 bits of value being written
    DWORD dwValLow      // Lower 32 bits of value being written
    )
{
    _asm {
        ;// WRMSR: address read from ECX, data read from EDX:EAX
        mov     ecx, dwAddr
        mov     edx, dwValHigh
        mov     eax, dwValLow
        wrmsr
    }
}


//------------------------------------------------------------------------------
// NKwrmsr - C wrapper for the wrmsr assembly call.  Handles getting into
// ring 0 but does not check whether MSRs are supported or whether the
// particular MSR address being written to is supported.
//------------------------------------------------------------------------------
BOOL
NKwrmsr(
    DWORD dwAddr,       // Address of MSR being written
    DWORD dwValHigh,    // Upper 32 bits of value being written
    DWORD dwValLow      // Lower 32 bits of value being written
    )
{
    if (InSysCall()) {
        wrmsr(dwAddr, dwValHigh, dwValLow);
    } else {
        KCall((PKFN)wrmsr, dwAddr, dwValHigh, dwValLow);
    }

    return TRUE;
}


///////////////////////////// FLOATING POINT UNIT CODE /////////////////////////////////



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void 
InitializeEmx87(void) 
{
    // Fast FP save/restore instructions are not available when emulating FP
    KCALLPROFON(70);
    ProcessorFeatures &= ~CPUID_FXSR;
    _asm {
        mov eax, cr0
        or  eax, MP_MASK or EM_MASK
        and eax, NOT (TS_MASK or NE_MASK)
        mov cr0, eax
    }
    KCALLPROFOFF(70);
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void 
InitNPXHPHandler(
    LPVOID NPXNPHandler
    ) 
{
    KCALLPROFON(71);
    if (IsKModeAddr ((DWORD)NPXNPHandler)) {
        InitIDTEntry(0x07, KGDT_R1_CODE, NPXNPHandler, RING1_TRAP_GATE);
    } else {
        InitIDTEntry(0x07, KGDT_R3_CODE, NPXNPHandler, RING3_TRAP_GATE);
    }
    KCALLPROFOFF(71);
}

BOOL __declspec(naked) NKIsSysIntrValid (DWORD idInt)
{
    _asm {
        mov     eax, [esp+4]            // (eax) = idInt
        sub     eax, SYSINTR_DEVICES    // (eax) = idInt - SYSINTR_DEVICES
        jb      NKIRetFalse             // return FALSE if < SYSINTR_DEVICES

        cmp     eax, SYSINTR_MAX_DEVICES // (eax) >= SYSINTR_MAX_DEVICES?
        jae     NKIRetFalse             // return FALSE if >= SYSINTR_MAX_DEVICES

        //; idInt is valid, return IntrEvents[idInt-SYSINTR_DEVICES]
        mov     ecx, dword ptr [g_pKData]
        mov     eax, dword ptr [ecx].alpeIntrEvents[eax*4]

        ret

    NKIRetFalse:
        xor     eax, eax                // return FALSE
        ret
    }
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
Naked 
FPUNotPresentException(void)
{
    _asm {
        push    eax                 // Fake error code
        
        // We cannot be emulating FP if we arrive here. It is safe to not check
        // if CR0.EM is set.
        pushad
        clts

        mov     ebx, dword ptr [g_pKData]
        dec     [ebx].cNest         // count kernel reentrancy level
        mov     esi, esp            // (esi) = original stack pointer
        jnz     short fpu10
        lea     esp, [ebx-4]        // switch to kernel stack (&KData-4)
 fpu10:
        sti

        mov     eax, [ebx].pCurFPUOwner
        test    eax, eax
        jz      NoCurOwner
        mov     eax, [eax].tlsSecure
        sub     eax, FLTSAVE_BACKOFF
        and     eax, 0xfffffff0             // and al, f0 causes processor stall
        test    ProcessorFeatures, CPUID_FXSR
        jz      fpu_fnsave
        FXSAVE_EAX
        jmp     NoCurOwner
fpu_fnsave:
        fnsave  [eax]
NoCurOwner:
        mov     eax, [ebx].pCurThd
        mov     [ebx].pCurFPUOwner, eax
        mov     eax, [eax].tlsSecure
        sub     eax, FLTSAVE_BACKOFF
        and     eax, 0xfffffff0             // and al, f0 causes processor stall
        test    ProcessorFeatures, CPUID_FXSR
        jz      fpu_frestor
        FXRESTOR_EAX
        jmp     fpu_done
fpu_frestor:
        frstor  [eax]
fpu_done:
        cli
        
        cmp     word ptr ([ebx].bResched), 1
        je      short fpu_resched           // must reschedule now
        inc     [ebx].cNest                 // back out of kernel one level
        mov     esp, esi                    // restore stack pointer
        popad
        add     esp, 4                      // skip fake error code
        iretd
    
        // The reschedule flag was set and we are at the first nest level into the kernel
        // so we must reschedule now.

fpu_resched:
        mov     edi, [ebx].pCurThd      // (edi) = ptr to current THREAD
        jmp     Reschedule
    }
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void 
FPUFlushContext(void) 
{
    FLOATING_SAVE_AREA *pFSave;
    if (g_CurFPUOwner) {
        _asm {
            // If we are emulating FP, g_CurFPUOwner is always 0 so we don't
            // have to test if CR0.EM is set(i.e. fnsave will not GP fault).
            clts
        }
        if (g_CurFPUOwner->pSavedCtx) {
            pFSave = &g_CurFPUOwner->pSavedCtx->FloatSave;
            // BUGBUG: Save the FP state in NK_PCR also ??
            _asm {
                mov eax, pFSave
                fnsave [eax]
            }
        }  else  {
            pFSave = PTH_TO_FLTSAVEAREAPTR(g_CurFPUOwner);
            _asm  {
                mov     eax, pFSave
                test    ProcessorFeatures, CPUID_FXSR
                jz      flush_fsave
                FXSAVE_EAX
                jmp     flush_done
            flush_fsave:
                fnsave   [eax]
                fwait
            flush_done:
            }
        }
        _asm  {
            mov     eax, CR0        // fnsave destroys FP state &
            or      eax, TS_MASK    // g_CurFPUOwner is 0 so we must force
            mov     CR0, eax        // trap 7 on next FP instruction
        }
        g_CurFPUOwner = 0;
    }
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
Naked 
FPUException(void)
{
    _asm {
        push    eax                 // Fake error code
        pushad
        xor     ecx, ecx            // EA = 0
        mov     esi, 16
        jmp     CommonFault
    }
}


//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void 
InitializeFPU(void)
{
    KCALLPROFON(69);    
    InitIDTEntry(0x07, KGDT_R0_CODE, FPUNotPresentException, INTERRUPT_GATE);
    InitIDTEntry(0x10, KGDT_R0_CODE, FPUException, INTERRUPT_GATE);

    _asm {
        mov     eax, cr0
        or      eax, MP_MASK OR NE_MASK
        and     eax, NOT (TS_MASK OR EM_MASK)
        mov     cr0, eax

        finit

        fwait
        mov     ecx, offset g_InitialFPUState
        add     ecx, 10h                    // Force 16 byte alignment else
        and     cl, 0f0h                    // fxsave will fault
        test    ProcessorFeatures, CPUID_FXSR
        jz      no_fxsr

        MOV_EDX_CR4
        or      edx, CR4_FXSR
        MOV_CR4_EDX

        FXSAVE_ECX
        mov     [ecx].MXCsr, 01f80h                       // Mask KNI exceptions
        and     word ptr [ecx], NOT NPX_CW_PRECISION_MASK // Control word is
        or      word ptr [ecx], NPX_CW_PRECISION_53       // 16 bits wide here
        jmp     init_done
no_fxsr:
        fnsave  [ecx]
        // Win32 threads default to long real (53-bit significand).
        // Control word is 32 bits wide here
        and     dword ptr [ecx], NOT NPX_CW_PRECISION_MASK
        or      dword ptr [ecx], NPX_CW_PRECISION_53
init_done:

        or      eax, TS_MASK
        mov     cr0, eax
   }
   KCALLPROFOFF(69);    
}


#undef INTERRUPTS_OFF
#undef INTERRUPTS_ON

Naked INTERRUPTS_ON (void)
{
    __asm {
        sti
        ret
    }
}

Naked INTERRUPTS_OFF (void)
{
    __asm {
        cli
        ret
    }
}

BOOL __declspec(naked) INTERRUPTS_ENABLE (BOOL fEnable)
{
    __asm {
        mov     ecx, [esp+4]            // (ecx) = argument
        pushfd
        pop     eax                     // (eax) = current flags
        shr     eax, EFLAGS_IF_BIT      // 
        and     eax, 1                  // (eax) = 0 if interrupt was disabled, 1 if enabled
        
        test    ecx, ecx                // enable or disable?

        jne     short INTERRUPTS_ON

        // disable interrupt
        cli
        ret
    }
}



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
// THE SOURCE CODE IS PROVIDED "AS IS", WITH NO WARRANTIES.
//
/*****************************************************************************/
/**								Microsoft Windows							**/
/*****************************************************************************/

/*
	dhcp.c

  DESCRIPTION:



*/

#include "dhcpp.h"
#include "dhcp.h"
#include "protocol.h"
#include "netui_kernel.h"
#include "linklist.h"


LIST_ENTRY  v_EstablishedList;
CTELock     v_GlobalListLock;
extern int  v_DhcpInitDelay;
extern BOOL v_ReUseDhcpInfoWhileAPRomaing;

CTEEvent    v_DhcpEvent;
BOOL        v_fEventRunning;


#define DEFAULT_MAX_RETRIES     2
#define DEFAULT_RETRY_DIALOGUE  2

extern BOOL CouldPingGateway(DhcpInfo * pDhcp);
extern BOOL PingDhcp(DWORD dwIPAddr);

DhcpInfo * FindLockRefDhcp(DhcpInfo *pDhcp, PTSTR pName);
void StartDhcpDialogThread(DWORD dwMsg);
void DhcpEventWorker(CTEEvent * Event, void * Context);
void EventObtainLease(DhcpEvent * pEvent);

void mul64_32_64(const FILETIME *lpnum1, DWORD num2, LPFILETIME lpres) {
    __int64 num1;
    num1 = (__int64)lpnum1->dwLowDateTime * (__int64)num2;
    num1 += ((__int64)lpnum1->dwHighDateTime * (__int64)num2)<<32;
    lpres->dwHighDateTime = (DWORD)(num1>>32);
    lpres->dwLowDateTime = (DWORD)(num1&0xffffffff);
}

void add64_32_64(const FILETIME *lpnum1, DWORD num2, LPFILETIME lpres) {
    DWORD bottom = lpnum1->dwLowDateTime + num2;
    lpres->dwHighDateTime = lpnum1->dwHighDateTime + (bottom < lpnum1->dwLowDateTime ? 1 : 0);
    lpres->dwLowDateTime = bottom;
}

#ifdef THUMB

#pragma optimize("",off)
#endif
void add64_64_64(const FILETIME *lpnum1, LPFILETIME lpnum2, LPFILETIME lpres) {
    __int64 num1, num2;
    num1 = (((__int64)lpnum1->dwHighDateTime)<<32)+(__int64)lpnum1->dwLowDateTime;
    num2 = (((__int64)lpnum2->dwHighDateTime)<<32)+(__int64)lpnum2->dwLowDateTime;
    num1 += num2;
    lpres->dwHighDateTime = (DWORD)(num1>>32);
    lpres->dwLowDateTime = (DWORD)(num1&0xffffffff);
}

void sub64_64_64(const FILETIME *lpnum1, LPFILETIME lpnum2, LPFILETIME lpres) {
    __int64 num1, num2;
    num1 = (((__int64)lpnum1->dwHighDateTime)<<32)+(__int64)lpnum1->dwLowDateTime;
    num2 = (((__int64)lpnum2->dwHighDateTime)<<32)+(__int64)lpnum2->dwLowDateTime;
    num1 -= num2;
    lpres->dwHighDateTime = (DWORD)(num1>>32);
    lpres->dwLowDateTime = (DWORD)(num1&0xffffffff);
}
#ifdef THUMB

#pragma optimize("",on)
#endif

// Unsigned divide
// Divides a 64 bit number by a *31* bit number.  Doesn't work for 32 bit divisors!

void div64_32_64(const FILETIME *lpdividend, DWORD divisor, LPFILETIME lpresult) {
    DWORD bitmask;
    DWORD top;
    FILETIME wholetop = *lpdividend;
    top = 0;
    lpresult->dwHighDateTime = 0;
    for (bitmask = 0x80000000; bitmask; bitmask >>= 1) {
        top = (top<<1) + ((wholetop.dwHighDateTime&bitmask) ? 1 : 0);
        if (top >= divisor) {
            top -= divisor;
            lpresult->dwHighDateTime |= bitmask;
        }
    }
    lpresult->dwLowDateTime = 0;
    for (bitmask = 0x80000000; bitmask; bitmask >>= 1) {
        top = (top<<1) + ((wholetop.dwLowDateTime&bitmask) ? 1 : 0);
        if (top >= divisor) {
            top -= divisor;
            lpresult->dwLowDateTime |= bitmask;
        }
    }
}


//    Len, SubnetMask(1), DomainName(15), Router(3), NB Name Server(44), 
//    NB Node Type(46), NBT Scope (47), DomainServer(6)
unsigned char v_aDftReqOptions[] = 
{7, 0x01, 0x03, 0x06, 0x0f, 0x2c, 0x2e, 0x2f, 0x0};

unsigned int    v_cXid;
int             v_fXidInit;

unsigned int 
GetXid(
    char *pMacAddr,
    unsigned len
    )
{
    unsigned int i;

    if (! v_fXidInit) {
        memcpy(&i, pMacAddr + 2, 4);
        v_cXid ^= i;
        v_fXidInit++;
    }

    if (i = v_cXid++)
        ;
    else    // if it is 0, we increment again!
        i = v_cXid++;
    

    return i;
}    // GetXid()


//
// Allocate and initialize a DhcpInfo struct for a new network interface
// 
DhcpInfo *
NewDhcpInfo(
    PDHCP_PROTOCOL_CONTEXT  pProtocolContext,
    PTSTR                   pAdapter
    )
{
    DhcpInfo *pDhcp;
    DWORD cName = (_tcslen(pAdapter) + 1) * sizeof(TCHAR);

    ASSERT(NULL == FindLockRefDhcp(NULL, pAdapter));
    
    pDhcp = LocalAlloc(LPTR, sizeof(*pDhcp) + cName);
    if (NULL != pDhcp) {
        pDhcp->cRefs = 1;
        pDhcp->ARPEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // manual reset
        if (NULL == pDhcp->ARPEvent) {
            DEBUGMSG(ZONE_WARN, (L"DHCP:NewDhcpInfo - CreateEvent failed!\n"));
            LocalFree(pDhcp);
            return NULL;
        }
        
        CTEInitLock(&pDhcp->Lock);
        // technically speaking should be net_long(INADDR_BROADCAST);
        pDhcp->DhcpServer = INADDR_BROADCAST;
        
        pDhcp->Flags = DHCPCFG_AUTO_IP_ENABLED_FL;
        pDhcp->AutoInterval = DHPC_IPAUTOCONFIG_DEFAULT_INTERVAL;
        pDhcp->AutoMask = DHCP_IPAUTOCONFIGURATION_DEFAULT_MASK;
        pDhcp->AutoSubnet = DHCP_IPAUTOCONFIGURATION_DEFAULT_SUBNET;
        _tcscpy(pDhcp->Name, pAdapter);
        pDhcp->pProtocolContext = pProtocolContext;
        InitializeListHead(&pDhcp->EventList);
        
        CTEGetLock(&v_GlobalListLock, 0);
        InsertTailList(&v_EstablishedList, &pDhcp->DhcpList);
        CTEFreeLock(&v_GlobalListLock, 0);

        CTEGetLock(&pDhcp->Lock, 0);
    }
    DEBUGMSG(ZONE_WARN, (TEXT("DHCP:: $$$ NewDhcpInfo: 0x%x\n"), pDhcp));
    return pDhcp;
}   // NewDhcpInfo()

void
FreeDhcpInfo(
    DhcpInfo *pDhcp
    )
{
    DEBUGMSG(ZONE_WARN, (TEXT("DHCP:: !!! FreeDhcpInfo: 0x%x\n"), pDhcp));
    
    CTEGetLock(&v_GlobalListLock, 0);
    RemoveEntryList(&pDhcp->DhcpList);
    CTEFreeLock(&v_GlobalListLock, 0);
    
    CTEDeleteLock(&pDhcp->Lock);
    if (pDhcp->pSendOptions) {
        LocalFree(pDhcp->pSendOptions);
		pDhcp->pSendOptions = NULL;
    }
    if (pDhcp->ARPEvent) {
        CloseHandle(pDhcp->ARPEvent);
		pDhcp->ARPEvent = NULL;
    }
    LocalFree(pDhcp);
}   // FreeDhcpInfo()


uint
RefDhcpInfo(
    DhcpInfo *pDhcp
    )
{
    ASSERT(pDhcp->cRefs);
    
    return InterlockedIncrement(&pDhcp->cRefs);
}   // RefDhcpInfo()


void
CloseDhcpSocket(DhcpInfo *pDhcp);


uint
DerefDhcpInfo(
    DhcpInfo *pDhcp
    )
{
    uint cRefs;
    
    ASSERT(pDhcp->cRefs > 0);
    
    cRefs = InterlockedDecrement(&pDhcp->cRefs);
    if (0 == cRefs) {
        CloseDhcpSocket(pDhcp);
        FreeDhcpInfo(pDhcp);
    }
    
    return cRefs;
}   // DerefDhcpInfo()

void
StopDhcpTimers(
    DhcpInfo *pDhcp
    )
{
    CTEStopTimer(&pDhcp->AutonetTimer);
    if (pDhcp->SFlags & DHCPSTATE_TIMER_SCHEDULED) {
        pDhcp->SFlags &= ~DHCPSTATE_TIMER_SCHEDULED;
        CTEStopFTimer(&pDhcp->Timer);
        DerefDhcpInfo(pDhcp);   // reference for Tn/Expire timers
    }
}

void
DeleteDhcpInfo(
    DhcpInfo *pDhcp
    )
{
    //
    // In the context of DhcpNotify(DHCP_NOTIFY_DEL_INTERFACE), DelDhcpInterface() will set
    // the DHCPSTATE_DELETE flag, causing FindLockRefDhcp() to no longer be able to find
    // the deleted DhcpInfo. This will protect against attempts at double deletion.
    //
    StopDhcpTimers(pDhcp);
    DerefDhcpInfo(pDhcp);
}   // DeleteDhcpInfo()

//
// Find a DhcpInfo by name or address.
// If found, the structure lock is taken and a reference is added.
//
DhcpInfo *
FindLockRefDhcp(
    DhcpInfo *pDhcp,
    PTSTR pName
    )
{
    DhcpInfo * pTemp;

    CTEGetLock(&v_GlobalListLock, 0);

    for (pTemp = (DhcpInfo *)v_EstablishedList.Flink;
         TRUE;
         pTemp = (DhcpInfo *)pTemp->DhcpList.Flink)
    {
        if (pTemp == (DhcpInfo *)&v_EstablishedList) {
            //
            // End of list, no matching entry found
            //
            pTemp = NULL;
            break;
        }

        if (!(pTemp->SFlags & DHCPSTATE_DELETE)) {  // Skip deleted entries
            if ((pDhcp && (pTemp == pDhcp)) ||
                (pName && (0 == _tcscmp(pTemp->Name, pName)))) {
                //
                // pTemp matches
                //
                RefDhcpInfo(pTemp);
                break;
            }
        }
    }    // for()

    CTEFreeLock(&v_GlobalListLock, 0);

    if (pTemp) {
        CTEGetLock(&pTemp->Lock, 0);
    }
    return pTemp;

}    // FindLockRefDhcp()

BOOL StringToAddr(TCHAR *AddressString, DWORD *AddressValue) {
    TCHAR   *pStr = AddressString;
    PUCHAR  AddressPtr = (PUCHAR)AddressValue;
    int     i;
    int     Value;

    // Parse the four pieces of the address.
    for (i=0; *pStr && (i < 4); i++) {
        Value = 0;
        while (*pStr && TEXT('.') != *pStr) {
            if ((*pStr < TEXT('0')) || (*pStr > TEXT('9'))) {
                DEBUGMSG (ZONE_INIT,
                          (TEXT("DHCP:StringToAddr: Unable to convert %s to address\r\n"),
                           AddressString));
                return FALSE;
            }
            Value *= 10;
            Value += *pStr - TEXT('0');
            pStr++;
        }
        if (Value > 255) {
            DEBUGMSG (ZONE_INIT,
                      (TEXT("DHCP:StringToAddr: Unable to convert %s to address\r\n"),
                       AddressString));
            return FALSE;
        }
        AddressPtr[i] = Value;
        if (TEXT('.') == *pStr) {
            pStr++;
        }
    }

    // Did we get all of the pieces?
    if (i != 4) {
        DEBUGMSG (ZONE_INIT,
                  (TEXT("DHCP:StringToAddr: Unable to convert %s to address\r\n"),
                   AddressString));
        return FALSE;
    }

    DEBUGMSG (ZONE_INIT,
              (TEXT("DHCP:StringToAddr: Converted %s to address %X\r\n"),
               AddressString, *AddressValue));

    return TRUE;
}    // StringToAddr()

//
// Convert an IP address into a string (addr==0 -> string=="0.0.0.0")
//
LPTSTR
AddrToString(
    DWORD Addr,
    TCHAR *pString,
    DWORD StringLength
    )
{
    TCHAR           Buffer[CHADDR_LEN+1];
    int             i, b;
    unsigned char   *aAddr;

    ASSERT(StringLength > CHADDR_LEN);

    if (StringLength < CHADDR_LEN) {
        return NULL;
    }

    DEBUGMSG(ZONE_MISC, (TEXT("+DHCP:AddrToString: %X\r\n"), Addr));
    
    Addr = net_long(Addr);  // put it in the desired format
    aAddr = (unsigned char *)&Addr;

    for (i = 0, b = 0; ((i < 4) && (b < CHADDR_LEN)); i++) {
        do {
            Buffer[b] = aAddr[i] % 10 + TEXT('0');
            b++;
        } while ((aAddr[i] /= 10) && (b < CHADDR_LEN));
        Buffer[b] = TEXT('.');
        b++;
    }
    b--;
    Buffer[b] = '\0';    // get rid of last '.' & end str

    for (i = 0; (i <= CHADDR_LEN) && b; i++) {
            b--;
            pString[i] = Buffer[b];
    }
    pString[i] = TEXT('\0');

    DEBUGMSG(ZONE_MISC, (TEXT("-DHCP:AddrToString: %s\r\n"), pString));
    return pString;
}    // AddrToString()

// our list is typically < 10 elts so this n^2 algo shld work
void InsSort(int cBuf, uchar *pBuf) {
    uchar    c, *p, *pBig, *pEnd = pBuf + cBuf;

    while ((pBig = pBuf) < pEnd) {
        for (p = pBuf; p < pEnd; p++) {
            if (*p > *pBig)
                pBig = p;
        }
        if (pBig < --pEnd) {
            c = *pBig;
            *pBig = *pEnd;
            *pEnd = c;
        }
    }
}    // InsSort


//
// Convert a string option name (i.e. "61") into an 8 bit unsigned integer
// Return FALSE for failure.
//
BOOL
Convert(
    TCHAR *szName,
    DWORD cName,
    uchar *pc
    )
{
    DWORD n = 0;

    for ( ; cName--  > 0 && *szName != TEXT('\0'); szName++) {
        if (*szName < TEXT('0') || *szName > TEXT('9')) {
            return FALSE;   // Invalid input.
        }
        n *= 10;
        n += *szName - TEXT('0');
    }

    if (n > 255) {
        return FALSE;   // Result too large.
    }

    *pc = (uchar)n;
    return TRUE;
}

#define REG_OPTION_VALUE_NAME_LEN 4

STATUS GetReqOptions(HKEY hRoot, DhcpInfo *pDhcp) {
    HKEY    hKey;
    LONG    hRes;
    TCHAR   szName[REG_OPTION_VALUE_NAME_LEN];
    DWORD   i, cName, cBuf;
    uchar   *p, aBuf[OPTIONS_LEN], aPrevReqOptions[MAX_DHCP_REQ_OPTIONS+1];

    pDhcp->Flags &= ~(DHCPCFG_OPTION_CHANGE_FL | DHCPCFG_USER_OPTIONS_FL);

    p = aBuf + 1;
    hRes = RegOpenKeyEx(hRoot, TEXT("DhcpOptions"), 0, 0, &hKey);

    if (ERROR_SUCCESS == hRes) {
        //
        // The presence of the default value indicates that there are additional options to request.
        // Otherwise, DhcpOptions stores what the server sent.
        //
        cName = sizeof(szName);
        hRes = RegQueryValueEx(hKey, NULL, NULL, NULL, NULL, &cName);
        if (ERROR_SUCCESS == hRes && cName) {
            pDhcp->Flags |= DHCPCFG_USER_OPTIONS_FL;
        
            i = 0;
            while (i < (MAX_DHCP_REQ_OPTIONS - 1)) {
                cName = REG_OPTION_VALUE_NAME_LEN;
                hRes = RegEnumValue(hKey, i++, szName, &cName, NULL, NULL, 
                    NULL, NULL);
                if (ERROR_SUCCESS == hRes) {
                    if (Convert(szName, REG_OPTION_VALUE_NAME_LEN, p))
                        p++;
                } else
                    break;
            }    // while()
            
            aBuf[0] = (uchar)(p - aBuf - 1);
            InsSort(aBuf[0], &aBuf[1]);

            ASSERT(aBuf[0] < MAX_DHCP_REQ_OPTIONS);
            memcpy(pDhcp->ReqOptions, aBuf, min(MAX_DHCP_REQ_OPTIONS, aBuf[0] + 1));
        }
        RegCloseKey (hKey);
    }

    // RFC 2131 requires that if a client requests parameters in a DISCOVER, the 
    // list must be included in subsequent REQUEST messages. So, if our option 
    // list changes, set a flag which will drop us back to INIT state.
    cBuf = sizeof(aPrevReqOptions);
    if (GetRegBinaryValue(hRoot,TEXT("PrevReqOptions"),aPrevReqOptions,&cBuf)) {
        ASSERT(cBuf == (DWORD)(aPrevReqOptions[0] + 1));
        if (memcmp(aPrevReqOptions,pDhcp->ReqOptions,cBuf)) {
            DEBUGMSG(ZONE_WARN,(TEXT("DHCP:GetReqOptions, detected ReqOptions change\r\n")));
            pDhcp->Flags |= DHCPCFG_OPTION_CHANGE_FL;
        }
    }

    return ERROR_SUCCESS;
}    // GetReqOptions


// Third parameter to GetSendOptionsWorker
#define DHCP_SEND_OPTIONS_ADAPTER       FALSE
#define DHCP_SEND_OPTIONS_GLOBAL        TRUE

#define MAX_SEND_OPTIONS (OPTIONS_LEN - MAX_DHCP_REQ_OPTIONS - 4 - 3 - 9 - 6)

//
// Read DhcpSendOptions from the specified registry key and return TRUE if
// a valid set of send options was read.
// Return FALSE for failure.
//
BOOL
GetSendOptionsWorker(
    HKEY hRoot,
    DhcpInfo *pDhcp,
    BOOL bGlobal
    )
{
    BOOL    bRet = FALSE;
    HKEY    hKey;
    LONG    hRes;
    TCHAR   szName[REG_OPTION_VALUE_NAME_LEN];
    DWORD   iValue, cBytes, cName, cBuf, Type, Max;
    uchar   *p, aBuf[OPTIONS_LEN];

    Max = MAX_SEND_OPTIONS;
    if (pDhcp->pSendOptions) {
        Max -= pDhcp->cSendOptions;
    }

    hRes = RegOpenKeyEx(hRoot, TEXT("DhcpSendOptions"), 0, 0, &hKey);
    if (ERROR_SUCCESS == hRes) {
        iValue = cBytes = 0;
        while (cBytes < Max) {
            cName = REG_OPTION_VALUE_NAME_LEN;
            cBuf = Max - cBytes;
            hRes = RegEnumValue(hKey, iValue++, szName, &cName, NULL, &Type, &aBuf[cBytes], &cBuf);
            if (ERROR_SUCCESS == hRes && REG_BINARY == Type) {
                if (DHCP_SEND_OPTIONS_GLOBAL == bGlobal) {
                    //
                    // Skip each global option for which an adapter specific option was specified
                    //
                    if (FindSendOption(pDhcp, aBuf[0])) {
                        continue;
                    }
                }
                cBytes += cBuf;
            } else {
                break;
            }
        }    // while()

        if (cBytes > 0) {
            if (pDhcp->pSendOptions) {
                p = pDhcp->pSendOptions + pDhcp->cSendOptions;
                memcpy(p, aBuf, cBytes);
                pDhcp->cSendOptions += cBytes;
                bRet = TRUE;
            } else {
                p = LocalAlloc(LPTR, MAX_SEND_OPTIONS);
                if (p) {
                    memcpy(p, aBuf, cBytes);
                    pDhcp->pSendOptions = p;
                    pDhcp->cSendOptions = cBytes;
                    pDhcp->Flags |= DHCPCFG_SEND_OPTIONS_FL;
                    bRet = TRUE;
                }
            }
        }

        RegCloseKey (hKey);
    }

    return bRet;
}    // GetSendOptionsWorker

void
GetSendOptions(
    HKEY hKey,
    DhcpInfo *pDhcp
    )
{
    HKEY hRoot;

    if (pDhcp->pSendOptions) {
        LocalFree(pDhcp->pSendOptions);
		pDhcp->pSendOptions = NULL;
        pDhcp->cSendOptions = 0;
    }

    //
    // Get adapter specific send options first
    //
    GetSendOptionsWorker(hKey, pDhcp, DHCP_SEND_OPTIONS_ADAPTER);

    if (hRoot = OpenDHCPKey(NULL)) {
        //
        // Get global send options
        //
        GetSendOptionsWorker(hRoot, pDhcp, DHCP_SEND_OPTIONS_GLOBAL);
        RegCloseKey(hRoot);
    }
}


//
// Read IP address strings from the registry and convert them to DWORDs
//
BOOL GetRegIPAddr(HKEY hKey, LPTSTR lpVal, LPDWORD lpdwIPAddrs, DWORD dwNumIPAddrs) {
    TCHAR   Buffer[MAX_REG_STR];
    LPTSTR  lpBuf = Buffer;

    Buffer[0] = TEXT('\0');
    // GetRegMultiSZValue works on REG_SZ strings as well as REG_MULTI_SZ
    if (!GetRegMultiSZValue(hKey, lpVal, lpBuf, sizeof(Buffer))) {
        return FALSE;
    }

    while (dwNumIPAddrs) {
        if (0 == *lpBuf) {
            //
            // Treat null string as 0.0.0.0
            //
            *lpdwIPAddrs = 0;
        } else {
            StringToAddr(lpBuf, lpdwIPAddrs);
            lpBuf += _tcslen(lpBuf) + 1;
        }
        lpdwIPAddrs++;
        dwNumIPAddrs--;
    }
    return TRUE;

}   // GetRegIPAddr

//
// Convert IP addresses to strings and write them to the registry
//
BOOL
SetRegIPAddr(
    HKEY hKey,
    LPWSTR lpVal,
    LPDWORD lpdwIPAddrs,
    DWORD dwNumIPAddrs
    )
{
    TCHAR   Buffer[MAX_REG_STR];
    BOOL    bRet = FALSE;
    DWORD   i,n;

    Buffer[0] = Buffer[1] = TEXT('\0');
    i = 0;
    for (n = 0; n < dwNumIPAddrs; n++) {
        if (lpdwIPAddrs[n]) {
            if (AddrToString(lpdwIPAddrs[n], &Buffer[i], MAX_REG_STR-i)) {
                i += _tcslen(&Buffer[i]) + 1;
                if (i < MAX_REG_STR) {
                    Buffer[i] = TEXT('\0');
                } else {
                    Buffer[MAX_REG_STR-2] = TEXT('\0');
                    Buffer[MAX_REG_STR-1] = TEXT('\0');
                    break;
                }
            }
        } else {
            break;
        }
    }

    if (i) {
        bRet = SetRegMultiSZValue(hKey, lpVal, Buffer);
    }
    return bRet;
}   // SetRegIPAddr


//
// Read the named registry flag value and set or clear the corresponding DHCPCFG_* bit.
// If it is not in the registry, then set the DHCPCFG_* bit according to bDefaultState.
// Return TRUE if the flag is set and FALSE if it is cleared.
//
BOOL
GetRegDhcpConfigFlag(
    HKEY hKey,
    LPWSTR lpValName,
    DhcpInfo * pDhcp,
    DWORD   Flag,
    BOOL    bDefaultState
    )
{
    int i;

    i = (bDefaultState == TRUE);

    GetRegDWORDValue(hKey, lpValName, &i);

    if (i) {
        pDhcp->Flags |= Flag;
    } else {
        pDhcp->Flags &= ~Flag;
    }

    return (i == 0) ? FALSE : TRUE;
}

//
// Open either the adapter specific parameters registry key or the global key if pDhcp==NULL.
// 
HKEY
OpenDHCPKey(
    DhcpInfo * pDhcp
    )
{
    TCHAR   Buffer[MAX_REG_STR];
    LPTSTR  pName;
    HKEY    hKey;
    LONG    hRes;

    if (pDhcp) {
        //
        // Format the adapter specific parameters registry key name.
        //
        if (S_OK != StringCchPrintf(Buffer, MAX_REG_STR, L"Comm\\%s\\Parms\\TcpIp", pDhcp->Name)) {
            return NULL;
        }
        pName = Buffer;
    } else {
        //
        // Use the system-wide network parameters registry key name
        //
        pName = L"Comm\\TcpIp\\Parms";
    }

    hRes = RegOpenKeyEx (HKEY_LOCAL_MACHINE, pName, 0, 0, &hKey);
    if (hRes) {
        return NULL;
    }
    return hKey;
}

STATUS GetDhcpConfig(DhcpInfo *pDhcp) {
    HKEY    hKey;
    BOOL    fStatus;
    uint    fDhcpEnabled, cSize;

    DEBUGMSG (ZONE_INIT, (TEXT("+DHCP:GetDhcpConfig(%s):\r\n"), pDhcp->Name));

    //
    //    Assign default settings
    //
    pDhcp->cMaxRetry = DEFAULT_MAX_RETRIES;
    pDhcp->InitDelay = v_DhcpInitDelay;
    pDhcp->cRetryDialogue = DEFAULT_RETRY_DIALOGUE;

    cSize = (v_aDftReqOptions[0] + 1);
    if (cSize > sizeof(v_aDftReqOptions))
        cSize = sizeof(v_aDftReqOptions);

    if (cSize > MAX_DHCP_REQ_OPTIONS)
        cSize = MAX_DHCP_REQ_OPTIONS;

    memcpy(pDhcp->ReqOptions, v_aDftReqOptions, cSize);

    // Open the Registry Key.
    hKey = OpenDHCPKey(pDhcp);
    
    if (hKey) {
        fStatus = GetRegDWORDValue(hKey, TEXT("EnableDHCP"), &fDhcpEnabled);

        if (fStatus && fDhcpEnabled)
            pDhcp->Flags |= DHCPCFG_DHCP_ENABLED_FL;

        fStatus = GetRegIPAddr(hKey, TEXT("DhcpIPAddress"), &pDhcp->IPAddr, 1); 
        if (fStatus && pDhcp->IPAddr) {
            fStatus = GetRegIPAddr(hKey, TEXT("DhcpSubnetMask"), &pDhcp->SubnetMask, 1);
            if (fStatus && pDhcp->SubnetMask) {
                // Get the DhcpServer
                GetRegIPAddr(hKey, TEXT("DhcpServer"), &pDhcp->DhcpServer, 1);

                // Get the DhcpDftGateway
                GetRegIPAddr(hKey, TEXT("DhcpDefaultGateway"), &pDhcp->Gateway, 1);

                GetRegIPAddr(hKey, TEXT("DhcpDNS"), pDhcp->DNS, MAX_RESOLVER);
                GetRegIPAddr(hKey, TEXT("DhcpWINS"), pDhcp->WinsServer, MAX_RESOLVER);

                // Get Lease Info
                GetRegDWORDValue(hKey, TEXT("LeaseObtainedLow"), &pDhcp->LeaseObtained.dwLowDateTime);
                GetRegDWORDValue(hKey, TEXT("LeaseObtainedHigh"), &pDhcp->LeaseObtained.dwHighDateTime);

                GetRegDWORDValue(hKey, TEXT("Lease"), &pDhcp->Lease);
                // Get T1 & T2
                GetRegDWORDValue(hKey, TEXT("T1"), &pDhcp->T1);
                GetRegDWORDValue(hKey, TEXT("T2"), &pDhcp->T2);
            }
        }

        // Get MaxRetries, InitDelayInterval
        GetRegDWORDValue(hKey, TEXT("DhcpMaxRetry"), &pDhcp->cMaxRetry);
        GetRegDWORDValue(hKey, TEXT("DhcpInitDelayInterval"), &pDhcp->InitDelay);
        GetRegDWORDValue(hKey, TEXT("DhcpRetryDialogue"), &pDhcp->cRetryDialogue);

        GetRegDhcpConfigFlag(hKey, TEXT("DhcpNoMacCompare"), pDhcp, DHCPCFG_NO_MAC_COMPARE_FL, FALSE);
        GetRegDhcpConfigFlag(hKey, TEXT("DhcpConstantRate"), pDhcp, DHCPCFG_CONSTANT_RATE_FL, FALSE);
        GetRegDhcpConfigFlag(hKey, TEXT("DhcpDirectRenewal"), pDhcp, DHCPCFG_DIRECT_RENEWAL_FL, FALSE);

        GetReqOptions(hKey, pDhcp);
        GetSendOptions(hKey, pDhcp);

        // Get auto IP config state
        if (GetRegDhcpConfigFlag(hKey, TEXT("AutoCfg"), pDhcp, DHCPCFG_AUTO_IP_ENABLED_FL, TRUE)) {
            GetRegDWORDValue(hKey, TEXT("AutoSeed"), &pDhcp->AutoSeed);
            GetRegIPAddr(hKey, TEXT("AutoIP"), &pDhcp->AutoIPAddr, 1);
            GetRegIPAddr(hKey, TEXT("AutoSubnet"), &pDhcp->AutoSubnet, 1);
            GetRegIPAddr(hKey, TEXT("AutoMask"), &pDhcp->AutoMask, 1);
            GetRegDWORDValue(hKey, TEXT("AutoInterval"), &pDhcp->AutoInterval);
            if (pDhcp->AutoIPAddr == pDhcp->IPAddr) {
                pDhcp->SFlags |= DHCPSTATE_AUTO_CFG_IP;
            }

            
            //
            // DhcpEnableImmediateAutoIP is used by the wireless setup code to indicate that the normal
            // DHCP behavior should be skipped initially and an auto ip address obtained right away to
            // speed up ad-hoc mode scenarios.
            // 
            if (GetRegDhcpConfigFlag(hKey, TEXT("DhcpEnableImmediateAutoIP"),
                                     pDhcp, DHCPCFG_FAST_AUTO_IP_FL, FALSE)) {
                //
                //  So the first time we'll try DISCOVER DFT_LOOP_FL no of times.
                //
                pDhcp->SFlags |= DHCPSTATE_WIRELESS_ADHOC;
            }
            RegDeleteValue(hKey, TEXT("DhcpEnableImmediateAutoIP"));
        }

        RegCloseKey(hKey);
    }

    DEBUGMSG (ZONE_INIT, (TEXT("-DHCP:GetDhcpConfig(%s):\r\n"), pDhcp->Name));
    return DHCP_SUCCESS;

}    // GetDhcpConfig()


STATUS SetDhcpConfig(DhcpInfo *pDhcp) {
    TCHAR   Buffer[MAX_REG_STR];
    HKEY    hKey;
    BOOL    fStatus;
    DWORD   DomainNameLen = 0;

    DEBUGMSG (ZONE_INIT, (TEXT("+DHCP:SetDhcpConfig(%s):\r\n"), pDhcp->Name));

    // Open the Registry Key.
    hKey = OpenDHCPKey(pDhcp);
    
    if (hKey) {
        fStatus = SetRegIPAddr(hKey, L"DhcpIPAddress", &pDhcp->IPAddr, 1);
        DEBUGMSG (ZONE_WARN, (TEXT("DHCP:SetDhcpConfig: set IPAddr %X\r\n"),
            pDhcp->IPAddr));
        if (fStatus) {
            SetRegIPAddr(hKey, L"DhcpSubnetMask", &pDhcp->SubnetMask, 1);
            SetRegIPAddr(hKey, L"DhcpServer", &pDhcp->DhcpServer, 1);
            SetRegIPAddr(hKey, L"DhcpDefaultGateway", &pDhcp->Gateway, 1);

            SetRegIPAddr(hKey, L"DhcpDNS", pDhcp->DNS, MAX_RESOLVER);
            SetRegIPAddr(hKey, L"DhcpWINS", pDhcp->WinsServer, MAX_RESOLVER);

            // Set Lease Times
            SetRegDWORDValue(hKey, TEXT("LeaseObtainedLow"), pDhcp->LeaseObtained.dwLowDateTime);
            SetRegDWORDValue(hKey, TEXT("LeaseObtainedHigh"), pDhcp->LeaseObtained.dwHighDateTime);
            DEBUGMSG (ZONE_WARN, 
                (TEXT("DHCP:SetDhcpConfig: set LeaseObtained %x %x\r\n"),
                pDhcp->LeaseObtained.dwHighDateTime, 
                pDhcp->LeaseObtained.dwLowDateTime));

            SetRegDWORDValue(hKey, TEXT("Lease"), pDhcp->Lease);
            // Set T1 & T2
            SetRegDWORDValue(hKey, TEXT("T1"), pDhcp->T1);
            SetRegDWORDValue(hKey, TEXT("T2"), pDhcp->T2);
            DEBUGMSG (ZONE_WARN, 
                (TEXT("DHCP:SetDhcpConfig: T1 %x T2 %x Lease %x\r\n"),
                pDhcp->T1, pDhcp->T2, pDhcp->Lease));

            // Store the list of params requested from server so we can tell
            // if it has changed after a reboot.
            SetRegBinaryValue(hKey,TEXT("PrevReqOptions"),pDhcp->ReqOptions,pDhcp->ReqOptions[0]+1);
        }

        // Save auto IP config state
        if (pDhcp->Flags & DHCPCFG_AUTO_IP_ENABLED_FL) {
            SetRegDWORDValue(hKey, TEXT("AutoSeed"), pDhcp->AutoSeed);
            SetRegIPAddr(hKey, L"AutoIP", &pDhcp->AutoIPAddr, 1);
            SetRegIPAddr(hKey, L"AutoSubnet", &pDhcp->AutoSubnet, 1);
            SetRegIPAddr(hKey, L"AutoMask", &pDhcp->AutoMask, 1);
            SetRegDWORDValue(hKey, TEXT("AutoInterval"), pDhcp->AutoInterval);
        } else {
            SetRegDWORDValue(hKey, TEXT("AutoCfg"), 0);
        }

        if (pDhcp->aDomainName[0]) {
            DomainNameLen = min(MAX_REG_STR-1, pDhcp->aDomainName[0]);

            //
            // Set adapter Domain name
            //
            if (-1 != mbstowcs(Buffer, &pDhcp->aDomainName[1], DomainNameLen)) {
                Buffer[DomainNameLen]= (L'\0');
                SetRegSZValue(hKey, TEXT("Domain"), Buffer);
            }
        } else {
            RegDeleteValue(hKey, TEXT("Domain"));
        }

        RegCloseKey (hKey);
    }    // if (hKey)

    if (DomainNameLen) {
        //
        // Set system-wide DNSDomain with latest domain name
        //
        if (hKey = OpenDHCPKey(NULL)) {
            if (-1 != mbstowcs(Buffer, &pDhcp->aDomainName[1], DomainNameLen)) {
                Buffer[DomainNameLen]= (L'\0');
                SetRegSZValue(hKey, TEXT("DNSDomain"), Buffer);
            }
            RegCloseKey(hKey);
        }
    }

    DEBUGMSG (ZONE_INIT, (TEXT("-DHCP:SetDhcpConfig(%s):\r\n"), pDhcp->Name));
    return DHCP_SUCCESS;
}    // SetDhcpConfig()


//
// TakeNetDown:
//
// Indicate to TCP/IP that the DHCP address is no longer valid.
// Indicate to AFD that the interface has gone away.
// Perform DHCP state cleanup.
//
// Assumes caller owns pDhcp->Lock
//
void
TakeNetDown(
    DhcpInfo *pDhcp,
    BOOL fDiscardIPAddr
    )
{
    HKEY hKey;

    RETAILMSG(1, (TEXT("DHCP:TakeNetDown: pDhcp 0x%x IP %X!\r\n"),
        pDhcp, pDhcp->IPAddr));

    // Verify that caller owns lock
    ASSERT( pDhcp->Lock.OwnerThread == (HANDLE )GetCurrentThreadId() );

    pDhcp->ARPResult = ERROR_UNEXP_NET_ERR;
    SetEvent(pDhcp->ARPEvent);  // wake up any waiting threads

    pDhcp->pProtocolContext->pfnSetAddr((ushort)pDhcp->NteContext, NULL, 0, 0, 0);

    if (DHCPSTATE_AFD_INTF_UP_FL & pDhcp->SFlags) {
        pDhcp->SFlags &= ~DHCPSTATE_AFD_INTF_UP_FL;
        (*pfnAfdAddInterface)(pDhcp->Name, pDhcp->Nte, pDhcp->NteContext,
            ADD_INTF_DELETE_FL, pDhcp->IPAddr, pDhcp->SubnetMask, 
            0, NULL, 0, NULL);
    }

    if (fDiscardIPAddr)
    {
        pDhcp->IPAddr = pDhcp->SubnetMask = 0;
        // Delete the IPAddr.
        hKey = OpenDHCPKey(pDhcp);
        if (hKey) {
            RegDeleteValue(hKey, TEXT("DhcpIPAddress"));
            RegDeleteValue(hKey, TEXT("DhcpSubnetMask"));
            RegCloseKey (hKey);
        }
    }
}    // TakeNetDown()


//
// Discard certain events based on the input event
//
// Assumes caller has interface lock (pDhcp->Lock)
//
// Returns TRUE if one or more events were discarded. FALSE when no events are discarded.
//
BOOL
DiscardEvents(
    DhcpInfo *pDhcp,
    DWORD EventCode
    )
{
    DhcpEvent * pCurrEvent;
    DhcpEvent * pNextEvent;
    BOOL bOneOrMoreEventsDeleted = FALSE;

    // Verify that caller owns lock
    ASSERT( pDhcp->Lock.OwnerThread == (HANDLE )GetCurrentThreadId() );

    for (pCurrEvent = (DhcpEvent *)pDhcp->EventList.Flink;
        pCurrEvent != (DhcpEvent *)&pDhcp->EventList;
        pCurrEvent = pNextEvent)
    {
        BOOL bDeleteCurrentEvent = FALSE;

        //
        // Save next event pointer now since we might delete/free the current event
        //
        pNextEvent = (DhcpEvent *)pCurrEvent->List.Flink;

        switch (EventCode) {
        case DHCP_EVENT_MEDIA_DISCONNECT:
        case DHCP_EVENT_MEDIA_CONNECT:
            //
            // For both connect and disconnect, all queued connect and disconnect events
            // should be discarded
            //
            switch (pCurrEvent->Code) {
            case DHCP_EVENT_MEDIA_DISCONNECT:
            case DHCP_EVENT_MEDIA_CONNECT:
                bDeleteCurrentEvent = TRUE;
                break;
            }
            break;

        case DHCP_EVENT_DEL_INTERFACE:
            //
            // Discard all other events - Nothing else matters if the interface is going away.
            //
            bDeleteCurrentEvent = TRUE;
            break;

        case DHCP_EVENT_RELEASE_LEASE:
            //
            // Don't do new or renew if we're going to release.
            //
            switch (pCurrEvent->Code) {
            case DHCP_EVENT_OBTAIN_LEASE:
            case DHCP_EVENT_RENEW_LEASE:
                bDeleteCurrentEvent = TRUE;
                break;
            }
            break;
        
        default:
            //
            // No events to discard for this type of event
            //
            return FALSE;
            break;
        }

        if (bDeleteCurrentEvent) {
            RemoveEntryList(&pCurrEvent->List);
            LocalFree(pCurrEvent);
            bOneOrMoreEventsDeleted = TRUE;
        }
    }
    return bOneOrMoreEventsDeleted;
}   // DiscardEvents

//
// Allocate and queue a DhcpEvent for the requested EventCode.
//
BOOL
PutInWorkerQEx(
    DhcpInfo    *pDhcp,
    DWORD       EventCode,
    PDHCP_PROTOCOL_CONTEXT    pProtocolContext,
    void        *Nte,
    unsigned    NteContext,
    BYTE        *ClientID,
    uint        ClientIDLen
    )
{
    DhcpEvent * pEvent;

    // Verify that caller owns lock
    ASSERT( pDhcp->Lock.OwnerThread == (HANDLE )GetCurrentThreadId() );

    pEvent = LocalAlloc(LPTR, sizeof(DhcpEvent));

    if (pEvent) {
        pEvent->pDhcp = pDhcp;
        pEvent->Code = EventCode;
        pEvent->pProtocolContext = pProtocolContext;
        pEvent->Nte = Nte;
        pEvent->NteContext = NteContext;
        pEvent->ClientIDLen = min(CHADDR_LEN, ClientIDLen); // ensure client ID fits in dhcp packet
        memcpy(pEvent->ClientID, ClientID, pEvent->ClientIDLen);

        DiscardEvents(pDhcp, EventCode);
        InsertTailList(&pDhcp->EventList, (PLIST_ENTRY)pEvent);

        //
        // Give advance warning of media disconnect events so the DHCP protocol
        // can be aborted.
        //
        if (DHCP_EVENT_MEDIA_CONNECT == EventCode) {
            pDhcp->SFlags &= ~DHCPSTATE_MEDIA_DISC_PENDING;
        } else if (DHCP_EVENT_MEDIA_DISCONNECT == EventCode) {
            pDhcp->SFlags |= DHCPSTATE_MEDIA_DISC_PENDING;
        }

        CTEGetLock(&v_GlobalListLock, 0);
        if (FALSE == v_fEventRunning) {
            v_fEventRunning = TRUE;
            CTEScheduleEvent(&v_DhcpEvent, NULL);
        }
        CTEFreeLock(&v_GlobalListLock, 0);
        return TRUE;
    }

    DEBUGMSG(ZONE_ERROR, (L"DHCP:PutInWorkerQEx - LocalAlloc failed!!!\r\n"));

    return FALSE;
}    // PutInWorkerQEx


BOOL
PutInWorkerQ(
    DhcpInfo    *pDhcp,
    DWORD       EventCode
    )
{
	return PutInWorkerQEx(pDhcp, EventCode, NULL, NULL, 0, NULL, 0);
}    // PutInWorkerQ


//
// Find the DhcpInfo corresponding to the specified adapter name and queue the requested event to it.
// Encapsulates a common sequence of operations for several event types.
//
STATUS
QueueEventByAdapterName(
    PTSTR       pAdapter,
    DWORD       EventCode
    )
{
    STATUS   Status;
    DhcpInfo *pDhcp;

    pDhcp = FindLockRefDhcp(NULL, pAdapter);
    if (pDhcp) {
        PutInWorkerQ(pDhcp, EventCode);
        CTEFreeLock(&pDhcp->Lock, 0);
        DerefDhcpInfo(pDhcp);
        Status = DHCP_SUCCESS;
    } else {
        Status = DHCP_NOINTERFACE;
    }
    return Status;
}


//
// Close the Dhcp interface's socket if open and set it to NULL
// (Afd doesn't check the socket parameter, so we have to)
//
void
CloseDhcpSocket(
    DhcpInfo *pDhcp
    )
{
    LPSOCK_INFO pSock;

    pSock = pDhcp->Socket;
    if (pSock) {
        pDhcp->Socket = NULL;
        CallSock(Close) (pSock);
    }
}

// called when our lease has expired!
void ProcessExpire(DhcpInfo *pDhcp) {

    DEBUGMSG(ZONE_TIMER|ZONE_WARN, (TEXT("+DHCP:ProcessExpire - pDhcp 0x%X\r\n"), pDhcp));
    
    CTEGetLock(&pDhcp->Lock, 0);
    pDhcp->SFlags &= ~DHCPSTATE_TIMER_SCHEDULED;
    DEBUGMSG(ZONE_TIMER, (TEXT("DHCP:ProcessExpire - Lease Time Expired, bringing net down %x\r\n"), pDhcp));

    // take the interface down!
    pDhcp->SFlags |= DHCPSTATE_LEASE_EXPIRE;
    TakeNetDown(pDhcp, DISCARD_IPADDR);
    ClearDHCPNTE(pDhcp);
    CloseDhcpSocket(pDhcp);
    PutInWorkerQ(pDhcp, DHCP_EVENT_OBTAIN_LEASE);

    CTEFreeLock(&pDhcp->Lock, 0);
    DerefDhcpInfo(pDhcp);   // StartMidwayTimer reference

    DEBUGMSG(ZONE_TIMER, (TEXT("-DHCP:ProcessExpire - pDhcp 0x%X\r\n"), pDhcp));
}    // ProcessExpire()


//
// Compute a FILETIME time relative to the lease time
//
void
ComputeLeaseRelativeFILETIME(
    IN DhcpInfo *pDhcp,
    IN int End,
    OUT FILETIME * EndTime
    )
{
    EndTime->dwLowDateTime = End;
    EndTime->dwHighDateTime = 0;
    mul64_32_64(EndTime, TEN_M, EndTime);
    add64_64_64(&pDhcp->LeaseObtained, EndTime, EndTime);
}

//
// Determine whether to start T1, T2 or Expire timer and start it.
//
void
StartMidwayTimer(
    DhcpInfo *pDhcp,
    int End,
    CTEEventRtn Rtn1, 
    CTEEventRtn Rtn2
    )
{
    FILETIME CurTime, EndTime, TempTime;

    // calc Expire time, if > 1 min restart T1 timer
    ComputeLeaseRelativeFILETIME(pDhcp, End, &EndTime);

    RefDhcpInfo(pDhcp); // reference it so it doesn't disappear before the timer fires.

    GetCurrentFT(&CurTime);
    if (CompareFileTime(&CurTime, &EndTime) >= 0) {
        DEBUGMSG(ZONE_TIMER, 
            (TEXT("DHCP:StartMidwayTimer: late already! start 2nd timer %x %x\r\n"),
            EndTime.dwHighDateTime, EndTime.dwLowDateTime));
        // start next timer
        pDhcp->SFlags |= DHCPSTATE_TIMER_SCHEDULED;
        CTEStartFTimer(&pDhcp->Timer, EndTime, (CTEEventRtn)Rtn2, pDhcp);
    } else {
        sub64_64_64(&EndTime, &CurTime, &TempTime);
        
        DEBUGMSG(ZONE_TIMER, 
            (TEXT("DHCP:StartMidwayTimer: CurTime %x %x Endtime %x %x dt %x %x\r\n"),
            CurTime.dwHighDateTime, CurTime.dwLowDateTime,
            EndTime.dwHighDateTime, EndTime.dwLowDateTime,
            TempTime.dwHighDateTime, TempTime.dwLowDateTime));

        // if more than 1 min remain, restart timer
        // otherwise start the next timer
        if (TempTime.dwHighDateTime > 0 ||
            (TempTime.dwHighDateTime == 0 &&
            TempTime.dwLowDateTime >= (60 * TEN_M))) {

            div64_32_64(&TempTime, 2, &TempTime);
            add64_64_64(&CurTime, &TempTime, &TempTime);

            DEBUGMSG(ZONE_TIMER, 
                (TEXT("DHCP:StartMidwayTimer: starting timer for %x %x\r\n"),
                TempTime.dwHighDateTime, TempTime.dwLowDateTime));

            // restart the timer
            pDhcp->SFlags |= DHCPSTATE_TIMER_SCHEDULED;
            CTEStartFTimer(&pDhcp->Timer, TempTime, (CTEEventRtn)Rtn1, pDhcp);
        } else {
            DEBUGMSG(ZONE_TIMER, 
                (TEXT("DHCP:StartMidwayTimer: starting 2nd timer for %x %x\r\n"),
                EndTime.dwHighDateTime, EndTime.dwLowDateTime));
            // start next timer
            pDhcp->SFlags |= DHCPSTATE_TIMER_SCHEDULED;
            CTEStartFTimer(&pDhcp->Timer, EndTime, (CTEEventRtn)Rtn2, pDhcp);
        }
    }

}    // StartMidwayTimer()


void ProcessT1orT2(DhcpInfo *pDhcp);

void StartT1Timer(DhcpInfo *pDhcp) {
    FILETIME Ft;

    ComputeLeaseRelativeFILETIME(pDhcp, pDhcp->T1, &Ft);

    // Start T1 timer
    DEBUGMSG(ZONE_TIMER, (TEXT("DHCP:StartT1Timer: init LO %x %x , T1 %x %x\r\n"),
        pDhcp->LeaseObtained.dwHighDateTime, pDhcp->LeaseObtained.dwLowDateTime,
        Ft.dwHighDateTime, Ft.dwLowDateTime));

    RefDhcpInfo(pDhcp); // reference it so it doesn't disappear before the timer fires.
    pDhcp->SFlags |= DHCPSTATE_TIMER_SCHEDULED;
    CTEStartFTimer(&pDhcp->Timer, Ft, (CTEEventRtn)ProcessT1orT2, pDhcp);

}    // StartT1Timer()

// Caller should hold DhcpInfo->Lock
STATUS
DhcpInitSock(
    DhcpInfo *pDhcp,
    int       SrcIP
    )
{
    SOCKADDR_IN SockAddr;
    STATUS      Status;
    LPSOCK_INFO Sock = NULL;
    int         const_true = 1;

    DEBUGMSG(ZONE_INIT, (TEXT("+DHCP:DhcpInitSock: pDhcp %#X\n"), pDhcp));

    CloseDhcpSocket(pDhcp);

    do
    {
        memset ((char *)&SockAddr, 0, sizeof(SockAddr));
        SockAddr.sin_family = AF_INET;
        SockAddr.sin_port = net_short(DHCP_CLIENT_PORT);

        SockAddr.sin_addr.S_un.S_addr = SrcIP;    // net_long(0);

        // set flag that this is an internal socket
        Sock = (LPSOCK_INFO) CallAfd(Socket)(AFD_SOCK_INTERNAL | AF_INET, SOCK_DGRAM, 
            IPPROTO_UDP, 0, NULL);
        if (Sock == NULL) {
            Status = GetLastError();
            DEBUGMSG(ZONE_ERROR, (TEXT("DHCP:DhcpInitSock: Socket error %d\n"), Status));
            break;
        }

        Status = CallSock(SetOption)(Sock, SOL_SOCKET, SO_REUSEADDR, &const_true, sizeof(const_true));
        if (Status != NO_ERROR) {
            DEBUGMSG(ZONE_ERROR, (TEXT("DHCP:DhcpInitSock: SetOption SO_REUSEADDR error %d\n"), Status));
            break;
        }

        
        Status = CallSock(SetOption)(Sock, SOL_SOCKET, SO_BROADCAST, &const_true, sizeof(const_true));
        if (Status != NO_ERROR) {
            DEBUGMSG(ZONE_ERROR, (TEXT("DHCP:DhcpInitSock: SetOption SO_BROADCAST error %d\n"), Status));
            break;
        }

        const_true = 1234;
        Status = CallSock(SetOption)(Sock, SOL_SOCKET, 0x8000, &const_true, sizeof(const_true));
        if (Status != NO_ERROR) {
            DEBUGMSG(ZONE_ERROR, (TEXT("DHCP:DhcpInitSock: SetOption Special AFD error %d\n"), Status));
            break;
        }

        Status = CallSock(Bind)(Sock, (SOCKADDR *)&SockAddr, sizeof(SockAddr));
        if (Status != NO_ERROR) {
            DEBUGMSG(ZONE_ERROR, (TEXT("DHCP:DhcpInitSock: Bind error %d\n"), Status));
            break;
        }

        pDhcp->Socket = Sock;
    } while (FALSE);

    //
    //    On failure, cleanup the socket if necessary
    //
    if (Status != NO_ERROR && Sock) {
        CallSock(Close) (Sock);
        ASSERT(NULL == pDhcp->Socket);
    }

    DEBUGMSG(ZONE_INIT, (TEXT("-DHCP:DhcpInitSock: pDhcp 0x%X/0x%X Ret: %d\n"), pDhcp, pDhcp->Socket, Status));

    return Status;
}    // InitSocket()


//
// Function to call ARP's SetDHCPNTE - caller must have pDhcp->Lock
//
BOOL
SetDHCPNTE(
    DhcpInfo * pDhcp
    )
{
    BOOL RetStatus = FALSE;

    // Verify that caller owns lock
    ASSERT( pDhcp->Lock.OwnerThread == (HANDLE )GetCurrentThreadId() );

    if (0 == (pDhcp->SFlags & DHCPSTATE_DHCPNTE_SET)) {
        pDhcp->SFlags |= DHCPSTATE_DHCPNTE_SET;
        RetStatus = pDhcp->pProtocolContext->pfnSetNTE(pDhcp->NteContext, NULL, NULL, NULL);
        if (!RetStatus)
            pDhcp->SFlags &= ~DHCPSTATE_DHCPNTE_SET;
    }

    return RetStatus;
}


BOOL
ClearDHCPNTE(
    DhcpInfo * pDhcp
    )
{
    BOOL RetStatus = FALSE;

    // Verify that caller owns lock
    ASSERT( pDhcp->Lock.OwnerThread == (HANDLE )GetCurrentThreadId() );

    if (pDhcp->SFlags & DHCPSTATE_DHCPNTE_SET) {
        // even if this fails we're going to assume that we no longer have
        // the dhcpnte set
        pDhcp->SFlags &= ~DHCPSTATE_DHCPNTE_SET;
        RetStatus = pDhcp->pProtocolContext->pfnSetNTE(0x1ffff, NULL, NULL, NULL);
    }

    return RetStatus;
}

//
// Get an IP address to use for DHCP negotiation.
// For a RAS interface, find an IP address from a "real" interface
//
// caller must hold the Dhcp->Lock
//
STATUS
GetEthernetIP(
    DhcpInfo *pDhcp,
    int *pEthIP
    )
{
    STATUS Status = DHCP_SUCCESS;

    // Verify that caller owns lock
    ASSERT( pDhcp->Lock.OwnerThread == (HANDLE )GetCurrentThreadId() );

    //
    //    For a soft lease we want the IP transaction to be done using
    //    the physical LAN adapter's IP address, not the soft lease address.
    //
    ASSERT(pEthIP);
    if (pEthIP) {
        *pEthIP = pDhcp->IPAddr;
        if (pDhcp->ClientIDType == DHCP_CLIENT_ID_TYPE_SOFT) {
            // ClientIDType of 0 is a 'soft' lease, so look for a 'real'
            // Ethernet adapter ( ClientIDType of 1 ) to use to talk to
            // the DHCP server.
            Status = DHCP_NOINTERFACE;

            CTEGetLock(&v_GlobalListLock, 0);
            for (pDhcp = (DhcpInfo *)v_EstablishedList.Flink;
                (DWORD)pDhcp != (DWORD)&v_EstablishedList;
                pDhcp = (DhcpInfo *)pDhcp->DhcpList.Flink)
            {
                if (pDhcp->ClientIDType == DHCP_CLIENT_ID_TYPE_ETHERNET && pDhcp->IPAddr && pDhcp->T1) {
                    // Found a LAN adapter with a lease, use its IP Address
                    *pEthIP = pDhcp->IPAddr;
                    Status = DHCP_SUCCESS;
                    break;
                }
            }
            CTEFreeLock(&v_GlobalListLock, 0);
            
            if (DHCP_NOINTERFACE == Status) {
                DEBUGMSG(ZONE_ERROR, (TEXT("DHCP:GetEthernetIP: ERROR - Unable to find adapter with DHCP lease\n")));
            }
        }
    }
    
    return Status;
}   // GetEthernetIP()


//
// Process T1 and T2 events
//
void
ProcessT1orT2(
    DhcpInfo *pDhcp
    )
{
    STATUS      Status = DHCP_SUCCESS;
    DhcpPkt     Pkt;
    int         cPkt, EthIP;
    FILETIME    CurTime;
    int         fStatus;
    BOOL        bStartTimer = FALSE;

    DEBUGMSG(ZONE_TIMER, (TEXT("+DHCP:ProcessT1orT2: pDhcp 0x%X\r\n"), pDhcp));

    CTEGetLock(&pDhcp->Lock, 0);
    pDhcp->SFlags &= ~DHCPSTATE_TIMER_SCHEDULED;
    if (pDhcp->SFlags & DHCPSTATE_T1_EXPIRE) {
        pDhcp->SFlags |= DHCPSTATE_T2_EXPIRE;
    } else {
        pDhcp->SFlags |= DHCPSTATE_T1_EXPIRE;
    }

    Status = GetEthernetIP(pDhcp, &EthIP);
    if ((DHCP_SUCCESS == Status) &&
        (DHCP_SUCCESS == (Status = DhcpInitSock(pDhcp, EthIP)))) {

        BuildDhcpPkt(pDhcp, &Pkt, DHCPREQUEST, (NEW_PKT_FL | RENEW_PKT_FL), pDhcp->ReqOptions, &cPkt);

        fStatus = SetDHCPNTE(pDhcp);
        GetCurrentFT(&CurTime);

        DEBUGMSG(ZONE_TIMER, (TEXT("DHCP:ProcessT1orT2: T%c: CurTime: %x %x\r\n"),
            (pDhcp->SFlags & DHCPSTATE_T2_EXPIRE) ? '2' : '1',
            CurTime.dwHighDateTime, CurTime.dwLowDateTime));

        if (fStatus) {
            Status = SendDhcpPkt(pDhcp, &Pkt, cPkt, DHCPACK, ONCE_FL);
            if (DHCP_DELETED == Status) {
                goto pt_exit;
            }
        } else
            Status = DHCP_NOCARD;

        if (Status == DHCP_SUCCESS) {
            pDhcp->SFlags &= ~(DHCPSTATE_T1_EXPIRE|DHCPSTATE_T2_EXPIRE);
            memcpy(&pDhcp->LeaseObtained, &CurTime, sizeof(CurTime));
            SetDhcpConfig(pDhcp);
            StartT1Timer(pDhcp);

        } else if (Status == DHCP_NACK) {
            // Take Net Down !!!
            DEBUGMSG(ZONE_WARN, 
                (TEXT("DHCP:ProcessT1orT2: Take Net Down\r\n")));
            TakeNetDown(pDhcp, DISCARD_IPADDR);
            ClearDHCPNTE(pDhcp);
            pDhcp->SFlags &= ~(DHCPSTATE_T1_EXPIRE|DHCPSTATE_T2_EXPIRE);
            CloseDhcpSocket(pDhcp);

            StartDhcpDialogThread(NETUI_GETNETSTR_LEASE_EXPIRED);
            PutInWorkerQ(pDhcp, DHCP_EVENT_OBTAIN_LEASE);
            goto pt_exit;

        } else {
            bStartTimer = TRUE;
        }
        if (fStatus)
            ClearDHCPNTE(pDhcp);
        CloseDhcpSocket(pDhcp);

    } else {    // if (DHCP_SUCCESS == DhcpInitSock)
        DEBUGMSG(ZONE_WARN, (TEXT("DHCP:ProcessT1orT2: DhcpInitSock failed %d!!\r\n"), Status));
        bStartTimer = TRUE;
    }

    if (bStartTimer) {
        if (pDhcp->SFlags & DHCPSTATE_T2_EXPIRE) {
            StartMidwayTimer(pDhcp, pDhcp->Lease, (CTEEventRtn)ProcessT1orT2, (CTEEventRtn)ProcessExpire);
        } else {
            StartMidwayTimer(pDhcp, pDhcp->T2, (CTEEventRtn)ProcessT1orT2, (CTEEventRtn)ProcessT1orT2);
        }
    }

pt_exit:
    CTEFreeLock(&pDhcp->Lock, 0);
    DerefDhcpInfo(pDhcp);   // reference from when this timer was started by StartMidwayTimer

    DEBUGMSG(ZONE_TIMER, (TEXT("-DHCP:ProcessT1orT2: pDhcp 0x%X\r\n"), pDhcp));
}    // ProcessT1orT2()


//
// PutNetUp - Cause TCP/IP to use DHCP negotiated IP address for this net.
// The TCP/IP stack will call our ARPResult function if there is an IP address conflict.
// ARPResult sets the ARPEvent to indicate it was called.
//
// Return: DHCP_* error code from comm\inc\dhcp.h
//
STATUS
PutNetUp(DhcpInfo *pDhcp)
{
    int cDNS = 0;
    int cWINS = 0;
    int SetAddrResult;
    int i;

    STATUS DhcpResult = DHCP_SUCCESS;

    for (i = 0; i < MAX_RESOLVER; i++) {
        if (pDhcp->DNS[i]) {
            cDNS++;
        }
        if (pDhcp->WinsServer[i]) {
            cWINS++;
        }
    }

    //
    //  Add the resolvers early so when NetBios is notifed later on on 
    //  LanaUp(), it will already have the WINS server..
    //

    ASSERT(!(DHCPSTATE_AFD_INTF_UP_FL & pDhcp->SFlags));
    pDhcp->SFlags |= DHCPSTATE_AFD_INTF_UP_FL;
    if ((*pfnAfdAddInterface)(pDhcp->Name,
                              pDhcp->Nte,
                              pDhcp->NteContext,
                              0,
                              pDhcp->IPAddr,
                              pDhcp->SubnetMask,
                              cDNS,
                              pDhcp->DNS,
                              cWINS,
                              pDhcp->WinsServer))
    {
        pDhcp->ARPResult = ERROR_SUCCESS;   // assume no IP address collision
        ResetEvent(pDhcp->ARPEvent);
        SetAddrResult = pDhcp->pProtocolContext->pfnSetAddr(
            (ushort)pDhcp->NteContext, NULL, pDhcp->IPAddr, pDhcp->SubnetMask, 
            pDhcp->Gateway);
    } else {
        pDhcp->SFlags &= ~DHCPSTATE_AFD_INTF_UP_FL;
        SetAddrResult = IP_GENERAL_FAILURE;
    }

    if (SetAddrResult == IP_PENDING)
    {
        CTEFreeLock(&pDhcp->Lock, 0);
        WaitForSingleObject(pDhcp->ARPEvent, ARP_RESPONSE_WAIT);
        CTEGetLock(&pDhcp->Lock, 0);

        if (ERROR_SUCCESS != pDhcp->ARPResult)
        {
            DEBUGMSG(ZONE_WARN, 
                (L"DHCP:PutNetUp - ARPResult == %d, assuming IP addr collision\n", pDhcp->ARPResult));
            DhcpResult = DHCP_COLLISION;
        }
    }
    else if (SetAddrResult != IP_SUCCESS)
    {
        DEBUGMSG(ZONE_WARN, 
            (L"DHCP:PutNetUp - IPSetNTEAddr failed, Result=%d!\n", SetAddrResult));
        
        if (SetAddrResult == IP_DUPLICATE_ADDRESS)
        {
            DhcpResult = DHCP_COLLISION;
        }
        else
        {
            DhcpResult = DHCP_FAILURE;
        }
    }

    if (DhcpResult != DHCP_SUCCESS) {
        //        
        //  Undo the AddInterface done earlier..
        //
        if (pDhcp->SFlags & DHCPSTATE_AFD_INTF_UP_FL) {
            pDhcp->SFlags &= ~DHCPSTATE_AFD_INTF_UP_FL;
            (*pfnAfdAddInterface)(pDhcp->Name,
                                  pDhcp->Nte,
                                  pDhcp->NteContext,
                                  ADD_INTF_DELETE_FL,
                                  pDhcp->IPAddr,
                                  pDhcp->SubnetMask, 
                                  0,
                                  NULL,
                                  0,
                                  NULL
                                  );
        }
    }

    return DhcpResult;

}    // PutNetUp()


STATUS DhcpSendDown(DhcpInfo *pDhcp, DhcpPkt *pPkt, int cPkt, int DestIP);


void DhcpDecline(DhcpInfo * pDhcp, DhcpPkt *pPkt) {
    int cPkt;

    BuildDhcpPkt(pDhcp, pPkt, DHCPDECLINE, RIP_PKT_FL|NEW_PKT_FL, NULL, &cPkt);
    DhcpSendDown(pDhcp, pPkt, cPkt, INADDR_BROADCAST);
    TakeNetDown(pDhcp, DISCARD_IPADDR);
}


BOOL CanUseCachedLease(DhcpInfo * pDhcp) {
    FILETIME BeginTime, EndTime;

    // we only try to use our old lease if we're not in T2 stage!
    ComputeLeaseRelativeFILETIME(pDhcp, pDhcp->T2, &EndTime);

    GetCurrentFT(&BeginTime);
    if (CompareFileTime(&EndTime, &BeginTime) >= 0) {
        ASSERT(0 == (DHCPSTATE_T2_EXPIRE & pDhcp->SFlags));
        return TRUE;
    }

    return FALSE;
}   // CanUseCachedLease


//
// Zero out the IP address and inform the stack
//
void
ZapIPAddr(
    DhcpInfo *pDhcp
    )
{
    if (pDhcp->IPAddr) {
        pDhcp->IPAddr = 0;
        pDhcp->pProtocolContext->pfnSetAddr((ushort)pDhcp->NteContext, NULL, 0, 0, 0);
    }
}


STATUS GetDhcpLease(DhcpInfo *pDhcp) {
    STATUS      Status = DHCP_SUCCESS;
    DhcpPkt     Pkt;
    int         cPkt, fDiscover, Flags, fDone, fFastAutoIP;
    FILETIME    BeginTime;
    uint        cRetry, cDelay;

    DEBUGMSG(ZONE_INIT, (TEXT("+DHCP:GetDhcpLease: pDhcp 0x%X/0x%X (%s)\n"),
            pDhcp, pDhcp->Socket, pDhcp->Name));

    cRetry = fDone = 0;
    pDhcp->SFlags &= ~(DHCPSTATE_T1_EXPIRE | DHCPSTATE_T2_EXPIRE | 
        DHCPSTATE_LEASE_EXPIRE | DHCPSTATE_SAW_DHCP_SRV);
        
    GetDhcpConfig(pDhcp);

    if (pDhcp->Flags & DHCPCFG_DHCP_ENABLED_FL) {

        // If the IP address is currently auto cfg and the user has requested "ipconfig /renew"
        // then stop the 5 minute discover timer and force a discover to happen 
        if (pDhcp->SFlags & DHCPSTATE_AUTO_CFG_IP) {
            StopDhcpTimers(pDhcp);
            pDhcp->SFlags &= ~DHCPSTATE_AUTO_CFG_IP;
            ZapIPAddr(pDhcp);
        }

        fFastAutoIP = ((pDhcp->Flags & (DHCPCFG_FAST_AUTO_IP_FL|DHCPCFG_AUTO_IP_ENABLED_FL)) == (DHCPCFG_FAST_AUTO_IP_FL|DHCPCFG_AUTO_IP_ENABLED_FL));
        //
        // Do a DISCOVER if there is no IP address or if the request options have changed.
        //
        fDiscover = ((0 == pDhcp->IPAddr) || (pDhcp->Flags & DHCPCFG_OPTION_CHANGE_FL));
        if (fDiscover) {
            ZapIPAddr(pDhcp);
            pDhcp->Flags &= ~DHCPCFG_OPTION_CHANGE_FL;
        }

        for ( ; ! fDone && cRetry <= pDhcp->cMaxRetry; cRetry++) {
            
            if (fFastAutoIP) {
                fFastAutoIP = FALSE;
                goto gdl_fast_autoip;
            }

            if (pDhcp->InitDelay) {
                cDelay = (GetTickCount() % pDhcp->InitDelay);
                Sleep(cDelay);
            }
            
            if (SetDHCPNTE(pDhcp)) {
                Status = DHCP_SUCCESS;
                GetCurrentFT(&BeginTime);

                fDiscover = ((0 == pDhcp->IPAddr) || (pDhcp->Flags & DHCPCFG_OPTION_CHANGE_FL));
                if (fDiscover) {
                    BuildDhcpPkt(pDhcp, &Pkt, DHCPDISCOVER, (NEW_PKT_FL), pDhcp->ReqOptions, &cPkt);
                    Status = SendDhcpPkt(pDhcp, &Pkt, cPkt, DHCPOFFER, BCAST_FL | DFT_LOOP_FL);
                }

                if (DHCP_SUCCESS == Status) {
                    Flags = RIP_PKT_FL;    // request IP addr must be filled in
                    if (fDiscover) {
                        Flags |= SID_PKT_FL;
                        fDiscover = FALSE;
                    } else {
                        Flags |= NEW_PKT_FL;
                    }
                    BuildDhcpPkt(pDhcp, &Pkt, DHCPREQUEST, Flags, pDhcp->ReqOptions, &cPkt);
                    Status = SendDhcpPkt(pDhcp, &Pkt, cPkt, DHCPACK, BCAST_FL | DFT_LOOP_FL);
                }

                if (DHCP_DELETED == Status) {
                    // bail out
                    goto Exit;
                }

                ClearDHCPNTE(pDhcp);

                //
                // Perform auto-configuration of a private net IP address only when:
                // 1. We've exhausted our discovery retries
                // 2. Auto IP is enabled
                // 3. We're still in the discovery phase
                // 4. No DHCP server responses were received during discovery
                //
                // Note: There is a feature that allows a wireless interface to skip the discovery
                //      phase which is controlled by the registry value,"DhcpEnableImmediateAutoIP",
                //      hence the "gdl_fast_autoip" label below.
                //
                if ( (cRetry == pDhcp->cMaxRetry) &&
                     (pDhcp->Flags & DHCPCFG_AUTO_IP_ENABLED_FL) &&
                     (fDiscover) &&
                     (!(pDhcp->SFlags & DHCPSTATE_SAW_DHCP_SRV)) )
                {
                    // No response from a DHCP server, so try AutoIP.
                    DEBUGMSG(ZONE_AUTOIP, (TEXT("DHCP:GetDhcpLease - No response from DHCP server, trying AutoIP\n")));
gdl_fast_autoip:
                    Status = AutoIP(pDhcp);
                }

                if (DHCP_SUCCESS == Status) {
                    memcpy(&pDhcp->LeaseObtained, &BeginTime, sizeof(BeginTime));
                    SetDhcpConfig(pDhcp);
                    fDone = TRUE;

                    // We need to PutNetUp() here only when we've negotiated a lease with a DHCP server.
                    // If we didn't auto configure and didn't see a DHCP server, then AutoIP() was able to
                    // ping the default gateway so it decided to use the cached lease already.
                    if ((!(pDhcp->SFlags & DHCPSTATE_AUTO_CFG_IP)) &&
                        (pDhcp->SFlags & DHCPSTATE_SAW_DHCP_SRV)) {
                        Status = PutNetUp(pDhcp); 
                        switch (Status) {
                        case DHCP_SUCCESS:
                            StartT1Timer(pDhcp);
                            break;

                        case DHCP_COLLISION:
                            SetDHCPNTE(pDhcp);
                            DhcpDecline(pDhcp, &Pkt);
                            ClearDHCPNTE(pDhcp);
                            // fall through

                        default:
                            fDone = FALSE;
                            break;
                        }
                    }

                } else if (DHCP_NACK == Status) {
                    pDhcp->SubnetMask = 0;
                    ZapIPAddr(pDhcp);
                    fDiscover = TRUE;

                } else if (!fDiscover) {

                    // Didn't get a NAK. See if we can use our old lease
                    Status = CouldPingGateway(pDhcp);
                    if (DHCP_SUCCESS == Status) {
                        DEBUGMSG(ZONE_WARN, 
                            (TEXT("DHCP:GetDhcpLease: using cached DHCP lease\r\n")));

                        // CouldPingGW puts up NETUI_GETNETSTR_CACHED_LEASE

                        // even if we're in T1 stage this should work
                        // just sets up a timer to call ProcessT1orT2 right
                        // away, ProcessT1orT2 should then do the right thing
                        StartT1Timer(pDhcp);
                        fDone = TRUE;
                    } else if (DHCP_COLLISION == Status) {

                        SetDHCPNTE(pDhcp);
                        DhcpDecline(pDhcp, &Pkt);
                        ClearDHCPNTE(pDhcp);
                        fDone = FALSE;

                    } else {
                        // reasons for failure:
                        // 1. we're in T2 stage don't use old addr
                        // 2. PutNetUp failed possible collision
                        // 3. no gateway

                        ZapIPAddr(pDhcp);
                        fDone = FALSE;
                    }
                } else {    // else if (!fDiscover)
                    DEBUGMSG(ZONE_WARN, (TEXT("DHCP:GetDhcpLease(%s): failed badly!\n"), pDhcp->Name));
                    // shall we try again?
                    ZapIPAddr(pDhcp);

                    if (Status == DHCP_MEDIA_DISC) {
                        fDone = TRUE;
                    }
                }
            } else {    // if (SetDHCPNTE)
                Status = DHCP_NOCARD;
                // if user asks for INFINITE retries,
                // force exit if adapter disappears 
                if ((pDhcp->cMaxRetry == 0xffffffff) &&
                    (pDhcp->SFlags & DHCPSTATE_DELETE))
                {
                    DEBUGMSG( ZONE_WARN, 
                        (TEXT("DHCP:GetDhcpLease: adapter state delete, exit\r\n")));
                    goto Exit;
                }
                else
                {
                    DEBUGMSG(ZONE_WARN, 
                        (TEXT("DHCP:GetDhcpLease: adapter disappeared\r\n")));
                }
            }

            if ( (!fDone) &&
                 (DHCP_NOCARD != Status) &&
                 (cRetry == pDhcp->cRetryDialogue)) {
                if (0 == (pDhcp->SFlags & DHCPSTATE_MEDIA_DISC_PENDING)) {
                    StartDhcpDialogThread(NETUI_GETNETSTR_NO_IPADDR);
                }
            }
        }    // for(...)
    }    // if (pDhcp->Flags & DHCPCFG_DHCP_ENABLED_FL)

Exit:
    DEBUGMSG(ZONE_INIT, (TEXT("-DHCP:GetDhcpLease: pDhcp 0x%X (%s) Ret: %d\n"), pDhcp, pDhcp->Name, Status));
    return Status;
}    // GetDhcpLease()


//
// DHCP_EVENT_DEL_INTERFACE
//
void
EventDelDhcpInterface(
    DhcpEvent * pEvent
    )
{
    DhcpInfo * pDhcp = pEvent->pDhcp;

    // we probably don't need to clear, but since we now have a flag
    // to not clear if we don't have it set it should be safer
    ClearDHCPNTE(pDhcp);
    DeleteDhcpInfo(pDhcp);
}

STATUS
DelDhcpInterface(
    PTSTR pAdapter,
    void *Nte,
    unsigned Context
    )
{
    STATUS   Status;
    DhcpInfo *pDhcp;

    DEBUGMSG(ZONE_REQUEST, (TEXT("DHCP:DelDhcpInterface: Adapter %s, Context %d Nte %X\r\n"), pAdapter, Context, Nte));

    if (pDhcp = FindLockRefDhcp(NULL, pAdapter)) {
        ASSERT(!(pDhcp->SFlags & DHCPSTATE_DELETE));
        if (!(pDhcp->SFlags & DHCPSTATE_DELETE)) {
            pDhcp->SFlags |= DHCPSTATE_DELETE;
        }

        TakeNetDown(pDhcp, DISCARD_IPADDR);

        PutInWorkerQ(pDhcp, DHCP_EVENT_DEL_INTERFACE);
        CTEFreeLock(&pDhcp->Lock, 0);
        DerefDhcpInfo(pDhcp);
        Status = DHCP_SUCCESS;
    } else {
        Status = DHCP_NOINTERFACE;
    }
    return Status;
}    // DelDhcpInterface()


//
// DHCP_EVENT_RENEW_LEASE
//
void
EventRenewLease(
    DhcpEvent * pEvent
    )
{
    DhcpInfo    *pDhcp = pEvent->pDhcp;
    STATUS      Status = DHCP_SUCCESS;
    int         fNewSock = 0;
    DhcpPkt     Pkt;
    int         cPkt;
    FILETIME    CurTime;
    int         EthIP;
    
    DEBUGMSG(ZONE_REQUEST, (TEXT("+DHCP:EventRenewLease: Adapter %s, Context %d Nte %X\r\n"), 
                pDhcp->Name, pDhcp->NteContext, pDhcp->Nte));

    if (pDhcp->SFlags & (DHCPSTATE_WLAN_NOT_AUTH | DHCPSTATE_MEDIA_DISC)) {
        //
        // Don't attempt DHCP negotiation on a disconnected WiFi interface.
        //
        DEBUGMSG(ZONE_REQUEST, (TEXT("-DHCP:EventRenewLease: Adapter %s disconnected\r\n"), pDhcp->Name));
        return;
    }
    
    if (pDhcp->SFlags & DHCPSTATE_LEASE_EXPIRE) {
        ASSERT(0);
        // if this is the case, we shouldn't be on the established list!
        Status = DHCP_FAILURE;
    } else if (pDhcp->SFlags & DHCPSTATE_AUTO_CFG_IP) {
        TakeNetDown(pDhcp, DISCARD_IPADDR);
        EventObtainLease(pEvent);
    } else {
        
        if (! pDhcp->Socket) {
            if (DHCP_SUCCESS == (Status = GetEthernetIP(pDhcp, &EthIP))) {
                //
                // Don't use our old auto IP address for this.
                //
                if (EthIP == pDhcp->AutoIPAddr) {
                    EthIP = 0;
                }

                if (DHCP_SUCCESS == (Status = DhcpInitSock(pDhcp, EthIP))) {
                    fNewSock++;
                }
            }

            if ((DHCP_SUCCESS == Status) && (0 == EthIP)) {
                //
                // Release has reset the IP address, so start from DISCOVER.
                //
                TakeNetDown(pDhcp, DISCARD_IPADDR);
                EventObtainLease(pEvent);
                DEBUGMSG(ZONE_REQUEST, (TEXT("-DHCP:EventRenewLease: Adapter %s, Context %d Nte %X Status %X.\r\n"), 
                            pDhcp->Name, pDhcp->NteContext, pDhcp->Nte, Status));
                return;
            }
        }
        
        if (DHCP_SUCCESS == Status) {
            
            BuildDhcpPkt(pDhcp, &Pkt, DHCPREQUEST, (NEW_PKT_FL | RENEW_PKT_FL), pDhcp->ReqOptions, &cPkt);
            
            GetCurrentFT(&CurTime);
            
            Status = SendDhcpPkt(pDhcp, &Pkt, cPkt, DHCPACK, 3);
            
            switch (Status) {
            case DHCP_SUCCESS:
                StopDhcpTimers(pDhcp);
                memcpy(&pDhcp->LeaseObtained, &CurTime, sizeof(CurTime));
                SetDhcpConfig(pDhcp);
                // this should set the FTimer based on the new settings.
                StartT1Timer(pDhcp);
                break;

            case DHCP_NACK:
                // Take Net Down !!!
                DEBUGMSG(ZONE_WARN, (TEXT("DHCP:EventRenewLease - DHCP_NACK: Take Net Down\r\n")));
                StopDhcpTimers(pDhcp);
                TakeNetDown(pDhcp, DISCARD_IPADDR);
                ClearDHCPNTE(pDhcp);
                CloseDhcpSocket(pDhcp);
                StartDhcpDialogThread(NETUI_GETNETSTR_LEASE_EXPIRED);
                PutInWorkerQ(pDhcp, DHCP_EVENT_OBTAIN_LEASE);
                break;

            case DHCP_FAILURE:
                TakeNetDown(pDhcp, RETAIN_IPADDR);
                PutInWorkerQ(pDhcp, DHCP_EVENT_OBTAIN_LEASE);
                break;
            }

            if (fNewSock) {
                CloseDhcpSocket(pDhcp);
            }
        }    // DHCP_SUCCESS == Status
    }

    DEBUGMSG(ZONE_REQUEST, (TEXT("-DHCP:EventRenewLease: Adapter %s, Context %d Nte %X Status %X\r\n"), 
                pDhcp->Name, pDhcp->NteContext, pDhcp->Nte, Status));
}    // EventRenewLease()


STATUS
RenewDHCPAddr(
    PDHCP_PROTOCOL_CONTEXT  pProtocolContext,
    PTSTR                   pAdapter,
    void *                  Nte,
    unsigned                Context,
    char *                  pAddr, 
    unsigned                cAddr
    )
{
    STATUS      Status = DHCP_SUCCESS;
    DhcpInfo    *pDhcp;

    DEBUGMSG(ZONE_REQUEST, (TEXT("+DHCP:RenewDHCPAddr: Adapter %s, Context %d Nte %X\r\n"), 
            pAdapter, Context, Nte));

    if (pDhcp = FindLockRefDhcp(NULL, pAdapter)) {
        PutInWorkerQEx(
            pDhcp,
            DHCP_EVENT_RENEW_LEASE,
            pProtocolContext,
            Nte,
            Context,
            pAddr,
            cAddr
            );

        CTEFreeLock(&pDhcp->Lock, 0);
        DerefDhcpInfo(pDhcp);
    } else {
        DEBUGMSG(ZONE_ERROR, (TEXT("DHCP:RenewDHCPAddr: couldn't find Interface %s\r\n"), pAdapter));
        Status = DHCP_NOINTERFACE;
    }

    return Status;

}    // RenewDHCPAddr()

void
SetClientID(
    DhcpInfo *  pDhcp,
    char *      ClientID, 
    unsigned    ClientIDLen
    )
{
    BYTE        *ClientHWAddr;
    unsigned    ClientHWAddrLen;
    
    ASSERT(!ClientID || ClientIDLen <= CHADDR_LEN);

    //
    //    It would be nice if the external interface allowed the
    //    caller to pass in separate hardware address and client
    //    identifiers.  Since it does not, we do an extraction
    //    of the hardware address from the client ID for PPP.
    //
    ClientHWAddr = ClientID;
    ClientHWAddrLen = ClientIDLen;
    if (ClientHWAddr && ClientHWAddrLen == CHADDR_LEN) {
        // The Client ID for PPP is of the form:
        // "RAS " + 0x0000 + <6 octet MAC address> + <4 byte index>
        ClientHWAddr += 4 + 2; // Skip "RAS " and 0x0000
        ClientHWAddrLen = ETH_ADDR_LEN;
    }

    if (ClientHWAddr) {
        pDhcp->ClientHWAddrLen = min(CHADDR_LEN, ClientHWAddrLen);
        memcpy(pDhcp->ClientHWAddr, ClientHWAddr, pDhcp->ClientHWAddrLen);
        pDhcp->ClientHWType = 1;
        pDhcp->ClientIDLen = min(CHADDR_LEN, ClientIDLen);
        memcpy(pDhcp->ClientID, ClientID, pDhcp->ClientIDLen);
        pDhcp->ClientIDType = (ClientIDLen == ETH_ADDR_LEN) ? DHCP_CLIENT_ID_TYPE_ETHERNET : DHCP_CLIENT_ID_TYPE_SOFT;
    }
}


STATUS RequestWLan(
    PDHCP_PROTOCOL_CONTEXT  pProtocolContext,
    PTSTR                   pAdapter, 
    void                    *Nte, 
    unsigned                Context, 
    char                    *ClientID, 
    unsigned                ClientIDLen
    )
{
    
    DhcpInfo    *pDhcp;
    STATUS      Status = DHCP_SUCCESS;

    DEBUGMSG(ZONE_REQUEST, 
        (TEXT("DHCP:RequestWLan: Adapter %s, Context %d Nte %X\r\n"), 
        pAdapter, Context, Nte));

    if (pDhcp = NewDhcpInfo(pProtocolContext, pAdapter)) {
        RefDhcpInfo(pDhcp);
        pDhcp->SFlags |= (DHCPSTATE_WLAN_INTF|DHCPSTATE_WLAN_NOT_AUTH);
        pDhcp->Nte = Nte;
        pDhcp->NteContext = Context;
        SetClientID(pDhcp, ClientID, ClientIDLen);

        // Force DHCP enabled on soft (RAS) addresses so we don't have
        // to set up registry entries for the virtual RAS adapters...

        if (pDhcp->ClientIDType == DHCP_CLIENT_ID_TYPE_SOFT) {
            pDhcp->Flags |= DHCPCFG_DHCP_ENABLED_FL;
        }

        CTEFreeLock(&pDhcp->Lock, 0);
        DerefDhcpInfo(pDhcp);
    } else
        Status = DHCP_NOMEM;

    DEBUGMSG(ZONE_WARN, (TEXT("-DHCP:RequestWLan: Context %d\r\n"),
        Context));

    return Status;
    
}    // RequestWLan()


STATUS
RequestDHCPAddr(
    PDHCP_PROTOCOL_CONTEXT  pProtocolContext,
    PTSTR                   pAdapter, 
    void                    *Nte, 
    unsigned                Context, 
    char                    *ClientID, 
    unsigned                ClientIDLen
    )
//
//    Function:
//        Obtain a lease on an IP address from a DHCP server
//
//  Parameters:
//
//        pProtocolContext:    DHCP protocol context obtained via call to DHCPRegister
//        pAdapter:            Name that DHCP will assign internally to the lease (name must be unique on this machine)
//        Nte:
//        Context:            Value opaque to DHCP, passed back to protocol on SetNTE and SetNTEAddr callbacks for this lease
//        ClientID:            Unique (on subnet) ID ( e.g. Ethernet MAC address) to be used to obtain a leased IP address from server
//        ClientIDLen:        Length in bytes of ClientID
//
//    Return values:
//        DHCP_SUCCESS: request issued
//        DHCP_NOMEM:    out of memory
//
{
    DhcpInfo    *pDhcp;
    STATUS      Status = DHCP_SUCCESS;

    DEBUGMSG(ZONE_REQUEST, 
        (TEXT("DHCP:RequestDHCPAddr: Adapter %s, Context %d Nte %X\r\n"), 
        pAdapter, Context, Nte));

    if (pDhcp = NewDhcpInfo(pProtocolContext, pAdapter)) {
        RefDhcpInfo(pDhcp);
        pDhcp->Nte = Nte;
        pDhcp->NteContext = Context;
        SetClientID(pDhcp, ClientID, ClientIDLen);

        //
        // Force DHCP enabled on soft (RAS) addresses so we don't have
        // to set up registry entries for the virtual RAS adapters...
        //
        if (pDhcp->ClientIDType == DHCP_CLIENT_ID_TYPE_SOFT) {
            pDhcp->Flags |= DHCPCFG_DHCP_ENABLED_FL;
        }

        PutInWorkerQ(pDhcp, DHCP_EVENT_OBTAIN_LEASE);

        CTEFreeLock(&pDhcp->Lock, 0);
        DerefDhcpInfo(pDhcp);

    }
    else
        Status = DHCP_NOMEM;

    DEBUGMSG(ZONE_WARN, (TEXT("-DHCP:RequestDHCPAddr: Context %d\r\n"), Context));

    return Status;
}    // RequestDHCPAddr()


//
// DHCP_EVENT_RELEASE_LEASE
//
void
EventReleaseLease(
    DhcpEvent * pEvent
    )
{
    DhcpInfo    *pDhcp = pEvent->pDhcp;
    int         cPkt, EthIP;
    STATUS      Status;
    DhcpPkt     Pkt;

    DEBUGMSG(ZONE_REQUEST, (TEXT("+DHCP:EventReleaseLease: Adapter %s\r\n"), pDhcp->Name));

    if (DHCP_SUCCESS == (Status = GetEthernetIP(pDhcp, &EthIP))) {
        // Open the socket
        if (DHCP_SUCCESS == (Status = DhcpInitSock(pDhcp, EthIP))) {
            // Build and transmit the Dhcp Release packet
            BuildDhcpPkt(pDhcp, &Pkt, DHCPRELEASE, NEW_PKT_FL, NULL, &cPkt);
            //
            //    Note that there is no ACK packet for a DHCPRELEASE, so there is
            //    no way to know for certain if a release is successful.
            //
            Status = DhcpSendDown(pDhcp, &Pkt, cPkt, pDhcp->DhcpServer);
            
            // Inform TCP/IP that the IP address is no longer valid,
            TakeNetDown(pDhcp, DISCARD_IPADDR);
            ClearDHCPNTE(pDhcp);
            CloseDhcpSocket(pDhcp);
        }
    }

    DEBUGMSG(ZONE_REQUEST, (TEXT("-DHCP:EventReleaseLease: Adapter %s, Status %X\r\n"), pDhcp->Name, Status));
}   // EventReleaseLease()


STATUS
ReleaseDHCPAddr(
    PTSTR pAdapter
    )
{
    DEBUGMSG(ZONE_REQUEST, (TEXT("DHCP:ReleaseDHCPAddr: Adapter %s\r\n"), pAdapter));
    return QueueEventByAdapterName(pAdapter, DHCP_EVENT_RELEASE_LEASE);
}   // ReleaseDHCPAddr()


//
// DHCP_EVENT_OBTAIN_LEASE
//
void
EventObtainLease(
    DhcpEvent * pEvent
    )
{
    DhcpInfo *  pDhcp = pEvent->pDhcp;
    STATUS      Status;
    UINT        Size;

    ASSERT(0 == (DHCPSTATE_DELETE & pDhcp->SFlags));

    if (NULL == pDhcp->Socket) {
        Status = DhcpInitSock(pDhcp, 0);
    } else {
        Status = DHCP_SUCCESS;
    }

    // now try to get an address for him
    if (DHCP_SUCCESS == Status) {

        if (! pDhcp->Nte) {
            Size = pDhcp->ClientIDLen;
            pDhcp->pProtocolContext->pfnSetNTE(pDhcp->NteContext, 
                &pDhcp->Nte, pDhcp->ClientID, &Size);
        }

        Status = GetDhcpLease(pDhcp);
        if (DHCP_DELETED == Status) {
            return;
        } else {
            CloseDhcpSocket(pDhcp);
        }

        if (DHCPSTATE_DELETE & pDhcp->SFlags) {
            return;
        }

        if (DHCP_SUCCESS != Status) {
            DEBUGMSG(ZONE_WARN, 
                (TEXT("DHCP:EventObtainLease - couldn't get Addr for pDhcp 0x%X, Context 0x%X\r\n"),
                pDhcp, pDhcp->NteContext));

            StopDhcpTimers(pDhcp);
        }
    } else {
        StopDhcpTimers(pDhcp);
        if (0 == (pDhcp->SFlags & DHCPSTATE_MEDIA_DISC_PENDING)) {
            StartDhcpDialogThread(NETUI_GETNETSTR_NO_IPADDR);
        }
    }
}


//
// DHCP_EVENT_MEDIA_CONNECT
//
void
EventMediaConnect(
    DhcpEvent * pEvent
    )
{
    DhcpInfo *  pDhcp = pEvent->pDhcp;
    STATUS      Status = DHCP_SUCCESS;
    BOOL        bNeedCloseSocket = FALSE;
    DhcpPkt     Pkt;
    int         cPkt;
    FILETIME    CurTime;
    int         fStatus;
    BOOL        AlreadyConnected;
    int         i;

    DEBUGMSG(ZONE_MEDIA_SENSE, 
        (TEXT("+DHCP:EventMediaConnect: pDhcp %s\r\n"), pDhcp->Name));

    if (pDhcp->SFlags & (DHCPSTATE_WLAN_NOT_AUTH | DHCPSTATE_MEDIA_DISC)) {
        //
        //  Make sure adapter is not in disconnected state..
        //  This may happen since IP can request us to add
        //  media disconnected adapter as WLAN adapter..
        //  And if we receive MEDIA_DISCONNECT after that, 
        //  the DHCPSTATE_MEDIA_DISC will be set causing the 
        //  send handler to not send any DHCP packet.
        //
        
        pDhcp->SFlags &= ~(DHCPSTATE_WLAN_NOT_AUTH | DHCPSTATE_MEDIA_DISC);
        
        DEBUGMSG (ZONE_MEDIA_SENSE, (TEXT("DHCP:EventMediaConnect: Inserting adapter to worker queue.\r\n")));
        
        EventObtainLease(pEvent);

    } else {
    
        if (pDhcp->SFlags & DHCPSTATE_MEDIA_DISC) {
            pDhcp->SFlags &= ~DHCPSTATE_MEDIA_DISC;
             AlreadyConnected = FALSE;
        } else {
            DEBUGMSG(ZONE_MEDIA_SENSE, (TEXT("DHCP:EventMediaConnect: Adapter %s not disconnected\r\n"), pDhcp->Name));
            AlreadyConnected = TRUE;
        }
        
        if (pDhcp->SFlags & DHCPSTATE_LEASE_EXPIRE) {
            DEBUGMSG(ZONE_MEDIA_SENSE, (TEXT("-DHCP:EventMediaConnect: pDhcp %s Lease has expired!\r\n"), pDhcp->Name));
            return;
        } else if (pDhcp->SFlags & DHCPSTATE_AUTO_CFG_IP) {
            EventObtainLease(pEvent);
        } else {
            if (AlreadyConnected && v_ReUseDhcpInfoWhileAPRomaing) {
                //
                // This MEDIA_CONNECT event is most likely due to roaming.
                // If we are on the same subnet, then we can skip the DHCP negotiation.
                // If we can ping the gateway device, then we are still on the same subnet.
                //

                //
                // For the Symbol/Target setup, MEDIA_CONNECT comes in before the Wifi
                // connection has been authenticated. All sent packets are dropped while
                // it is not authenticated, so we'll ping a few times.
                //
                Sleep(200);
                for (i = 0; i < 3; i++) {
                    if (PingDhcp(pDhcp->Gateway)) {
                        DEBUGMSG(ZONE_MEDIA_SENSE, (TEXT("DHCP:EventMediaConnect: Adapter %s could ping gateway. Skipping DHCP netgotiation\r\n"), pDhcp->Name));
                        goto Exit;
                    }
                    Sleep(1000);
                }
            }

            bNeedCloseSocket = TRUE;
            if (NULL == pDhcp->Socket) {
                Status = DhcpInitSock(pDhcp, 0);
            } else {
                Status = DHCP_SUCCESS;
            }
            
            if (DHCP_SUCCESS == Status) {
                
                BuildDhcpPkt(pDhcp, &Pkt, DHCPREQUEST, (NEW_PKT_FL), pDhcp->ReqOptions, &cPkt);
                
                fStatus = SetDHCPNTE(pDhcp);
                GetCurrentFT(&CurTime);
                
                if (fStatus) {
                    Status = SendDhcpPkt(pDhcp, &Pkt, cPkt, DHCPACK, (3 | BCAST_FL));
                } else {
                    Status = DHCP_NOCARD;
                }
                
                if (DHCP_DELETED == Status) {
                    goto Exit;
                }
                
                ClearDHCPNTE(pDhcp);
                
                if (Status == DHCP_SUCCESS) {
                    // if we're already up don't arp again, just update the time
                    
                    if (DHCPSTATE_AFD_INTF_UP_FL & pDhcp->SFlags) {
                        StopDhcpTimers(pDhcp);
                        memcpy(&pDhcp->LeaseObtained, &CurTime, sizeof(CurTime));
                        SetDhcpConfig(pDhcp);
                        
                        // this should set the FTimer based on the new settings.
                        StartT1Timer(pDhcp);
                    } else {
                        Status = PutNetUp(pDhcp);
                        if (DHCP_SUCCESS == Status) {
                            StopDhcpTimers(pDhcp);
                            memcpy(&pDhcp->LeaseObtained, &CurTime, sizeof(CurTime));
                            SetDhcpConfig(pDhcp);
                            
                            // this should set the FTimer based on the new settings.
                            StartT1Timer(pDhcp);
                        } else {
                            ASSERT(DHCP_COLLISION == Status);
                            SetDHCPNTE(pDhcp);
                            DhcpDecline(pDhcp, &Pkt);
                            ClearDHCPNTE(pDhcp);
                        }
                    }
                
                    DEBUGMSG(ZONE_WARN | ZONE_MEDIA_SENSE, (TEXT("DHCP:EventMediaConnect: Successful renewal\r\n")));
                
                } else if (Status == DHCP_NACK) {
                    // Take Net Down !!!
                    DEBUGMSG(ZONE_WARN | ZONE_MEDIA_SENSE, (TEXT("DHCP:EventMediaConnect: NACK - Take Net Down\r\n")));
                    StopDhcpTimers(pDhcp);
                    TakeNetDown(pDhcp, DISCARD_IPADDR);
                    ClearDHCPNTE(pDhcp);
    
                    PutInWorkerQ(pDhcp, DHCP_EVENT_OBTAIN_LEASE);
                }    // Status == DHCP_NACK
            }    // else    DHCPSTATE_LEASE_EXPIRE
        }
    }

Exit:
    if (bNeedCloseSocket) {
        CloseDhcpSocket(pDhcp);
    }    // DHCP_SUCCESS == Status

    DEBUGMSG(ZONE_MEDIA_SENSE, 
        (TEXT("-DHCP:EventMediaConnect: pDhcp %s Status %X\r\n"), pDhcp->Name, Status));

}    // EventMediaConnect()

STATUS
HandleMediaConnect(
    unsigned Context,
    PTSTR pAdapter
    )
{
    DEBUGMSG(ZONE_MEDIA_SENSE, (TEXT("+DHCP:HandleMediaConnect: pDhcp %s\r\n"), pAdapter));
    return QueueEventByAdapterName(pAdapter, DHCP_EVENT_MEDIA_CONNECT);
}    // HandleMediaConnect()


//
// DHCP_EVENT_MEDIA_DISCONNECT
//
void
EventMediaDisconnect(
    DhcpEvent * pEvent
    )
{
    HKEY hKey;
    DhcpInfo * pDhcp = pEvent->pDhcp;
    DEBUGMSG(ZONE_MEDIA_SENSE, (TEXT("+DHCP:EventMediaDisconnect: pDhcp %s\r\n"), pDhcp->Name));

    //
    //  With ResetOnResume(), IP indicates media disconnect
    //  when reset-end received.
    //  Hence we can get here when we are already disconnected.
    //
    pDhcp->SFlags &= ~DHCPSTATE_MEDIA_DISC_PENDING;

    if (!(pDhcp->SFlags & DHCPSTATE_MEDIA_DISC)) {
        pDhcp->SFlags |= DHCPSTATE_MEDIA_DISC;

        // don't stop the timer, so we can track lease expirations...
        // CTEStopFTimer(&pDhcp->Timer);
        TakeNetDown(pDhcp, RETAIN_IPADDR);
        ClearDHCPNTE(pDhcp);
        CloseDhcpSocket(pDhcp);
        
        if (pDhcp->SFlags & DHCPSTATE_WLAN_INTF) {
            //
            // Delete the gateway address so that when we switch to a different network
            // we don't put up the cached IP address when we attempt to ping the previous
            // gateway address.
            //
            hKey = OpenDHCPKey(pDhcp);
            if (hKey) {
                pDhcp->Gateway = 0;
                RegDeleteValue(hKey, TEXT("DhcpDefaultGateway"));
                RegCloseKey (hKey);
            }
        }
    }            
    DEBUGMSG(ZONE_MEDIA_SENSE, (TEXT("-DHCP:EventMediaDisconnect: pDhcp %s\r\n"), pDhcp->Name));
}    // EventMediaDisconnect()


STATUS
HandleMediaDisconnect(
    unsigned Context,
    PTSTR    pAdapter
    )
{
    DEBUGMSG(ZONE_MEDIA_SENSE, (TEXT("DHCP:HandleMediaDisconnect: pDhcp %s\r\n"), pAdapter));
    return QueueEventByAdapterName(pAdapter, DHCP_EVENT_MEDIA_DISCONNECT);
}    // HandleMediaDisconnect()

#ifdef DEBUG
LPWSTR
GetDhcpEventName(
    DWORD EventCode
    )
{
    switch (EventCode) {
    case DHCP_EVENT_OBTAIN_LEASE:       return L"DHCP_EVENT_OBTAIN_LEASE";
    case DHCP_EVENT_RELEASE_LEASE:      return L"DHCP_EVENT_RELEASE_LEASE";
    case DHCP_EVENT_RENEW_LEASE:        return L"DHCP_EVENT_RENEW_LEASE";
    case DHCP_EVENT_MEDIA_CONNECT:      return L"DHCP_EVENT_MEDIA_CONNECT";
    case DHCP_EVENT_MEDIA_DISCONNECT:   return L"DHCP_EVENT_MEDIA_DISCONNECT";
    case DHCP_EVENT_DEL_INTERFACE:      return L"DHCP_EVENT_DEL_INTERFACE";
    }
    return L"Unknown Event Code!!!!";
}
#endif

//
// CTE event function to perform DHCP protocol
//
void
DhcpEventWorker(
    CTEEvent *  Event,
    void *      Context
    )
{
    DhcpInfo    *pDhcp;
    DhcpEvent   *pEvent;

    while (1) {
        //
        // Find an interface to work on
        //
        CTEGetLock(&v_GlobalListLock, 0);

        for (pDhcp = (DhcpInfo *)v_EstablishedList.Flink;
             TRUE;
             pDhcp = (DhcpInfo *)pDhcp->DhcpList.Flink)
        {
            if (pDhcp == (DhcpInfo *)&v_EstablishedList) {
                //
                // End of list, no matching entry found
                //
                pDhcp = NULL;
                v_fEventRunning = FALSE;
                break;
            }

            if (!IsListEmpty(&pDhcp->EventList)) {
                RefDhcpInfo(pDhcp);
                break;
            }
        }    // for()

        CTEFreeLock(&v_GlobalListLock, 0);

        if (NULL == pDhcp) {
            //
            // No more Dhcp interfaces with queued events
            //
            break;
        }

        //
        // Process the events for this interface
        //
        CTEGetLock(&pDhcp->Lock, 0);

        while (!IsListEmpty(&pDhcp->EventList)) {
            pEvent = (DhcpEvent *)RemoveHeadList(&pDhcp->EventList);
            DEBUGMSG(ZONE_MISC, (L"DHCP:DhcpEventWorker: Processing %s for %s\n",
                                 GetDhcpEventName(pEvent->Code), pDhcp->Name));

            switch (pEvent->Code) {
            case DHCP_EVENT_OBTAIN_LEASE:
                EventObtainLease(pEvent);
                break;

            case DHCP_EVENT_RELEASE_LEASE:
                EventReleaseLease(pEvent);
                break;

            case DHCP_EVENT_RENEW_LEASE:
                EventRenewLease(pEvent);
                break;

            case DHCP_EVENT_MEDIA_CONNECT:
                EventMediaConnect(pEvent);
                break;

            case DHCP_EVENT_MEDIA_DISCONNECT:
                EventMediaDisconnect(pEvent);
                break;

            case DHCP_EVENT_DEL_INTERFACE:
                EventDelDhcpInterface(pEvent);
                break;
            }

            LocalFree(pEvent);
        }
        
        CTEFreeLock(&pDhcp->Lock, 0);
        DerefDhcpInfo(pDhcp);
    }
}    // DhcpEventWorker()


STATUS DhcpNotify(
    uint                    Code,
    PDHCP_PROTOCOL_CONTEXT  pProtocolContext,
    PTSTR                   pAdapter,
    void                    *Nte,
    unsigned                Context, 
    char                    *pAddr,
    unsigned                cAddr
    )
{
    STATUS    Status;


    DEBUGMSG(ZONE_NOTIFY, 
        (TEXT("DHCP:DhcpNotify: Adapter %s, Code %d, Context %d Nte %X\r\n"), 
        pAdapter, Code, Context, Nte));

    switch(Code) {
    case DHCP_NOTIFY_DEL_INTERFACE:
        Status = DelDhcpInterface(pAdapter, Nte, Context);
        break;

    case DHCP_REQUEST_ADDRESS:
        Status = RequestDHCPAddr(pProtocolContext, pAdapter, Nte, Context, pAddr, cAddr);
        break;

    case DHCP_REQUEST_WLAN:
        Status = RequestWLan(pProtocolContext, pAdapter, Nte, Context, pAddr, cAddr);
        break;

    case DHCP_REQUEST_RELEASE:
        Status = ReleaseDHCPAddr(pAdapter);
        break;

    case DHCP_REQUEST_RENEW:
        Status = RenewDHCPAddr(pProtocolContext, pAdapter, Nte, Context, pAddr, cAddr);
        break;

    // the following are unhandled cases no one should be calling them
    case DHCP_NOTIFY_ADD_INTERFACE:
    default:
        ASSERT(0);
        Status = DHCP_FAILURE;
        break;
    }

    ASSERT(v_GlobalListLock.OwnerThread != (HANDLE)GetCurrentThreadId());

    return Status;

}    // DhcpNotify()


BOOL v_fCachedLeaseDialogUp;
BOOL v_fLeaseExpiredDialogUp;
BOOL v_fNoIPAddrDialogUp;

DWORD
DhcpDialogThread(
    LPVOID Context
    )
{
    DWORD dwMsg = (DWORD)Context;


    switch (dwMsg) {
    case NETUI_GETNETSTR_CACHED_LEASE:
        CallKNetMsgBox(NULL, NMB_FL_OK, NETUI_GETNETSTR_CACHED_LEASE);
        v_fCachedLeaseDialogUp = FALSE;
        return 0;

    case NETUI_GETNETSTR_LEASE_EXPIRED:
        CallKNetMsgBox(NULL, NMB_FL_EXCLAIM | NMB_FL_OK | NMB_FL_TOPMOST, NETUI_GETNETSTR_LEASE_EXPIRED);
        v_fLeaseExpiredDialogUp = FALSE;
        return 0;

    case NETUI_GETNETSTR_NO_IPADDR:
        CallKNetMsgBox(NULL, NMB_FL_EXCLAIM | NMB_FL_OK | NMB_FL_TOPMOST, NETUI_GETNETSTR_NO_IPADDR);
        v_fNoIPAddrDialogUp = FALSE;
        return 0;

    default:
        ASSERT(0);
        return 1;
    }
    return 0;
}

void StartDhcpDialogThread(DWORD dwMsg)
{
    HANDLE hThd;

    //
    // Currently the DHCP dialog is not parameterized per adapter, so just one system-wide instance
    // for each message will suffice.
    //
    switch (dwMsg) {
    case NETUI_GETNETSTR_CACHED_LEASE:
        if (v_fCachedLeaseDialogUp) {
            return;
        }
        v_fCachedLeaseDialogUp = TRUE;
        break;

    case NETUI_GETNETSTR_LEASE_EXPIRED:
        if (v_fLeaseExpiredDialogUp) {
            return;
        }
        v_fLeaseExpiredDialogUp = TRUE;
        break;

    case NETUI_GETNETSTR_NO_IPADDR:
        if (v_fNoIPAddrDialogUp) {
            return;
        }
        v_fNoIPAddrDialogUp = TRUE;
        break;

    default:
        ASSERT(0);
        return;
    }

    hThd = CreateThread(NULL, 0, DhcpDialogThread, (LPVOID)dwMsg, 0, NULL);
    if (hThd) {
        CloseHandle(hThd);
    } else {
        RETAILMSG(1, (L"DHCP:StartDhcpDialogThread - CreateThread failed!\n"));
        switch (dwMsg) {
        case NETUI_GETNETSTR_CACHED_LEASE:
            v_fCachedLeaseDialogUp = FALSE;
            break;

        case NETUI_GETNETSTR_LEASE_EXPIRED:
            v_fLeaseExpiredDialogUp = FALSE;
            break;

        case NETUI_GETNETSTR_NO_IPADDR:
            v_fNoIPAddrDialogUp = FALSE;
            break;
        }
    }
}

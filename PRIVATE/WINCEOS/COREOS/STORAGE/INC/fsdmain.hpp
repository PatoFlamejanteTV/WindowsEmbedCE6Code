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

// Exported Storage Manager APIs.
EXTERN_C LRESULT STOREMGR_Initialize ();

#ifdef UNDER_CE
EXTERN_C LRESULT STOREMGR_StartBootPhase (DWORD BootPhase);
EXTERN_C void STOREMGR_NotifyFileSystems (DWORD Flags);
EXTERN_C void STOREMGR_ProcNotify (DWORD Flags, HPROCESS hProc, HTHREAD hThread);
EXTERN_C BOOL STOREMGR_RegisterFileSystemFunction (SHELLFILECHANGEFUNC_t pFn);
EXTERN_C BOOL STOREMGR_GetOidInfoEx (int MountIndex, CEOIDINFOEX* pOidInfoEx);
#endif


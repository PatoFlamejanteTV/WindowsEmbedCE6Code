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
//+----------------------------------------------------------------------------
//
//
// File:
//      StatusMap.h
//
// Contents:
//
//      Declaration of functions mapping HTTP status code into HRESULT
//
//-----------------------------------------------------------------------------

#ifndef __STATUSMAP_H_INCLUDED__
#define __STATUSMAP_H_INCLUDED__

struct HttpMapEntry
{
    DWORD   http;
    HRESULT hr;
};

struct HttpMap
{
    DWORD           elc;
    HttpMapEntry   *elv;
    HRESULT         dflt;
};

extern HttpMap g_HttpStatusCodeMap[];

HRESULT HttpStatusToHresult(DWORD dwStatus);
HRESULT HttpContentTypeToHresult(LPCSTR contentType);
HRESULT HttpContentTypeToHresult(LPCWSTR contentType);

#endif //__STATUSMAP_H_INCLUDED__


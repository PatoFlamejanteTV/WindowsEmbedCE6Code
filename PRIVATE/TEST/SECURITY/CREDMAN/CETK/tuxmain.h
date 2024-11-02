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
// --------------------------------------------------------------------
//                                                                     
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF 
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO 
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A      
// PARTICULAR PURPOSE.                                                 
//                                                                     
// --------------------------------------------------------------------
#ifndef __TUXMAIN_H__
#define __TUXMAIN_H__

#include "globals.h"

// Test function prototypes (TestProc's)
TESTPROCAPI ListCredProvPrimitiveTest             (UINT, TPPARAM, LPFUNCTION_TABLE_ENTRY);
TESTPROCAPI ListCredProvVirtualTest             (UINT, TPPARAM, LPFUNCTION_TABLE_ENTRY);

TESTPROCAPI CreateReadUpdateDeleteTest             (UINT, TPPARAM, LPFUNCTION_TABLE_ENTRY);

// Tux testproc function table
FUNCTION_TABLE_ENTRY g_lpFTE[] = {

    _T("Credentials Manager Test"                      ),      0,      0,                               0,  NULL,

   _T(" Credentials Providers Tests"                 ),      1,      0,                               0,  NULL,
   _T(" List Credentials Providers for Primitive Types"               ),      2,      PR_ALL,                 PROVR  +  1,  ListCredProvPrimitiveTest,   
   _T(" List Credentials Providers for Virtual Types"               ),      2,      PR_ALL,                 PROVR  +  2,  ListCredProvVirtualTest,   

    _T("  Create, Read, Update and Delete Credentials Tests"                   ),      1,      0,                               0,  NULL,
    _T(" Create, Read, Update and Delete Credentials Test"             ),      2,      CD_ALL,                CANDD +  1,  CreateReadUpdateDeleteTest,

    NULL,   0,  0,  0,  NULL
};

// Internal functions

#endif // __TUXMAIN_H__

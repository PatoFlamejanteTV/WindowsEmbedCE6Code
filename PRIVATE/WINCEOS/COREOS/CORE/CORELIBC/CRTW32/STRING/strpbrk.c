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

#include <stdlib.h>

#if !defined(_M_IX86)
char * strpbrk(const char * string, const char * control) {
    char *cset;
    /* 1st char in control string stops search */
    while (*string) {
        for (cset = (char *) control; *cset; cset++)
            if (*cset == *string)
                return (char *) string;
        string++;
    }
    return NULL;
}
#endif // _M_IX86


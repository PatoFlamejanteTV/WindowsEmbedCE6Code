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
#ifndef __COMMON_H__


#define	ARRAY_SIZE(rg)			((sizeof rg) / (sizeof rg[0]))
#define	RANDOM_INT(max, min)	((INT)((rand() % ((INT)max - (INT)min + 1)) + (INT)min))
#define	RANDOM_BOOL(max, min)	((BOOL)((rand() % ((BOOL)max - (BOOL)min + 1)) + (BOOL)min))
#define	RECT_WIDTH(rc)			(abs((LONG)rc.right - (LONG)rc.left))
#define	RECT_HEIGHT(rc)			(abs((LONG)rc.bottom - (LONG)rc.top))

#define	RANDOM_CHOICE			RANDOM_BOOL(TRUE, FALSE)

#define	LOOPCOUNT_MIN			(0x10)
#define	LOOPCOUNT_MAX			(0x20)
#define	LISTBOX_TOTAL_STRINGS	(0x20)
#define COMBOBOX_TOTAL_STRINGS	(0x20)

#define	EDITBOX_MAX_LINES		(0x20)
#define	EDITBOX_MIN_LINES		(0x10)

#define	DLGITEM_ACTION_WAIT		Sleep(50)
#define	DLGITEM_KEYPOST_WAIT	Sleep(10)
#define	DLGITEM_MOUSECLICK_WAIT	Sleep(10)
#define	THREADPOLL_WAIT			Sleep(250)

#define	THREAD_COMPLETION_DELAY	(8 * 1000) // 8 minute

#define	QA_INIT_DLG				(WM_USER + 1)

#define SCROLLBAR_RANGE_MIN		(0x00)
#define	SCROLLBAR_RANGE_MAX		(0xFF)
#define	SCROLLBAR_SCROLLSTEP	(0x10)
#define	SCROLLBAR_SCROLLPAGE	(0x20)


void FAR PASCAL MessagePump();

#define __COMMON_H__
#endif /* __COMMON_H__ */

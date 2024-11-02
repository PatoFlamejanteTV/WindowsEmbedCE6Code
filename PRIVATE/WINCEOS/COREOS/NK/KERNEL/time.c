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
/*
 *
 * Module Name:
 *
 *        time.c
 *
 * Abstract:
 *
 *        This file implements time/alarm functions
 *
 * Revision History:
 *
 */

#include <kernel.h>

#define WEEKDAY_OF_1601         1

#define FIRST_VALID_YEAR        1601        // first valid year 
#define TICKS_IN_A_DAY          86400000    // # of ticks in a day
#define MS_TO_100NS             10000       // multiplier to convert time in ticks to time in 100ns
#define MINUTES_TO_MILLS        60000       // convert minutes to milliseconds
#define DAYS_IN_400_YEARS       146097      // # of days in 400 years
#define DAYS_IN_100_YEARS       36524       // # of days in 100 years
#define IsLeapYear(Y)           (!((Y)%4) && (((Y)%100) || !((Y)%400)))
#define NumberOfLeapYears(Y)    ((Y)/4 - (Y)/100 + (Y)/400)
#define ElapsedYearsToDays(Y)   ((Y)*365 + NumberOfLeapYears(Y))
#define MaxDaysInMonth(Y,M)     (IsLeapYear(Y) \
            ? (LeapYearDaysBeforeMonth[(M) + 1] - LeapYearDaysBeforeMonth[M]) \
            : (NormalYearDaysBeforeMonth[(M) + 1] - NormalYearDaysBeforeMonth[M]))

#define TZ_BIAS_IN_MILLS        ((LONG) KInfoTable[KINX_TIMEZONEBIAS] * MINUTES_TO_MILLS)

//
// soft RTC related registries
//
// registry key definition
#define REGKEY_PLATFORM         L"Platform"
// registry value names
#define REGVAL_SOFTRTC          L"SoftRTC"

const WORD LeapYearDaysBeforeMonth[13] = {
    0,                                 // January
    31,                                // February
    31+29,                             // March
    31+29+31,                          // April
    31+29+31+30,                       // May
    31+29+31+30+31,                    // June
    31+29+31+30+31+30,                 // July
    31+29+31+30+31+30+31,              // August
    31+29+31+30+31+30+31+31,           // September
    31+29+31+30+31+30+31+31+30,        // October
    31+29+31+30+31+30+31+31+30+31,     // November
    31+29+31+30+31+30+31+31+30+31+30,  // December
    31+29+31+30+31+30+31+31+30+31+30+31};

const WORD NormalYearDaysBeforeMonth[13] = {
    0,                                 // January
    31,                                // February
    31+28,                             // March
    31+28+31,                          // April
    31+28+31+30,                       // May
    31+28+31+30+31,                    // June
    31+28+31+30+31+30,                 // July
    31+28+31+30+31+30+31,              // August
    31+28+31+30+31+30+31+31,           // September
    31+28+31+30+31+30+31+31+30,        // October
    31+28+31+30+31+30+31+31+30+31,     // November
    31+28+31+30+31+30+31+31+30+31+30,  // December
    31+28+31+30+31+30+31+31+30+31+30+31};

const BYTE LeapYearDayToMonth[366] = {
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // January
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,        // February
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // March
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,     // April
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  // May
     5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,     // June
     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,  // July
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,  // August
     8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,     // September
     9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,  // October
    10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,     // November
    11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11}; // December

const BYTE NormalYearDayToMonth[365] = {
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // January
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,           // February
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // March
     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,     // April
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  // May
     5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,     // June
     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,  // July
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,  // August
     8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,     // September
     9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,  // October
    10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,     // November
    11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11}; // December

//
// MAX_TIME_IN_TICKS - calculated with maximum value from SYSTEMTIME structure 
//          (year 0xffff, month 12, day 31, 23:59:59.999)
//
#define MAX_TIME_IN_TICKS   0x00072afd9ef937ff  


//
// soft RTC related
//
static ULONGLONG        gBaseTimeInTicks;
static ULONGLONG        gLastTimeInTickRead;        // to make sure RTC not going backward when sync with hardware
static DWORD            gTickCountAtBase;

static BOOL             gfUseSoftRTC;

//
// Raw Time related
//
#define gOfstRawTime    (g_pKData->i64RawOfst)
static ULONGLONG        ui64PrevRawTime;            // last "raw time" recorded, for rollover detection

//
// general
//
extern CRITICAL_SECTION rtccs;
static DWORD dwYearRollover;


static void UpdateAndSignalAlarm (void);

//
// convert days + milliseconds to milliseconds
//
static ULONGLONG DayAndFractionToTicks (ULONG ElapsedDays, ULONG Milliseconds)
{
    return (ULONGLONG) ElapsedDays * TICKS_IN_A_DAY + Milliseconds;
}

//
// convert millisecons to days + milleseconds
//
static void TicksToDaysAndFraction (ULONGLONG ui64Ticks, LPDWORD lpElapsedDays, LPDWORD lpMilliseconds)
{
    ULONGLONG uiTicksInFullDay;
    *lpElapsedDays   = (DWORD) (ui64Ticks / TICKS_IN_A_DAY);
    uiTicksInFullDay = (ULONGLONG) *lpElapsedDays * TICKS_IN_A_DAY;
    *lpMilliseconds  = (DWORD) (ui64Ticks - uiTicksInFullDay);
}

//
// calculate # of years given the number of days
//
static ULONG ElapsedDaysToYears (ULONG ElapsedDays)
{
    ULONG NumberOf400s;
    ULONG NumberOf100s;
    ULONG NumberOf4s;

    //  A 400 year time block is 146097 days
    NumberOf400s = ElapsedDays / DAYS_IN_400_YEARS;
    ElapsedDays -= NumberOf400s * DAYS_IN_400_YEARS;
    //  A 100 year time block is 36524 days
    //  The computation for the number of 100 year blocks is biased by 3/4 days per
    //  100 years to account for the extra leap day thrown in on the last year
    //  of each 400 year block.
    NumberOf100s = (ElapsedDays * 100 + 75) / 3652425;
    ElapsedDays -= NumberOf100s * 36524;
    //  A 4 year time block is 1461 days
    NumberOf4s = ElapsedDays / 1461;
    ElapsedDays -= NumberOf4s * 1461;
    return (NumberOf400s * 400) + (NumberOf100s * 100) +
           (NumberOf4s * 4) + (ElapsedDays * 100 + 75) / 36525;
}

//
// test if a system time is valid
//
static BOOL IsValidSystemTime(const SYSTEMTIME *lpst) {

    return (lpst->wYear >= FIRST_VALID_YEAR)    // at least year 1601
        && ((lpst->wMonth - 1) < 12)            // month between 1-12
        && (lpst->wDay >= 1)                    // day >=1
        && (lpst->wDay <= MaxDaysInMonth (lpst->wYear,lpst->wMonth-1))   // day < max day of month
        && (lpst->wHour < 24)                   // valid hour
        && (lpst->wMinute < 60)                 // valid minute
        && (lpst->wSecond < 60)                 // valid second
        && (lpst->wMilliseconds < 1000);        // valid milli-second
}

//
// convert time to # of ticks since 1601/1/1, 00:00:00
//
ULONGLONG NKSystemTimeToTicks (const SYSTEMTIME *lpst)
{
    ULONG ElapsedDays;
    ULONG ElapsedMilliseconds;

    ElapsedDays = ElapsedYearsToDays(lpst->wYear - FIRST_VALID_YEAR);
    ElapsedDays += (IsLeapYear(lpst->wYear) ?
        LeapYearDaysBeforeMonth[lpst->wMonth-1] :
        NormalYearDaysBeforeMonth[lpst->wMonth-1]);
    ElapsedDays += lpst->wDay - 1;
    ElapsedMilliseconds = (((lpst->wHour*60) + lpst->wMinute)*60 +
       lpst->wSecond)*1000 + lpst->wMilliseconds;

    return DayAndFractionToTicks (ElapsedDays, ElapsedMilliseconds);
}

//
// convert # of ticks since 1601/1/1, 00:00:00 to time
//
BOOL NKTicksToSystemTime (ULONGLONG ui64Ticks, SYSTEMTIME *lpst)
{
    BOOL fRet = (ui64Ticks <= MAX_TIME_IN_TICKS);

    if (fRet) {
        ULONG Days;
        ULONG Years;
        ULONG Minutes;
        ULONG Seconds;
        ULONG Milliseconds;

        TicksToDaysAndFraction (ui64Ticks, &Days, &Milliseconds);

        lpst->wDayOfWeek = (WORD)((Days + WEEKDAY_OF_1601) % 7);
        Years = ElapsedDaysToYears(Days);
        Days = Days - ElapsedYearsToDays(Years);
        if (IsLeapYear(Years + 1)) {
            lpst->wMonth = (WORD)(LeapYearDayToMonth[Days] + 1);
            Days = Days - LeapYearDaysBeforeMonth[lpst->wMonth-1];
        } else {
            lpst->wMonth = (WORD)(NormalYearDayToMonth[Days] + 1);
            Days = Days - NormalYearDaysBeforeMonth[lpst->wMonth-1];
        }
        Seconds = Milliseconds/1000;
        lpst->wMilliseconds = (WORD)(Milliseconds % 1000);
        Minutes = Seconds / 60;
        lpst->wSecond = (WORD)(Seconds % 60);
        lpst->wHour = (WORD)(Minutes / 60);
        lpst->wMinute = (WORD)(Minutes % 60);
        lpst->wYear = (WORD)(Years + 1601);
        lpst->wDay = (WORD)(Days + 1);
    }
    return fRet;
}


static ULONGLONG GetRTCInTicks (void)
{
    ULONGLONG ui64CurrTime;
    SYSTEMTIME st;
    DEBUGCHK (OwnCS (&rtccs));

    // read RTC
    OEMGetRealTime (&st);

    // takes # of rollover year into account
    st.wYear += (WORD) dwYearRollover;

    ui64CurrTime = NKSystemTimeToTicks (&st);

    if (((ui64CurrTime - gOfstRawTime) < ui64PrevRawTime) 
        && g_pOemGlobal->dwYearsRTCRollover) {
       
        // RTC rollover
        RETAILMSG (1, (L"RTC Rollover detected (year = %d)\r\n", st.wYear));

        dwYearRollover  += g_pOemGlobal->dwYearsRTCRollover;
        st.wYear        += (WORD) g_pOemGlobal->dwYearsRTCRollover; // take rollover into account
        ui64CurrTime     = NKSystemTimeToTicks (&st);

        DEBUGCHK ((ui64CurrTime - gOfstRawTime) >= ui64PrevRawTime);
    }

    // if ui64PrevRawTime is 0, RTC haven't being intialized yet.
    if (ui64PrevRawTime) {
        ui64PrevRawTime = (ui64CurrTime - gOfstRawTime);
    }
    return ui64CurrTime;
}

//
// convert system time to file time
//
BOOL NKSystemTimeToFileTime (const SYSTEMTIME *lpst, LPFILETIME lpft) 
{
    BOOL fRet = IsValidSystemTime(lpst);

    if (fRet) {
        ULARGE_INTEGER ui64_100ns;
        ui64_100ns.QuadPart = NKSystemTimeToTicks (lpst) * MS_TO_100NS;
        lpft->dwHighDateTime = ui64_100ns.HighPart;
        lpft->dwLowDateTime  = ui64_100ns.LowPart;
    } else {
        KSetLastError (pCurThread, ERROR_INVALID_PARAMETER);
    }

    return fRet;
}

//
// convert file time to system time
//
BOOL NKFileTimeToSystemTime (const FILETIME *lpft, LPSYSTEMTIME lpst) 
{
    ULARGE_INTEGER ui64_100ns;
    BOOL           fRet;

    ui64_100ns.HighPart = lpft->dwHighDateTime;
    ui64_100ns.LowPart  = lpft->dwLowDateTime;

    if (!(fRet = NKTicksToSystemTime (ui64_100ns.QuadPart/MS_TO_100NS, lpst))) {
        KSetLastError (pCurThread, ERROR_INVALID_PARAMETER);
    }

    return fRet;
}

//
// get local/system time in ticks from 1601/1/1, 00:00:00
//
static ULONGLONG GetTimeInTicks (DWORD dwType)
{
    ULONGLONG ui64Ticks;
    DEBUGCHK ((TM_LOCALTIME == dwType) || (TM_SYSTEMTIME == dwType));

    EnterCriticalSection (&rtccs);
    if (gfUseSoftRTC) {
        // NOTE: Tick count can wrap. The offset (OEMGetTickCount - gTickCountAtBase) must
        //       be calculated 1st before adding the base tick.
        ui64Ticks = gBaseTimeInTicks + (ULONG) (OEMGetTickCount () - gTickCountAtBase);

        // last time GetxxxTime is called. Should never set gTickCountAtBase to anything smaller than this
        // when refreshing RTC, or RTC can go backward.
        gLastTimeInTickRead = ui64Ticks;
    } else {
        // use a local copy so we won't except while holding RTC CS
        ui64Ticks = GetRTCInTicks ();
    }
    LeaveCriticalSection (&rtccs);
    if (TM_SYSTEMTIME == dwType) {
        // apply time-zone bias
        ui64Ticks += TZ_BIAS_IN_MILLS;
    }

    return ui64Ticks;
}

//
// set local time in ticks
//      pui64Delta returns the time changed.
//
static BOOL SetLocalTimeInTicks (const SYSTEMTIME* pst, ULONGLONG ui64Tick, ULONGLONG *pui64Delta)
{
    BOOL      fRet;
    ULONGLONG ui64TickCurr;
    SYSTEMTIME st = *pst;

    DEBUGCHK (OwnCS (&rtccs));

    // get the current RTC
    ui64TickCurr = GetRTCInTicks ();

    // update local time
    fRet = OEMSetRealTime (&st);

    if (fRet) {
        // reset years rollover
        dwYearRollover = 0;
    } else if (g_pOemGlobal->dwYearsRTCRollover) {
        // try the time with rollover
        st.wYear -= (WORD) g_pOemGlobal->dwYearsRTCRollover;
        fRet      = OEMSetRealTime (&st);

        if (fRet) {
            dwYearRollover = g_pOemGlobal->dwYearsRTCRollover;
        }
    }
    
    if (fRet) {
        WORD wYear = st.wYear;
        
        // re-read rtc, in case hardware changed the time we set.
        VERIFY (OEMGetRealTime (&st));
        if (wYear > st.wYear) {
            // got preempted between Set/GetRealTime, and year rollover
            dwYearRollover += g_pOemGlobal->dwYearsRTCRollover;
        }

        // calculate new RTC in ticks and update soft RTC
        st.wYear        += (WORD) dwYearRollover;
        gBaseTimeInTicks = NKSystemTimeToTicks (&st);
        gTickCountAtBase = OEMGetTickCount ();
        gLastTimeInTickRead = 0;    // reset "last time reading RTC", as time had changed

        // calculate/update delta and raw time offset
        *pui64Delta   = gBaseTimeInTicks - ui64TickCurr;
        gOfstRawTime += *pui64Delta;

    }

    return fRet;
}

//
// refresh soft RTC, return the current (local) time in ticks since 1601/1/1, 00:00:00 to time
//
ULONGLONG NKRefreshRTC (void)
{
    EnterCriticalSection (&rtccs);
    gBaseTimeInTicks = GetRTCInTicks();
    
    if (gfUseSoftRTC) {
        gTickCountAtBase = OEMGetTickCount ();
        
        if (gBaseTimeInTicks < gLastTimeInTickRead) {
            // Soft RTC moving slower, catch it up by increasing the gap
            DEBUGCHK(gLastTimeInTickRead - gBaseTimeInTicks < 0xFFFFFFFF);
            gTickCountAtBase -= (DWORD) (gLastTimeInTickRead - gBaseTimeInTicks);
            gLastTimeInTickRead = 0;
        }
    }
    LeaveCriticalSection (&rtccs);
    return gBaseTimeInTicks;
}

//
// external interface to get time
//
BOOL NKGetTime (SYSTEMTIME *pst, DWORD dwType)
{
    ULONGLONG ui64Ticks = GetTimeInTicks (dwType);
    VERIFY (NKTicksToSystemTime (ui64Ticks, pst));

    return TRUE;
}

BOOL NKGetTimeAsFileTime (LPFILETIME pft, DWORD dwType)
{
    ULARGE_INTEGER uliTicks;
    uliTicks.QuadPart = GetTimeInTicks (dwType) * MS_TO_100NS;
    pft->dwHighDateTime = uliTicks.HighPart;
    pft->dwLowDateTime  = uliTicks.LowPart;
    return TRUE;
}



//
// external interface to set time
//
BOOL NKSetTime (const SYSTEMTIME *pst, DWORD dwType, LONGLONG *pi64Delta)
{
    BOOL fRet = IsValidSystemTime (pst);
    LONGLONG  i64Delta = 0;
    
    if (fRet) {
        ULONGLONG ui64Ticks;
        SYSTEMTIME st = *pst;
        st.wMilliseconds = 0;         // system time granularity is second.
        ui64Ticks = NKSystemTimeToTicks (&st);

        DEBUGCHK ((TM_LOCALTIME == dwType) || (TM_SYSTEMTIME == dwType));
        
        EnterCriticalSection (&rtccs);

        if (TM_SYSTEMTIME == dwType) {
            // apply time-zone bias
            ui64Ticks -= TZ_BIAS_IN_MILLS;
        }
        
        fRet = NKTicksToSystemTime (ui64Ticks, &st);
        
        if (fRet) {
            
            fRet = SetLocalTimeInTicks (&st, ui64Ticks, &i64Delta);
            
            if (fRet) {
                // refresh kernel alarm
                UpdateAndSignalAlarm ();
            }

        }
        LeaveCriticalSection (&rtccs);
    }

    if (!fRet) {
        KSetLastError (pCurThread, ERROR_INVALID_PARAMETER);
    } else {
        // update data passed in without holding CS.
        *pi64Delta = i64Delta;
    }

    return fRet;
}

//
// external interface to get raw time
//
BOOL NKGetRawTime (ULONGLONG *pui64Ticks)
{
    ULONGLONG ui64Ticks;
    EnterCriticalSection (&rtccs);
    ui64Ticks = GetRTCInTicks () - gOfstRawTime;
    LeaveCriticalSection (&rtccs);

    *pui64Ticks = ui64Ticks;
    return TRUE;
}

//------------------------------------------------------------------------------
// alarm related code
//------------------------------------------------------------------------------

static HANDLE     hAlarmEvent;
static SYSTEMTIME CurAlarmTime;

static void UpdateAndSignalAlarm (void)
{
    ULONGLONG ui64Ticks;
    SYSTEMTIME st;
    DEBUGCHK (OwnCS (&rtccs));

    ui64Ticks = NKRefreshRTC ();

    if (hAlarmEvent) {

        ULONGLONG ui64CurAlarmTick = NKSystemTimeToTicks (&CurAlarmTime);

        if (ui64CurAlarmTick <= ui64Ticks + g_pOemGlobal->dwAlarmResolution) {
            // within alarm resolution time, fire the alarm
            NKSetEvent (g_pprcNK, hAlarmEvent);
            HNDLCloseHandle (g_pprcNK, hAlarmEvent);
            hAlarmEvent = NULL;
        } else {
            // pass a local copy of the alarm time; otherwise there is potential for oem code
            // to update the CurAlarmTime leading to failures downstream
            memcpy(&st, &CurAlarmTime, sizeof(st));
            OEMSetAlarmTime (&st);
        }
    }
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void
NKRefreshKernelAlarm (void)
{
    DEBUGMSG(ZONE_ENTRY,(L"NKRefreshKernelAlarm entry\r\n"));
    EnterCriticalSection(&rtccs);
    UpdateAndSignalAlarm ();
    LeaveCriticalSection(&rtccs);
    DEBUGMSG(ZONE_ENTRY,(L"NKRefreshKernelAlarm exit\r\n"));
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void
NKSetKernelAlarm(
    HANDLE hAlarm,
    const SYSTEMTIME *lpst
    )
{
    SYSTEMTIME st = *lpst;  // make a local copy so we don't except holding rtc CS
    DEBUGMSG(ZONE_ENTRY,(L"NKSetKernelAlarm entry: %8.8lx %8.8lx\r\n",hAlarm,lpst));
    if (IsValidSystemTime (&st)) {
        st.wMilliseconds = 0;               // alarm resolution no less than 1 second.
        EnterCriticalSection(&rtccs);
        if (hAlarmEvent) {
            HNDLCloseHandle (g_pprcNK, hAlarmEvent);
            hAlarmEvent = NULL;
        }
        if (!HNDLDuplicate (pActvProc, hAlarm, g_pprcNK, &hAlarmEvent)) {
            memcpy (&CurAlarmTime, &st, sizeof(SYSTEMTIME));
            UpdateAndSignalAlarm ();
        }
        LeaveCriticalSection(&rtccs);
    }
    DEBUGMSG(ZONE_ENTRY,(L"NKSetKernelAlarm exit\r\n"));
}

//------------------------------------------------------------------------------
// time-zone/DST related
//------------------------------------------------------------------------------
DWORD NormalBias, DaylightBias, InDaylight;

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void
NKSetDaylightTime(
    DWORD dst,
    BOOL fAutoUpdate
    )
{
    DEBUGMSG(ZONE_ENTRY,(L"NKSetDaylightTime entry: %8.8lx\r\n",dst));
    EnterCriticalSection(&rtccs);
    InDaylight = dst;
    KInfoTable[KINX_TIMEZONEBIAS] = (InDaylight ? DaylightBias : NormalBias);
    LeaveCriticalSection(&rtccs);
    DEBUGMSG(ZONE_ENTRY,(L"NKSetDaylightTime exit\r\n"));
}



//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void
NKSetTimeZoneBias(
    DWORD dwBias,
    DWORD dwDaylightBias
    )
{
    DEBUGMSG(ZONE_ENTRY,(L"NKSetTimeZoneBias entry: %8.8lx %8.8lx\r\n",dwBias,dwDaylightBias));
    EnterCriticalSection(&rtccs);
    NormalBias = dwBias;
    DaylightBias = dwDaylightBias;
    KInfoTable[KINX_TIMEZONEBIAS] = (InDaylight ? DaylightBias : NormalBias);
    LeaveCriticalSection(&rtccs);
    DEBUGMSG(ZONE_ENTRY,(L"NKSetTimeZoneBias exit\r\n"));
}


//------------------------------------------------------------------------------
// initialization
//------------------------------------------------------------------------------

void InitSoftRTC (void)
{
    DWORD type;
    DWORD size = sizeof (DWORD);
    // Query "HKLM\Platform\SoftRTC" to see if we should use SoftRTC or not.
    // If the query fails, gfUseSoftRTC remains 0 and we'll use Hardware RTC.
    DEBUGCHK (SystemAPISets[SH_FILESYS_APIS]);
    NKRegQueryValueExW (HKEY_LOCAL_MACHINE, REGVAL_SOFTRTC,(LPDWORD) REGKEY_PLATFORM,
                &type, (LPBYTE)&gfUseSoftRTC, &size);

#if 0
    if (!g_pOemGlobal->dwYearsRTCRollover) {
        // OEM not specifying rollover year, assume 100 years (unlikely to rollover if rollover year is >= 1000)
        g_pOemGlobal->dwYearsRTCRollover = 100;
    }
#endif
    ui64PrevRawTime = NKRefreshRTC ();
}


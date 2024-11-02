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
// ----------------------------------------------------------------------------
//
// This is the DllMain and ShellProc for all the Tux tests.
//
// ----------------------------------------------------------------------------

#include "TuxMain.hpp"

#include <windows.h>
#include <winsock2.h>
#include <cmdline.h>
#include <cmnprint.h>
#include <logwrap.h>

#include "Factory_t.hpp"
#include "Utils.hpp"

using namespace ce::qa;

// Global Kato logging object: (Set while processing SPM_LOAD_DLL message.)
CKato *g_pKato = NULL;

// Global shell info structure: (Set while processing SPM_SHELL_INFO message.)
SPS_SHELL_INFO *g_pShellInfo = NULL;

// ----------------------------------------------------------------------------
//
// Windows CE specific code:
//
#ifdef UNDER_CE
BOOL WINAPI
DllMain(
    HANDLE hInstance,
    ULONG  dwReason,
    LPVOID lpReserved)
{
   return TRUE;
}
#endif /* ifdef UNDER_CE */

// ----------------------------------------------------------------------------
//
// Forwards debug and error messages from CmnPrint to Kato.
//
static void
ForwardDebug(const TCHAR *pMessage)
{
    if (g_pKato) g_pKato->Log(LOG_COMMENT, TEXT("%s"), pMessage);
}
static void
ForwardWarn(const TCHAR *pMessage)
{
    if (g_pKato) g_pKato->Log(LOG_DETAIL, TEXT("%s"), pMessage);
}
static void
ForwardError(const TCHAR *pMessage)
{
    if (g_pKato) g_pKato->Log(LOG_FAIL, TEXT("%s"), pMessage);
}
static void
ForwardAlways(const TCHAR *pMessage)
{
    if (g_pKato) g_pKato->Log(LOG_DETAIL, TEXT("%s"), pMessage);
}

// ----------------------------------------------------------------------------
//
// Tux ShellProc: Called by tux at various points during test execution.
//
SHELLPROCAPI
ShellProc(
    UINT    uMsg,
    SPPARAM spParam)
{
    DWORD options_flag    = NETLOG_DEBUG_OP;
    DWORD level           = LOG_PASS;
    DWORD use_multithread = FALSE;
    DWORD result;

    WSADATA wsd;

    switch (uMsg)
    {
        // --------------------------------------------------------------------
        // Message: SPM_LOAD_DLL
        //
        // Sent once to the DLL immediately after it is loaded. The spParam
        // parameter will contain a pointer to a SPS_LOAD_DLL structure.
        // The DLL should set the fUnicode member of this structre to TRUE
        // if the DLL is built with the UNICODE flag set. By setting this
        // flag, Tux will ensure that all strings passed to your DLL will
        // be in UNICODE format, and all strings within your function table
        // will be processed by Tux as UNICODE. The DLL may return SPR_FAIL
        // to prevent the DLL from continuing to load.
        //
        case SPM_LOAD_DLL:
        {
            SPS_LOAD_DLL *pLOAD = (SPS_LOAD_DLL *)spParam;

            // If we are UNICODE, then tell Tux this by setting the
            // following flag.
            #ifdef UNICODE
            pLOAD->fUnicode = TRUE;
            #endif
    
            // Get/Create our global logging object.
            g_pKato = (CKato *)KatoGetDefaultObject();
    
            // Initialize logwrapper and CmnPrint.
            NetLogInitWrapper(g_pDllName, level, options_flag, use_multithread);
            CmnPrint_Initialize();
            CmnPrint_RegisterPrintFunction(PT_LOG,     ForwardDebug,  FALSE);
            CmnPrint_RegisterPrintFunction(PT_WARN1,   ForwardWarn,   FALSE);
            CmnPrint_RegisterPrintFunction(PT_WARN2,   ForwardWarn,   FALSE);
            CmnPrint_RegisterPrintFunction(PT_FAIL,    ForwardError,  FALSE);
            CmnPrint_RegisterPrintFunction(PT_VERBOSE, ForwardAlways, FALSE);
    
            // initialize winsock
            int retcode = WSAStartup(MAKEWORD(2,2), &wsd);
            if (retcode != 0)
            {
                LogError(TEXT("Could not initialize Winsock: %d"),
                         WSAGetLastError());
                return SPR_FAIL;
            }
    
            // Initialize the standard utils.
            Utils::StartupInitialize();

            // Generate the test-factory.
            if (NULL == Factory_t::GetInstance())
            {
                LogError(TEXT("Can't allocate test-factory"));
                return SPR_FAIL;
            }
    
            return SPR_HANDLED;
        }
    
        // --------------------------------------------------------------------
        // Message: SPM_UNLOAD_DLL
        //
        // Sent once to the DLL immediately before it is unloaded.
        //
        case SPM_UNLOAD_DLL:
        {
            // Clean up the test-factory and utils.
            Factory_t::DeleteInstance();
            Utils::ShutdownCleanup();
    
            // Uninitialize logwrapper
            NetLogCleanupWrapper();
    
            // Shut down Winsock
            WSACleanup();
    
            return SPR_HANDLED;
        }
    
        // --------------------------------------------------------------------
        // Message: SPM_SHELL_INFO
        //
        // Sent once to the DLL immediately after SPM_LOAD_DLL to give the
        // DLL some useful information about its parent shell and environment.
        // The spParam parameter will contain a pointer to a SPS_SHELL_INFO
        // structure. The pointer to the structure may be stored for later
        // use as it will remain valid for the life of this Tux Dll. The DLL
        // may return SPR_FAIL to prevent the DLL from continuing to load.
        //
        case SPM_SHELL_INFO:
        {
            // Store a pointer to our shell info for later use.
            g_pShellInfo = (SPS_SHELL_INFO *)spParam;
    
            return SPR_HANDLED;
        }
    
        // --------------------------------------------------------------------
        // Message: SPM_REGISTER
        //
        // This is the only ShellProc() message that a DLL is required to
        // handle (except for SPM_LOAD_DLL if you are UNICODE). This message
        // is sent once to the DLL immediately after the SPM_SHELL_INFO
        // message to query the DLL for it's function table. The spParam
        // will contain a pointer to a SPS_REGISTER structure. The DLL
        // should store its function table in the lpFunctionTable member
        // of the SPS_REGISTER structure. The DLL may return SPR_FAIL to
        // prevent the DLL from continuing to load.
        //
        case SPM_REGISTER:
        {
            SPS_REGISTER *pREG = (SPS_REGISTER *)spParam;
            Factory_t *pFactory = Factory_t::GetInstance();

            // Parse the command-line options.
            if (g_pShellInfo->szDllCmdLine
             && g_pShellInfo->szDllCmdLine[0])
            {
                TCHAR szCommandLine[1024];
                WiFUtils::ConvertString(szCommandLine,
                                        g_pShellInfo->szDllCmdLine,
                                COUNTOF(szCommandLine));

                #define MAX_COMMAND_LINE_ARGS 64
                int argc = MAX_COMMAND_LINE_ARGS;
                TCHAR *argv[MAX_COMMAND_LINE_ARGS];
                CommandLineToArgs(szCommandLine, &argc, argv);
    
                result = pFactory->ParseCommandLine(argc, argv);
                if ((ERROR_SUCCESS != result)
                 || (WasOption(argc, argv, TEXT("?")) >= 0)
                 || (WasOption(argc, argv, TEXT("help")) >= 0))
                {
                    LogAlways(TEXT("tux -o -d %s.dll -c [options]"), g_pDllName);
                    LogAlways(TEXT("\nOptions:"));
                    LogAlways(TEXT("  -?    show this information"));
                    pFactory->PrintUsage();
                    return SPR_FAIL;
                }
            }
    
            // Generate the function-table.
            result = pFactory->GetFunctionTable(&(pREG->lpFunctionTable));
            if (ERROR_SUCCESS != result)
                return SPR_FAIL;
    
            #ifdef UNICODE
            return SPR_HANDLED | SPF_UNICODE;
            #else
            return SPR_HANDLED;
            #endif
        }
    
        // --------------------------------------------------------------------
        // Message: SPM_START_SCRIPT
        //
        // Sent to the DLL immediately before a script is started. It is sent
        // to all Tux DLLs, including loaded Tux DLLs that are not in the
        // script. All DLLs will receive this message before the first
        // TestProc() in the script is called.
        //
        case SPM_START_SCRIPT:
        {
            return SPR_HANDLED;
        }
    
        // --------------------------------------------------------------------
        // Message: SPM_STOP_SCRIPT
        //
        // Sent to the DLL when the script has stopped. This message is sent
        // when the script reaches its end, or because the user pressed
        // stopped prior to the end of the script. This message is sent to
        // all Tux DLLs, including loaded Tux DLLs that are not in the script.
        //
        case SPM_STOP_SCRIPT:
        {
            return SPR_HANDLED;
        }
    
        // --------------------------------------------------------------------
        // Message: SPM_BEGIN_GROUP
        //
        // Sent to the DLL before a group of tests from that DLL is about to
        // be executed. This gives the DLL a time to initialize or allocate
        // data for the tests to follow. Only the DLL that is next to run
        // receives this message. The prior DLL, if any, will first receive
        // a SPM_END_GROUP message. For global initialization and
        // de-initialization, the DLL should probably use SPM_START_SCRIPT
        // and SPM_STOP_SCRIPT, or even SPM_LOAD_DLL and SPM_UNLOAD_DLL.
        //
        case SPM_BEGIN_GROUP:
        {
            g_pKato->BeginLevel(0, TEXT("BEGIN GROUP in %s.dll"), g_pDllName);

            result = Factory_t::GetInstance()->OnBeginGroup();
            if (ERROR_SUCCESS != result)
                return SPR_FAIL;

            return SPR_HANDLED;
        }
    
        // --------------------------------------------------------------------
        // Message: SPM_END_GROUP
        //
        // Sent to the DLL after a group of tests from that DLL has completed
        // running. This gives the DLL a time to cleanup after it has been run.
        // This message does not mean that the DLL will not be called again
        // to run tests; it just means that the next test to run belongs to
        // a different DLL. SPM_BEGIN_GROUP and SPM_END_GROUP allow the DLL
        // to track when it is active and when it is not active.
        //
        case SPM_END_GROUP:
        {
            g_pKato->EndLevel(TEXT("END GROUP in %s"), g_pDllName);

            result = Factory_t::GetInstance()->OnEndGroup();
            if (ERROR_SUCCESS != result)
                return SPR_FAIL;

            return SPR_HANDLED;
        }
    
        // --------------------------------------------------------------------
        // Message: SPM_BEGIN_TEST
        //
        // Sent to the DLL immediately before a test executes. This gives
        // the DLL a chance to perform any common action that occurs at the
        // beginning of each test, such as entering a new logging level. The
        // spParam parameter will contain a pointer to a SPS_BEGIN_TEST
        // structure, which contains the function table entry and some
        // other useful information for the next test to execute. If the
        // ShellProc function returns SPR_SKIP, then the test case will
        // not execute.
        //
        case SPM_BEGIN_TEST:
        {
            const SPS_BEGIN_TEST *pBT = (const SPS_BEGIN_TEST *)spParam;
    
            // Start our logging level.
            g_pKato->BeginLevel(pBT->lpFTE->dwUniqueID,
                                TEXT("BEGIN TEST: \"%s\", Threads=%u, Seed=%u"),
                                pBT->lpFTE->lpDescription, pBT->dwThreadCount,
                                pBT->dwRandomSeed);
    
            return SPR_HANDLED;
        }
    
        // --------------------------------------------------------------------
        // Message: SPM_END_TEST
        //
        // Sent to the DLL after a single test executes from the DLL. This
        // gives the DLL a time perform any common action that occurs at the
        // completion of each test, such as exiting the current logging level.
        // The spParam parameter will contain a pointer to a SPS_END_TEST
        // structure, which contains the function table entry and some other
        // useful information for the test that just completed.
        //
        case SPM_END_TEST:
        {
            // End our logging level.
            const SPS_END_TEST *pET = (const SPS_END_TEST *)spParam;
    
            // Give server some time to finish up.
            Sleep(1500);
    
            g_pKato->EndLevel(TEXT("END TEST: \"%s\", %s, Time=%u.%03u"),
                              pET->lpFTE->lpDescription,
                              pET->dwResult == TPR_SKIP ? TEXT("SKIPPED") :
                              pET->dwResult == TPR_PASS ? TEXT("PASSED") :
                              pET->dwResult == TPR_FAIL ? TEXT("FAILED") :
                                                          TEXT("ABORTED"),
                              pET->dwExecutionTime / 1000,
                              pET->dwExecutionTime % 1000);
    
            return SPR_HANDLED;
        }
    
        // --------------------------------------------------------------------
        // Message: SPM_EXCEPTION
        //
        // Sent to the DLL whenever code execution in the DLL causes and
        // exception fault. By default, Tux traps all exceptions that occur
        // while executing code inside a Tux DLL.
        //
        case SPM_EXCEPTION:
        {
            g_pKato->Log(LOG_EXCEPTION, TEXT("Exception occurred!"));
            return SPR_HANDLED;
        }

    }
    return SPR_NOT_HANDLED;
}

// ----------------------------------------------------------------------------

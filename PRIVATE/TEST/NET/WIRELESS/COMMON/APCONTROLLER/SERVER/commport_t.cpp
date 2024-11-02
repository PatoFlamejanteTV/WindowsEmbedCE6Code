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
// Use of this source code is subject to the terms of the Microsoft end-user
// license agreement (EULA) under which you licensed this SOFTWARE PRODUCT.
// If you did not accept the terms of the EULA, you are not authorized to use
// this source code. For a copy of the EULA, please see the LICENSE.RTF on your
// install media.
//
// ----------------------------------------------------------------------------
//
// Implementation of the CommPort_t class.
//
// ----------------------------------------------------------------------------

#include "CommPort_t.hpp"

#include <assert.h>
#include <inc/auto_xxx.hxx>
#include "Utils.hpp"
#include <strsafe.h>

// Default critical-section:
ce::critical_section s_DefaultCSection;

// Define this if you want LOTS of debug output:
//#define EXTRA_DEBUG 1

using namespace ce::qa;

/* ============================== CommHandle =============================== */

// ----------------------------------------------------------------------------
//
// Provides a contention-controlled interface to one of the comm ports.
//
class ce::qa::CommHandle_t
{
private:

    // Port number and name:
    int   m_PortNumber;
    TCHAR m_pPortName[20];

    // Port handle:
    ce::auto_handle m_PortHandle;

    // Configuration structures:
    DCB          m_DCB;
    COMMTIMEOUTS m_Timeouts;
    DCB          m_OldDCB;
    COMMTIMEOUTS m_OldTimeouts;
    
    // Number connected CommPorts:
    int m_NumberConnected;

    // Synchronization object:
    ce::critical_section m_Locker;

    // Pointer to next handle in list:
    CommHandle_t *m_Next;

    // Construction is done by AttachHandle:
    CommHandle_t(int PortNumber);

public:

    // Destructor:
   ~CommHandle_t(void);

    // Attaches or releases the port connection:
    static CommHandle_t *
    AttachHandle(int PortNumber);
    static void
    DetachHandle(const CommHandle_t *pHandle);

    // Retrieves an object which can be locked to prevent other threads
    // from using the port:
    // Callers should lock this object before performing any I/O operations.
    ce::critical_section &
    GetLocker(void) {
        return m_Locker;
    }

    // Connects to the port and configures it for serial communication:
    DWORD
    Open(
        int BaudRate,
        int ByteSize,
        int Parity,
        int StopBits);

    // Closes the port connection:
    DWORD
    Close(void);
    
    // Determines whether the comm-port is open and configured:
    bool
    IsOpened(void) const {
        return m_PortHandle.valid() && INVALID_HANDLE_VALUE != m_PortHandle;
    }
    
    // Reads ASCII from the comm port and inserts it into the specified
    // ASCII or (after translation) Unicode buffer:
    DWORD
    Read(
        ce::string *pBuffer,
        int         MaxCharsToRead,
        int         MaxMillisToWait);
    DWORD
    Read(
        ce::wstring *pBuffer,
        int          MaxCharsToRead,
        int          MaxMillisToWait);

    // If necessary, converts the specified string to ASCII and writes
    // it to the port:
    DWORD
    Write(
        const ce::string &Message,
        int               MaxMillisToWait);
    DWORD
    Write(
        const ce::wstring &Message,
        int                MaxMillisToWait);
};

// ----------------------------------------------------------------------------
//
// Constructor.
//
CommHandle_t::
CommHandle_t(int PortNumber)
    : m_PortNumber(PortNumber),
      m_NumberConnected(1),
      m_Next(NULL)
{
    HRESULT hr = StringCchPrintf(m_pPortName, COUNTOF(m_pPortName),
                                 TEXT("COM%d:"), m_PortNumber);
    assert(SUCCEEDED(hr));
}

// ----------------------------------------------------------------------------
//
// Destructor.
//
CommHandle_t::
~CommHandle_t(void)
{
    // nothing to do
}

// ----------------------------------------------------------------------------
//
// Existing handles:
//

static CommHandle_t        *CommHandles = NULL;
static ce::critical_section CommHandlesLocker;

// ----------------------------------------------------------------------------
//
// Attaches the port connection.
//
CommHandle_t *
CommHandle_t::
AttachHandle(int PortNumber)
{
    ce::gate<ce::critical_section> locker(CommHandlesLocker);

    CommHandle_t *hand = CommHandles;
    for (;;)
    {
        if (NULL == hand)
        {
            hand = new CommHandle_t(PortNumber);
            if (NULL == hand)
            {
                LogError(TEXT("[AC] Can't allocate CommHandle for COM%d"), 
                         PortNumber);
            }
            else
            {
                hand->m_Next = CommHandles;
                CommHandles = hand;
            }
            break;
        }
        if (PortNumber == hand->m_PortNumber)
        {
            hand->m_NumberConnected++;
            break;
        }
        hand = hand->m_Next;
    }

    return hand;
}

// ----------------------------------------------------------------------------
//
// Releases the port connection.
//
void
CommHandle_t::
DetachHandle(const CommHandle_t *pHandle)
{
    ce::gate<ce::critical_section> locker(CommHandlesLocker);

    CommHandle_t **parent = &CommHandles;
    for (;;)
    {
        CommHandle_t *hand = *parent;
        if (NULL == hand)
        {
            assert(NULL == "Tried to detach unknown CommHandle");
            break;
        }
        if (hand == pHandle)
        {
            if (--hand->m_NumberConnected <= 0)
            {
               *parent = hand->m_Next;
                delete hand;
            }
            break;
        }
        parent = &(hand->m_Next);
    }
}

// ----------------------------------------------------------------------------
//
// Connects to the port and configures it for serial communication.
//
DWORD
CommHandle_t::
Open(
    int BaudRate,
    int ByteSize,
    int Parity,
    int StopBits)
{
    DWORD result;
    
    // Only if it's not already opened.
    if (IsOpened())
    {
        return ERROR_SUCCESS;
    }

    LogDebug(TEXT("[AC] Connecting to \"%s\""), m_pPortName);

    // Open the port.
    m_PortHandle = CreateFile(m_pPortName,
                              GENERIC_READ | GENERIC_WRITE,
                              0,    // must be opened with exclusive-access
                              NULL, // no security attributes
                              OPEN_EXISTING, // must use OPEN_EXISTING
                              0,    // not overlapped I/O
                              NULL  // hTemplate must be NULL for comm devices
                              );
                              
    if (!IsOpened())
    {
        result = GetLastError();
        LogError(TEXT("[AC] Can't open %s: %s"), 
                 m_pPortName, Win32ErrorText(result));
        return result;
    }

    // Get the current port configuration settings.
    if (!GetCommState(m_PortHandle, &m_OldDCB))
    {
        result = GetLastError();
        LogError(TEXT("[AC] Can't get comm settings for %s: %s"), 
                 m_pPortName, Win32ErrorText(result));
        m_PortHandle.close();
        return result;
    }
    m_DCB = m_OldDCB;

    if (!GetCommTimeouts(m_PortHandle, &m_OldTimeouts))
    {
        result = GetLastError();
        LogError(TEXT("[AC] Can't get timeouts for %s: %s"),
                 m_pPortName, Win32ErrorText(result));
        m_PortHandle.close();
        return result;
    }
    m_Timeouts = m_OldTimeouts;

    // Customize the configuration settings.
    m_DCB.BaudRate          = BaudRate;           // Current baud 
    m_DCB.fBinary           = TRUE;               // Binary mode; no EOF check 
    m_DCB.fParity           = TRUE;               // Enable parity checking
    m_DCB.fOutxCtsFlow      = FALSE;              // No CTS output flow ctl 
    m_DCB.fOutxDsrFlow      = FALSE;              // No DSR output flow ctl 
    m_DCB.fDtrControl       = DTR_CONTROL_ENABLE; // DTR flow ctl type 
    m_DCB.fDsrSensitivity   = FALSE;              // DSR sensitivity 
    m_DCB.fTXContinueOnXoff = TRUE;               // XOFF continues Tx 
    m_DCB.fOutX             = FALSE;              // No XON/XOFF out flow ctl 
    m_DCB.fInX              = FALSE;              // No XON/XOFF in flow ctl 
    m_DCB.fErrorChar        = FALSE;              // Disable error replacement
    m_DCB.fNull             = FALSE;              // Disable null stripping
    m_DCB.fRtsControl       = RTS_CONTROL_ENABLE; // RTS flow ctl 
    m_DCB.fAbortOnError     = FALSE;              // Don't abort I/O on error
    m_DCB.ByteSize          = ByteSize;           // Number of bits/bytes, 4-8 
    m_DCB.Parity            = Parity;             // 0-4=no,odd,even,mark,space 
    m_DCB.StopBits          = StopBits;           // 0,1,2 = 1, 1.5, 2 
    if (!SetCommState(m_PortHandle, &m_DCB))
    {
        result = GetLastError();                       
        LogError(TEXT("[AC] Can't set comm settings for %s: %s"), 
                 m_pPortName, Win32ErrorText(result));
        m_PortHandle.close();
        return result;
    }

#ifdef EXTRA_DEBUG
    LogDebug(TEXT("[AC] Connected to %s w/ handle 0x%X"), 
             m_pPortName, (HANDLE)m_PortHandle);
#endif

    return ERROR_SUCCESS;
}

// ----------------------------------------------------------------------------
//
// Closes the port connection.
//
DWORD
CommHandle_t::
Close(void)
{
    if (IsOpened())
    {
        // Reset the port configuration.
        if (!SetCommTimeouts(m_PortHandle, &m_OldTimeouts)
         || !SetCommState   (m_PortHandle, &m_OldDCB))
        {
            LogWarn(TEXT("[AC] Can't reset comm settings for %s: %s"), 
                     m_pPortName, Win32ErrorText(GetLastError()));
        }

        // Close the port.
        m_PortHandle.close();
        LogDebug(TEXT("[AC] Disconnected \"%s\""), m_pPortName);
    }
    return ERROR_SUCCESS;
}

// ----------------------------------------------------------------------------
//
// Retrieves the comm port's error status and clears it to enable
// I/O to continue.
//
inline DWORD
ClearPortErrors(
    HANDLE PortHandle)
{
    DWORD errors;
    return ClearCommError(PortHandle, &errors, NULL)? errors : 0;
}

// ----------------------------------------------------------------------------
//
// Reads ASCII from the comm port and inserts it into the specified
// ASCII or (after translation) Unicode buffer.
//
DWORD
CommHandle_t::
Read(
    ce::string *pBuffer,
    int         MaxCharsToRead,
    int         MaxMillisToWait)
{
    DWORD result;

    if (!IsOpened())
        return ERROR_INVALID_HANDLE;

    // Set the comm-timeouts.
    bool updateTimeouts = false;
    if (m_Timeouts.ReadIntervalTimeout != MaxMillisToWait / 2)
    {
        m_Timeouts.ReadIntervalTimeout = MaxMillisToWait / 2;
        updateTimeouts = true;
    }
    if (m_Timeouts.ReadTotalTimeoutMultiplier != 0)
    {
        m_Timeouts.ReadTotalTimeoutMultiplier = 0;
        updateTimeouts = true;
    }
    if (m_Timeouts.ReadTotalTimeoutConstant != MaxMillisToWait)
    {
        m_Timeouts.ReadTotalTimeoutConstant = MaxMillisToWait;
        updateTimeouts = true;
    }
    if (updateTimeouts && !SetCommTimeouts(m_PortHandle, &m_Timeouts))
    {
        result = GetLastError();
        LogError(TEXT("[AC] Can't set read timeouts for %s: %s"),
                 m_pPortName, Win32ErrorText(result));
        return result;
    }
    
    // Read the ASCII data.
    ce::string readBuffer;
    if (!readBuffer.reserve(MaxCharsToRead))
        return ERROR_OUTOFMEMORY;
    DWORD readed;
    if (!ReadFile(m_PortHandle, readBuffer.get_buffer(), 
                                readBuffer.capacity(), &readed, NULL))
    {
        result = GetLastError();
        LogError(TEXT("[AC] Error reading from %s: errcode=0x%X: %s"),
                 m_pPortName,   
                 ClearPortErrors(m_PortHandle),
                 Win32ErrorText(result));
        PurgeComm(m_PortHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return result;
    }

    // Copy the data to the output buffer.
    if (0 == readed)
    {
        pBuffer->clear();
    }
    else
    if (!pBuffer->assign(readBuffer.get_buffer(), readed))
    {
        return ERROR_OUTOFMEMORY;
    }

    return ERROR_SUCCESS;
}

DWORD
CommHandle_t::
Read(
    ce::wstring *pBuffer,
    int          MaxCharsToRead,
    int          MaxMillisToWait)
{
    // Read the ASCII data.
    ce::string mbBuffer;
    DWORD result = Read(&mbBuffer, MaxCharsToRead, MaxMillisToWait);
    if (ERROR_SUCCESS != result)
        return result;

    // Convert to Unicode.
    HRESULT hr = WiFUtils::ConvertString(pBuffer, mbBuffer, NULL,
                                                  mbBuffer.length());
    if (FAILED(hr))
    {
        LogError(TEXT("[AC] Can't convert ASCII to Unicode: %s"),
                 HRESULTErrorText(hr));
        return HRESULT_CODE(hr);
    }           

    return ERROR_SUCCESS;
}

// ----------------------------------------------------------------------------
//
// If necessary, converts the specified string to ASCII and writes
// it to the port.
//
DWORD
CommHandle_t::
Write(
    const ce::string &Message,
    int               MaxMillisToWait)
{
    DWORD result;

    if (!IsOpened())
        return ERROR_INVALID_HANDLE;

#ifdef EXTRA_DEBUG
    LogDebug(TEXT("[AC] writing \"%hs\" (%d bytes) to %s w/ handle 0x%X"),
            &Message[0], Message.length(), 
            m_pPortName, (HANDLE)m_PortHandle);
#endif

    // Set the comm-timeouts.
    bool updateTimeouts = false;
    if (m_Timeouts.WriteTotalTimeoutMultiplier != 0)
    {
        m_Timeouts.WriteTotalTimeoutMultiplier = 0;
        updateTimeouts = true;
    }
    if (m_Timeouts.WriteTotalTimeoutConstant != MaxMillisToWait)
    {
        m_Timeouts.WriteTotalTimeoutConstant = MaxMillisToWait;
        updateTimeouts = true;
    }
    if (updateTimeouts && !SetCommTimeouts(m_PortHandle, &m_Timeouts))
    {
        result = GetLastError();
        LogError(TEXT("[AC] Can't set write timeouts for %s: %s"),
                 m_pPortName, Win32ErrorText(result));
        return result;
    }
    
    // Write the ASCII data.
    ce::string &mutableMessage = const_cast<ce::string &>(Message);
    DWORD written;
    if (!WriteFile(m_PortHandle, mutableMessage.get_buffer(), 
                                 mutableMessage.length(), &written, NULL))
    {
        result = GetLastError();
        LogError(TEXT("[AC] Error writing to %s: errcode=0x%X: %s"),
                 m_pPortName, 
                 ClearPortErrors(m_PortHandle),
                 Win32ErrorText(result));
        PurgeComm(m_PortHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return result;
    }

    // Make sure it was all written.
    if (Message.length() != written)
    {
        LogError(TEXT("[AC] Error writing to %s: only wrote %u of %u bytes"),
                 m_pPortName, written, Message.length());
        PurgeComm(m_PortHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return ERROR_WRITE_FAULT;
    }
    
    return ERROR_SUCCESS;
}

DWORD
CommHandle_t::
Write(
    const ce::wstring &Message,
    int                MaxMillisToWait)
{
    // Convert to ASCII.
    ce::string mbBuffer;
    HRESULT hr = WiFUtils::ConvertString(&mbBuffer, Message, NULL,
                                                    Message.length());
    if (FAILED(hr))
    {
        LogError(TEXT("[AC] Can't convert Unicode to ASCII: %s"),
                 HRESULTErrorText(hr));
        return HRESULT_CODE(hr);
    }

    // Write the ASCII data.
    return Write(mbBuffer, MaxMillisToWait);
}

/* =============================== CommPort ================================ */

// ----------------------------------------------------------------------------
//
// Constructor.
//
CommPort_t::
CommPort_t(int PortNumber)
    : m_pHandle(CommHandle_t::AttachHandle(PortNumber))
{
    assert(NULL != m_pHandle);
}

// ----------------------------------------------------------------------------
//
// Destructor.
//
CommPort_t::
~CommPort_t(void)
{
    if (NULL != m_pHandle)
    {
        Close();
        CommHandle_t::DetachHandle(m_pHandle);
        m_pHandle = NULL;
    }
}

// ----------------------------------------------------------------------------
//
// Retrieves an object which can be locked to prevent other threads
// from using the port.
//
ce::critical_section &
CommPort_t::
GetLocker(void)
{
    return (NULL == m_pHandle)? s_DefaultCSection
                  : m_pHandle->GetLocker();
}

// ----------------------------------------------------------------------------
//
// Connects to the port and configures it for serial communication.
//
DWORD
CommPort_t::
Open(
    int BaudRate,
    int ByteSize,
    int Parity,
    int StopBits)
{
    return (NULL == m_pHandle)? ERROR_OUTOFMEMORY
                  : m_pHandle->Open(BaudRate, ByteSize, Parity, StopBits);
}

// ----------------------------------------------------------------------------
//
// Closes the port connection.
//
DWORD
CommPort_t::
Close(void)
{
    return (NULL == m_pHandle)? ERROR_OUTOFMEMORY 
                  : m_pHandle->Close();
}

// ----------------------------------------------------------------------------
//
// Determines whether the comm port is opened and configured.
//
bool
CommPort_t::
IsOpened(void) const
{
    return (NULL != m_pHandle && m_pHandle->IsOpened());
}

// ----------------------------------------------------------------------------
//
// Reads ASCII from the comm port and inserts it into the specified
// ASCII or (after translation) Unicode buffer.
//
DWORD
CommPort_t::
Read(
    ce::string *pBuffer,
    int         MaxCharsToRead,
    int         MaxMillisToWait)
{
    return (NULL == m_pHandle)? ERROR_OUTOFMEMORY
                  : m_pHandle->Read(pBuffer, MaxCharsToRead, 
                                             MaxMillisToWait);
}
DWORD
CommPort_t::
Read(
    ce::wstring *pBuffer,
    int          MaxCharsToRead,
    int          MaxMillisToWait)
{
    return (NULL == m_pHandle)? ERROR_OUTOFMEMORY
                  : m_pHandle->Read(pBuffer, MaxCharsToRead, 
                                             MaxMillisToWait);
}

// ----------------------------------------------------------------------------
//
// If necessary, converts the specified string to ASCII and writes
// it to the port.
//
DWORD
CommPort_t::
Write(
    const ce::string &Message,
    int               MaxMillisToWait)
{
    return (NULL == m_pHandle)? ERROR_OUTOFMEMORY
                  : m_pHandle->Write(Message, MaxMillisToWait);
}
DWORD
CommPort_t::
Write(
    const ce::wstring &Message,
    int                MaxMillisToWait)
{
    return (NULL == m_pHandle)? ERROR_OUTOFMEMORY
                  : m_pHandle->Write(Message, MaxMillisToWait);
}

// ----------------------------------------------------------------------------

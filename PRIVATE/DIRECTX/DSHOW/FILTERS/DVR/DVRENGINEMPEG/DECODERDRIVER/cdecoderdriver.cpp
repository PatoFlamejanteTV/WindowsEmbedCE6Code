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
#include "stdafx.h"
#include "..\\PipelineInterfaces.h"
#include "DvrInterfaces.h"
#include "..\\DVREngine.h"
#include "CDecoderDriver.h"
#include "..\\ComponentWorkerThread.h"
#include "..\\SampleProducer\\CPinMapping.h"
#include "..\\Plumbing\\PipelineManagers.h"
#include "..\\Plumbing\\Source.h"
#include "..\\HResultError.h"
#include "..\\SampleProducer\\ProducerConsumerUtilities.h"
#include "AVGlitchEvents.h"
#include <dsthread.h>

// To enable Win/CE kernel tracker events that help sort out A/V
// glitches, uncomment the following define -- or otherwise define
// it for all MPEG DVR Engine sources.

#ifdef _WIN32_WCE
// #define WIN_CE_KERNEL_TRACKING_EVENTS
#endif // _WIN32_WCE

#ifdef WIN_CE_KERNEL_TRACKING_EVENTS
#include "celog.h"
#endif // WIN_CE_KERNEL_TRACKING_EVENTS

using namespace MSDvr;

const ULONG CDecoderDriver::g_uRateScaleFactor = 10000L;
const LONGLONG CDecoderDriver::s_hyAllowanceForDownstreamProcessing = 500000;
    // Allow 200 ms for samples to be processed downstream
const LONGLONG CDecoderDriver::s_hyMaxMediaTime = 0x7fffffffffffffff;
const LONGLONG CDecoderDriver::s_hyMinAllowance = 20000;
    // If we lag behind, jump to 20 ms ahead of stream time
const LONGLONG CDecoderDriver::s_hyMaximumWaitForRate = 2000000;
    // We're willing to trade off a lag of 200 ms in changing rate
    // for a smoother transition (flush-less)

const double CDecoderDriver::s_dblFrameToKeyFrameRatio = 3.0;
const double CDecoderDriver::s_dblSecondsToKeyFrameRatio = 10.0;
const REFERENCE_TIME CDecoderDriver::s_rtMaxEarlyDeliveryMargin = 100000000;  // TODO: Tune this!  how far into the future to send a sample
const REFERENCE_TIME CDecoderDriver::s_rtMaxEarlyDeliveryDrift = 1000000;  // TODO: Tune this!  how far into the future to send a sample
const REFERENCE_TIME CDecoderDriver::s_rtMaxLateDeliveryDrift = 1000000;  // TODO:  Tune this!

///////////////////////////////////////////////////////////////////////
//
//  Class CDecoderDriverAppThread -- declaration and implementation
//
///////////////////////////////////////////////////////////////////////
namespace MSDvr {
    class CDecoderDriverAppThread : public CComponentWorkerThread
    {
    public:
        enum DECODER_DRIVER_EVENT_TYPE {
            DECODER_DRIVER_EVENT_SET_RATE,
            DECODER_DRIVER_EVENT_SET_POSITION,
            DECODER_DRIVER_EVENT_TUNE,
            DECODER_DRIVER_EVENT_RUN,
            DECODER_DRIVER_EVENT_ERROR,
            DECODER_DRIVER_EVENT_SET_AUDIO_ENABLE,
            DECODER_DRIVER_EVENT_QUEUE_END_EVENT,
            DECODER_DRIVER_EVENT_QUEUE_POSITION_EVENT,
            DECODER_DRIVER_EVENT_ACTION
        };

        struct SDecoderDriverEvent
        {
            DECODER_DRIVER_EVENT_TYPE eDecoderDriverEventType;
            union {
                double dblRate;
                struct {
                    LONGLONG hyPosition;
                    bool fNoFlushRequested;
                    bool fSkippingTimeHole;
                    bool fSeekToKeyFrame;
                };
                HRESULT hr;
                bool fEnableAudio;
                struct {
                    DVR_ENGINE_EVENTS eDVREngineEvent;
                    REFERENCE_TIME rtEcCompleteStreamTime, rtEcCompleteAVPosition;
                    long iArg1, iArg2;
                };
                CAppThreadAction *pcAppThreadAction;
            };
            CSmartRefPtr<CDecoderDriverNotifyOnPosition> pcDecoderDriverNotifyOnPosition;
        };

        CDecoderDriverAppThread(CDecoderDriver &rcDecoderDriver);

        void SendNewRate(double dblRate);
        void SendNewPosition(LONGLONG hyPosition,
            bool fNoFlushRequested, bool fSkippingTimeHole,
            bool fSeekToKeyFrame);
        void SendTuneEnd(LONGLONG hyChannelStartPos);
        void SendRun();
        void SendGraphConfused(HRESULT hr);
        void SendSetAudioEnable(bool fEnableAudio);
        void SendPendingEndEvent(
                DVR_ENGINE_EVENTS eDVREngineEvent,
                REFERENCE_TIME rtEcCompleteOriginStreamTime,
                REFERENCE_TIME rtEcCompleteLastSamplePosition,
                long iArg1, long iArg2);
        void SendPendingPositionWatch(
                CDecoderDriverNotifyOnPosition *pcDecoderDriverNotifyOnPosition,
                REFERENCE_TIME rtOriginStreamTime);
        void SendAction(CAppThreadAction *pcAppThreadAction);
        void FlushPendingEndEvents();

    protected:
        struct SPendingEndEvent
        {
            DVR_ENGINE_EVENTS eDVREngineEvent;
            REFERENCE_TIME rtAVPositionOfSampleIfKnown;
            REFERENCE_TIME rtStreamTimeWhenSubmitted;
            long iArg1;
            long iArg2;
            REFERENCE_TIME rtAVPositionLastKnown;
            REFERENCE_TIME rtStreamTimeWhenChecked;
        };

        struct SPendingPositionEvent
        {
            CSmartRefPtr<CDecoderDriverNotifyOnPosition> pcDecoderDriverNotifyOnPosition;
            REFERENCE_TIME rtStreamTimeWhenSubmitted;
            REFERENCE_TIME rtAVPositionLastKnown;
            REFERENCE_TIME rtStreamTimeWhenChecked;
        };

        virtual DWORD ThreadMain() ;
        virtual void OnThreadExit(DWORD &dwOutcome);

        ~CDecoderDriverAppThread();

        DWORD ComputeAppThreadSleep();
        void FireEndEvents();
        void QueueEndEvent(DVR_ENGINE_EVENTS eDVREngineEvent,
                            REFERENCE_TIME rtAVPositionOfSampleIfKnown,
                            REFERENCE_TIME rtStreamTimeWhenSubmitted,
                            long iArg1,
                            long iArg2);
        void QueuePositionEvent(
                CDecoderDriverNotifyOnPosition *pcDecoderDriverNotifyOnPosition,
                REFERENCE_TIME rtStreamTimeWhenSubmitted);

        CDecoderDriver &m_rcDecoderDriver;
        std::list<SDecoderDriverEvent> m_listSDecoderDriverEvent;
        HANDLE m_hEventHandle;
        std::list<SPendingEndEvent> m_listSPendingEndEvent;
        std::list<SPendingPositionEvent> m_listSPendingPositionEvent;
        CCritSec m_cCritSecEndEvents;
    };

}

CDecoderDriverAppThread::CDecoderDriverAppThread(CDecoderDriver &rcDecoderDriver)
    : CComponentWorkerThread(NULL)
    , m_rcDecoderDriver(rcDecoderDriver)
    , m_listSDecoderDriverEvent()
    , m_hEventHandle(CreateEvent(NULL, TRUE, FALSE, NULL))
    , m_listSPendingEndEvent()
    , m_listSPendingPositionEvent()
    , m_cCritSecEndEvents()
{
    if (!m_hEventHandle)
    {
        throw CHResultError(GetLastError(), "CreateEvent() failure");
    }
}

CDecoderDriverAppThread::~CDecoderDriverAppThread()
{
    if (m_hEventHandle)
        CloseHandle(m_hEventHandle);

    std::list<SDecoderDriverEvent>::iterator iter;

    for (iter = m_listSDecoderDriverEvent.begin();
         iter != m_listSDecoderDriverEvent.end();
         ++iter)
    {
        if (iter->eDecoderDriverEventType == DECODER_DRIVER_EVENT_ACTION)
        {
            delete iter->pcAppThreadAction;
        }
    }
}

void CDecoderDriverAppThread::SendNewRate(double dblRate)
{
    SDecoderDriverEvent sDecoderDriverEvent;
    sDecoderDriverEvent.eDecoderDriverEventType = DECODER_DRIVER_EVENT_SET_RATE;
    sDecoderDriverEvent.dblRate = dblRate;

    m_listSDecoderDriverEvent.push_back(sDecoderDriverEvent);
    SetEvent(m_hEventHandle);
}

void CDecoderDriverAppThread::SendNewPosition(LONGLONG hyPosition,
    bool fNoFlushRequested, bool fSkippingTimeHole, bool fSeekToKeyFrame)
{
    SDecoderDriverEvent sDecoderDriverEvent;
    sDecoderDriverEvent.eDecoderDriverEventType = DECODER_DRIVER_EVENT_SET_POSITION;
    sDecoderDriverEvent.hyPosition = hyPosition;
    sDecoderDriverEvent.fNoFlushRequested = fNoFlushRequested;
    sDecoderDriverEvent.fSkippingTimeHole = fSkippingTimeHole;
    sDecoderDriverEvent.fSeekToKeyFrame = fSeekToKeyFrame;

    m_listSDecoderDriverEvent.push_back(sDecoderDriverEvent);
    SetEvent(m_hEventHandle);
}

void CDecoderDriverAppThread::SendTuneEnd(LONGLONG hyChannelStartPos)
{
    SDecoderDriverEvent sDecoderDriverEvent;
    sDecoderDriverEvent.eDecoderDriverEventType = DECODER_DRIVER_EVENT_TUNE;
    sDecoderDriverEvent.hyPosition = hyChannelStartPos;

    m_listSDecoderDriverEvent.push_back(sDecoderDriverEvent);
    SetEvent(m_hEventHandle);
}


void CDecoderDriverAppThread::SendRun()
{
    SDecoderDriverEvent sDecoderDriverEvent;
    sDecoderDriverEvent.eDecoderDriverEventType = DECODER_DRIVER_EVENT_RUN;

    m_listSDecoderDriverEvent.push_back(sDecoderDriverEvent);
    SetEvent(m_hEventHandle);
}

void CDecoderDriverAppThread::SendGraphConfused(HRESULT hr)
{
    SDecoderDriverEvent sDecoderDriverEvent;
    sDecoderDriverEvent.eDecoderDriverEventType = DECODER_DRIVER_EVENT_ERROR;
    sDecoderDriverEvent.hr = hr;

    m_listSDecoderDriverEvent.push_back(sDecoderDriverEvent);
    SetEvent(m_hEventHandle);
}

void CDecoderDriverAppThread::SendSetAudioEnable(bool fEnableAudio)
{
    SDecoderDriverEvent sDecoderDriverEvent;
    sDecoderDriverEvent.eDecoderDriverEventType = DECODER_DRIVER_EVENT_SET_AUDIO_ENABLE;
    sDecoderDriverEvent.fEnableAudio = fEnableAudio;

    m_listSDecoderDriverEvent.push_back(sDecoderDriverEvent);
    SetEvent(m_hEventHandle);
}           

void CDecoderDriverAppThread::SendPendingEndEvent(DVR_ENGINE_EVENTS eDVREngineEvent,
                                             REFERENCE_TIME rtEcCompleteOriginStreamTime,
                                             REFERENCE_TIME rtEcCompleteLastSamplePosition,
                                             LONG iArg1,
                                             LONG iArg2)
{
    SDecoderDriverEvent sDecoderDriverEvent;
    sDecoderDriverEvent.eDecoderDriverEventType = DECODER_DRIVER_EVENT_QUEUE_END_EVENT;
    sDecoderDriverEvent.eDVREngineEvent = eDVREngineEvent;
    sDecoderDriverEvent.rtEcCompleteStreamTime = rtEcCompleteOriginStreamTime;
    sDecoderDriverEvent.rtEcCompleteAVPosition = rtEcCompleteLastSamplePosition;
    sDecoderDriverEvent.iArg1 = iArg1;
    sDecoderDriverEvent.iArg2 = iArg2;

    m_listSDecoderDriverEvent.push_back(sDecoderDriverEvent);
    SetEvent(m_hEventHandle);
}

void CDecoderDriverAppThread::SendPendingPositionWatch(
                CDecoderDriverNotifyOnPosition *pcDecoderDriverNotifyOnPosition,
                REFERENCE_TIME rtOriginStreamTime)
{
    SDecoderDriverEvent sDecoderDriverEvent;
    sDecoderDriverEvent.eDecoderDriverEventType = DECODER_DRIVER_EVENT_QUEUE_POSITION_EVENT;
    sDecoderDriverEvent.pcDecoderDriverNotifyOnPosition = pcDecoderDriverNotifyOnPosition;
    sDecoderDriverEvent.rtEcCompleteStreamTime = rtOriginStreamTime;

    m_listSDecoderDriverEvent.push_back(sDecoderDriverEvent);
    SetEvent(m_hEventHandle);
}

void CDecoderDriverAppThread::SendAction(CAppThreadAction *pcAppThreadAction)
{
    SDecoderDriverEvent sDecoderDriverEvent;
    sDecoderDriverEvent.eDecoderDriverEventType = DECODER_DRIVER_EVENT_ACTION;
    sDecoderDriverEvent.pcAppThreadAction = pcAppThreadAction;

    m_listSDecoderDriverEvent.push_back(sDecoderDriverEvent);
    SetEvent(m_hEventHandle);
} // CDecoderDriverAppThread::SendAction;

DWORD CDecoderDriverAppThread::ComputeAppThreadSleep()
{
    DWORD dwSleepTime = 500;

    if (m_rcDecoderDriver.m_eFilterState == State_Running)
    {
        try {
            REFERENCE_TIME rtStreamTimeNow = m_rcDecoderDriver.m_cClockState.GetStreamTime();

            CAutoLock cAutoLock(&m_cCritSecEndEvents);
            if (!m_listSPendingEndEvent.empty())
            {
                std::list<SPendingEndEvent>::iterator iter;

                for (iter = m_listSPendingEndEvent.begin();
                    iter != m_listSPendingEndEvent.end();
                    ++iter)
                {
                    REFERENCE_TIME rtDummy = -1;
                    REFERENCE_TIME rtLastPoll = iter->rtStreamTimeWhenChecked;
                    DWORD dwPredictedSleepUntilEnd = m_rcDecoderDriver.ComputeTimeUntilFinalSample(
                        iter->rtAVPositionOfSampleIfKnown, iter->rtStreamTimeWhenSubmitted,
                        rtDummy, rtLastPoll);
                    if (dwPredictedSleepUntilEnd > 0)
                    {
                        if (dwPredictedSleepUntilEnd < dwSleepTime)
                        {
                            dwSleepTime = dwPredictedSleepUntilEnd;
                            if (dwSleepTime < 35)
                                dwSleepTime = 35;
                        }
                    }
                }
            }
            if (!m_listSPendingPositionEvent.empty())
            {
                std::list<SPendingPositionEvent>::iterator iter;

                for (iter = m_listSPendingPositionEvent.begin();
                    iter != m_listSPendingPositionEvent.end();
                    ++iter)
                {
                    REFERENCE_TIME rtDummy = -1;
                    REFERENCE_TIME rtLastPoll = iter->rtStreamTimeWhenChecked;
                    DWORD dwPredictedSleepUntilEnd = m_rcDecoderDriver.ComputeTimeUntilFinalSample(
                        iter->pcDecoderDriverNotifyOnPosition->m_hyTargetPosition, iter->rtStreamTimeWhenSubmitted,
                        rtDummy, rtLastPoll);
                    if (dwPredictedSleepUntilEnd > 0)
                    {
                        if (dwPredictedSleepUntilEnd < dwSleepTime)
                        {
                            dwSleepTime = dwPredictedSleepUntilEnd;
                            if (dwSleepTime < 5)
                                dwSleepTime = 5;
                        }
                    }
                }
            }
        }
        catch (const std::exception& rcException)
        {
            UNUSED(rcException);
#ifdef  UNICODE
            DbgLog((LOG_ERROR, 2, _T("CDecoderDriverAppThread(%p)::ComputeAppThreadSleep() caught exception %S\n"),
                    this, rcException.what()));
#else
            DbgLog((LOG_ERROR, 2, _T("CDecoderDriverAppThread(%p)::ComputeAppThreadSleep() caught exception %s\n"),
                    this, rcException.what()));
#endif
        };
    }
    return dwSleepTime;
} // CDecoderDriverAppThread::ComputeAppThreadSleep

void CDecoderDriverAppThread::FlushPendingEndEvents()
{
    CAutoLock cAutoLock(&m_cCritSecEndEvents);

    // Some events we need to send if we flush:

    try {
        m_listSPendingPositionEvent.clear();

        std::list<SPendingEndEvent>::iterator iter;

        for (iter = m_listSPendingEndEvent.begin();
                iter != m_listSPendingEndEvent.end();
                ++iter)
        {
            switch (iter->eDVREngineEvent)
            {
            case DVR_SOURCE_EC_COMPLETE_DONE:
            case DVRENGINE_EVENT_BEGINNING_OF_PAUSE_BUFFER:
            case DVRENGINE_EVENT_END_OF_PAUSE_BUFFER:
                DbgLog((LOG_EVENT_DETECTED, 3,
                        TEXT("DecoderDriver::FlushPendingEndEvents() -- sending event 0x%x\n"),
                        (int) iter->eDVREngineEvent ));
                m_rcDecoderDriver.SendNotification(iter->eDVREngineEvent, iter->iArg1, iter->iArg2);
                break;

            case DVRENGINE_EVENT_RECORDING_END_OF_STREAM:
            default:
                break;
            }
        }
    }
    catch (const std::exception& rcException)
    {
        UNUSED(rcException);
#ifdef  UNICODE
        DbgLog((LOG_ERROR, 2, _T("CDecoderDriverAppThread(%p)::FlushPendingEndEvents() caught exception %S\n"),
                this, rcException.what()));
#else
        DbgLog((LOG_ERROR, 2, _T("CDecoderDriverAppThread(%p)::FlushPendingEndEvents() caught exception %s\n"),
                this, rcException.what()));
#endif
    };

    m_listSPendingEndEvent.clear();
} // CDecoderDriverAppThread::FlushPendingEndEvents

void CDecoderDriverAppThread::FireEndEvents()
{
    try {
        if (m_rcDecoderDriver.m_fFlushing)
            return;

        REFERENCE_TIME rtStreamTimeNow = m_rcDecoderDriver.m_cClockState.GetStreamTime();

        CAutoLock cAutoLock(&m_cCritSecEndEvents);

        if (m_listSPendingEndEvent.empty() && m_listSPendingPositionEvent.empty())
            return;

        bool fNoAVBetweenFlushAndEOF = false;

        if (m_rcDecoderDriver.m_fAtEndOfStream)
        {
            fNoAVBetweenFlushAndEOF = (m_rcDecoderDriver.m_fSentKeyFrameSinceStop != CDecoderDriver::COMPLETE_KEY_FRAME);
            if (fNoAVBetweenFlushAndEOF)
            {
                DbgLog((LOG_DECODER_DRIVER, 1,
                        _T("CDecoderDriverAppThread(%p)::FireEndEvent() -- firing due to no a/v between flush and end-of-stream\n"), 
                        this, rtStreamTimeNow ));
            }
        }

        if (!m_listSPendingEndEvent.empty())
        {
            std::list<SPendingEndEvent>::iterator iter;

            for (iter = m_listSPendingEndEvent.begin();
                iter != m_listSPendingEndEvent.end();
                )
            {
                bool fFireEvent = fNoAVBetweenFlushAndEOF;
                if (!fFireEvent &&
                    ((m_rcDecoderDriver.m_eFilterState == State_Stopped) ||
                     !m_rcDecoderDriver.m_fAtEndOfStream))
                {
                    switch (iter->eDVREngineEvent)
                    {
                    case DVRENGINE_EVENT_BEGINNING_OF_PAUSE_BUFFER:
                    case DVRENGINE_EVENT_END_OF_PAUSE_BUFFER:
                        // End-of-stream status has nothing to do with
                        // this event.  On the other hand, if we stop,
                        // we'd better signal now:
                        fFireEvent = (m_rcDecoderDriver.m_eFilterState == State_Stopped);
                        break;

                    case DVR_SOURCE_EC_COMPLETE_DONE:
                    case DVRENGINE_EVENT_RECORDING_END_OF_STREAM:
                    default:
                        fFireEvent = true;
                        break;
                    }
                    if (fFireEvent)
                    {
                        DbgLog((LOG_DECODER_DRIVER, 1,
                                _T("CDecoderDriverAppThread(%p)::FireEndEvent() -- firing due to stop and/or !end-of-stream\n"), this ));
                    }
                }
                if (!fFireEvent)
                {
                    // The graph is running or paused -- see if we're there yet:

                    DWORD dwEstimatedSleepUntilEndTime = m_rcDecoderDriver.ComputeTimeUntilFinalSample(
                        iter->rtAVPositionOfSampleIfKnown, iter->rtStreamTimeWhenSubmitted,
                        iter->rtAVPositionLastKnown, iter->rtStreamTimeWhenChecked);
                    if (dwEstimatedSleepUntilEndTime <= 0)
                        fFireEvent = true;
                    if (fFireEvent)
                    {
                        DbgLog((LOG_DECODER_DRIVER, 1,
                                _T("CDecoderDriverAppThread(%p)::FireEndEvent() -- firing due to rendering @ %I64d\n"),
                                this, rtStreamTimeNow ));
                    }
                }
                if (fFireEvent)
                {
                    if ((DVRENGINE_EVENT_BEGINNING_OF_PAUSE_BUFFER == iter->eDVREngineEvent) &&
                        (DVRENGINE_PAUSED_UNTIL_TRUNCATION == iter->iArg1))
                    {
                        // We are resuming normal play -- no longer at the beginning:

                        DbgLog((LOG_DECODER_DRIVER, 2,
                            _T("CDecoderDriverAppThread()::FireEndEvent() -- noting normal play due to resume-from-pause\n") ));

                        m_rcDecoderDriver.m_eDecoderDriverMediaEndStatus = CDecoderDriver::DECODER_DRIVER_NORMAL_PLAY;
                    }
                    else if ((m_rcDecoderDriver.m_eFilterState != State_Stopped) &&
                             (m_rcDecoderDriver.m_eFilterStatePrior != State_Stopped))
                    {
                        if ((DVRENGINE_EVENT_BEGINNING_OF_PAUSE_BUFFER == iter->eDVREngineEvent) ||
                            ((DVRENGINE_EVENT_RECORDING_END_OF_STREAM == iter->eDVREngineEvent) &&
                            (DVRENGINE_BEGINNING_OF_RECORDING == iter->iArg1)))
                        {
                            // Note:  DVRENGINE_PAUSED_UNTIL_TRUNCATION means that playback is resuming from the
                            //        last rendered point. We're not stopping rendering at that point. Hence
                            //        the exclusion from this code by the if above.
                            m_rcDecoderDriver.m_eDecoderDriverMediaEndStatus = CDecoderDriver::DECODER_DRIVER_START_OF_MEDIA_SENT;
                        }
                        else if ((DVRENGINE_EVENT_END_OF_PAUSE_BUFFER == iter->eDVREngineEvent) ||
                                ((DVRENGINE_EVENT_RECORDING_END_OF_STREAM == iter->eDVREngineEvent) &&
                                (DVRENGINE_END_OF_RECORDING == iter->iArg1)))
                        {
                            m_rcDecoderDriver.m_eDecoderDriverMediaEndStatus = CDecoderDriver::DECODER_DRIVER_END_OF_MEDIA_SENT;
                        }
                    }
                    DbgLog((LOG_EVENT_DETECTED, 3,
                                _T("CDecoderDriverAppThread()::FireEndEvent() -- beginning/end of recording/pause-buffer event\n") ));

                    m_rcDecoderDriver.SendNotification(iter->eDVREngineEvent, iter->iArg1, iter->iArg2);
                    iter = m_listSPendingEndEvent.erase(iter);
                }
                else
                    ++iter;
            }
        }
        if (!m_listSPendingPositionEvent.empty())
        {
            std::list<SPendingPositionEvent>::iterator iter;

            for (iter = m_listSPendingPositionEvent.begin();
                iter != m_listSPendingPositionEvent.end();
                )
            {
                bool fFireEvent = fNoAVBetweenFlushAndEOF;

                if (m_rcDecoderDriver.m_eFilterState == State_Stopped)
                {
                    // We chuck these callbacks if the graph is flushed or stopped:

                    iter = m_listSPendingPositionEvent.erase(iter);
                    continue;
                }

                // The graph is running or paused -- see if we're there yet:

                if (!fFireEvent)
                {
                    DWORD dwEstimatedSleepUntilEndTime = m_rcDecoderDriver.ComputeTimeUntilFinalSample(
                        iter->pcDecoderDriverNotifyOnPosition->m_hyTargetPosition,
                        iter->rtStreamTimeWhenSubmitted,
                        iter->rtAVPositionLastKnown, iter->rtStreamTimeWhenChecked);
                    if (dwEstimatedSleepUntilEndTime <= 0)
                        fFireEvent = true;
                }
                if (fFireEvent)
                {
                    DbgLog((LOG_DECODER_DRIVER, 1,
                            _T("CDecoderDriverAppThread(%p)::FireEndEvent() -- firing position event due to rendering @ %I64d\n"),
                            this, rtStreamTimeNow ));

                    try {
                        //  Hmm... we need to hold no locks during this call but this
                        //  does open up the possibility of a flush zapping the list
                        //  or adding to the list.
                        {
                            CAutoUnlock cAutoUnlock(m_cCritSecEndEvents);

                            DbgLog((LOG_EVENT_DETECTED, 3,
                                    _T("CDecoderDriverAppThread()::FireEndEvent() -- reached position of interest\n") ));

                            iter->pcDecoderDriverNotifyOnPosition->OnPosition();
                        }
                        if (m_listSPendingPositionEvent.empty())
                            break;
                    } catch (const std::exception &rcException2) {
                        UNUSED(rcException2);
#ifdef  UNICODE
                        DbgLog((LOG_ERROR, 2, _T("CDecoderDriverAppThread(%p)::FireEndEvents() caught exception %S\n"),
                                this, rcException2.what()));
#else
                        DbgLog((LOG_ERROR, 2, _T("CDecoderDriverAppThread(%p)::FireEndEvents() caught exception %s\n"),
                                this, rcException2.what()));
#endif
                    }
                    iter = m_listSPendingPositionEvent.erase(iter);
                }
                else
                    ++iter;
            }
        }
    }
    catch (const std::exception& rcException)
    {
        UNUSED(rcException);
#ifdef  UNICODE
        DbgLog((LOG_ERROR, 2, _T("CDecoderDriverAppThread(%p)::FireEndEvents() caught exception %S\n"),
                this, rcException.what()));
#else
        DbgLog((LOG_ERROR, 2, _T("CDecoderDriverAppThread(%p)::FireEndEvents() caught exception %s\n"),
                this, rcException.what()));
#endif
    };
} // CDecoderDriverAppThread::FireEndEvents

void CDecoderDriverAppThread::QueueEndEvent(DVR_ENGINE_EVENTS eDVREngineEvent,
                            REFERENCE_TIME rtAVPositionOfSampleIfKnown,
                            REFERENCE_TIME rtStreamTimeWhenSubmitted,
                            long iArg1,
                            long iArg2)
{
    CAutoLock cAutoLock(&m_cCritSecEndEvents);
    SPendingEndEvent sPendingEndEvent;

    sPendingEndEvent.eDVREngineEvent = eDVREngineEvent;
    sPendingEndEvent.rtAVPositionOfSampleIfKnown = rtAVPositionOfSampleIfKnown;
    sPendingEndEvent.rtStreamTimeWhenSubmitted = rtStreamTimeWhenSubmitted;
    sPendingEndEvent.rtStreamTimeWhenChecked = rtStreamTimeWhenSubmitted;
    sPendingEndEvent.iArg1 = iArg1;
    sPendingEndEvent.iArg2 = iArg2;
    sPendingEndEvent.rtAVPositionLastKnown = -1;

    m_listSPendingEndEvent.push_back(sPendingEndEvent);
} // CDecoderDriverAppThread::QueueEndEvent

void CDecoderDriverAppThread::QueuePositionEvent(
                CDecoderDriverNotifyOnPosition *pcDecoderDriverNotifyOnPosition,
                REFERENCE_TIME rtStreamTimeWhenSubmitted)
{
    CAutoLock cAutoLock(&m_cCritSecEndEvents);
    SPendingPositionEvent sPendingPositionEvent;

    sPendingPositionEvent.pcDecoderDriverNotifyOnPosition = pcDecoderDriverNotifyOnPosition;
    sPendingPositionEvent.rtStreamTimeWhenSubmitted = rtStreamTimeWhenSubmitted;
    sPendingPositionEvent.rtStreamTimeWhenChecked = rtStreamTimeWhenSubmitted;
    sPendingPositionEvent.rtAVPositionLastKnown = -1;

    m_listSPendingPositionEvent.push_back(sPendingPositionEvent);
} // CDecoderDriverAppThread::QueuePositionEvent

DWORD CDecoderDriverAppThread::ThreadMain()
{
    DWORD dwWaitResult;
    BOOL fBackgroundMode = FALSE;


    while (!m_fShutdownSignal)
    {
        if (fBackgroundMode != m_rcDecoderDriver.m_fEnableBackgroundPriority)
        {
            fBackgroundMode = m_rcDecoderDriver.m_fEnableBackgroundPriority;
            _internal_SetThreadPriorityEx(GetCurrentThread(), fBackgroundMode ? THREAD_PRIORITY_NORMAL : THREAD_PRIORITY_ABOVE_NORMAL);
        }

        dwWaitResult = WaitForSingleObject(m_hEventHandle, ComputeAppThreadSleep());
        if (!m_fShutdownSignal)
        {
            try {
                // First:  keep tickling the clock to avoid excessive clock drift
                //         while paused:

                if (m_rcDecoderDriver.m_eFilterState != State_Stopped)
                    m_rcDecoderDriver.m_cClockState.GetClockTime();

                // Second:  see if we can fire off any pending end-of-stream or
                //          end-of-media events:

                FireEndEvents();
            }
            catch (const std::exception& rcException)
            {
                UNUSED(rcException);
#ifdef  UNICODE
                DbgLog((LOG_ERROR, 2, _T("CDecoderDriverAppThread(%p)::ThreadMain() caught exception %S\n"),
                        this, rcException.what()));
#else
                DbgLog((LOG_ERROR, 2, _T("CDecoderDriverAppThread(%p)::ThreadMain() caught exception %s\n"),
                        this, rcException.what()));
#endif
            };
        }
        if (dwWaitResult == WAIT_OBJECT_0)
        {
            CAutoLock cAutoLock(&m_rcDecoderDriver.m_cCritSec);
            ResetEvent(m_hEventHandle);
            std::list<SDecoderDriverEvent>::iterator iter;

            for (iter = m_listSDecoderDriverEvent.begin();
                 !m_fShutdownSignal && (iter != m_listSDecoderDriverEvent.end());
                 iter = m_listSDecoderDriverEvent.begin())
            {
                try {
                    SDecoderDriverEvent sDecoderDriverEvent = *iter;
                    iter = m_listSDecoderDriverEvent.erase(iter);
                    CAutoUnlock cAutoUnlock(m_rcDecoderDriver.m_cCritSec);

                    switch (sDecoderDriverEvent.eDecoderDriverEventType)
                    {
                    case DECODER_DRIVER_EVENT_SET_RATE:
                        m_rcDecoderDriver.HandleSetGraphRate(sDecoderDriverEvent.dblRate);
                        break;

                    case DECODER_DRIVER_EVENT_SET_POSITION:
                        {
                            CAutoLock cAutoRelock(m_rcDecoderDriver.m_pcCritSecApp);

                            m_rcDecoderDriver.HandleGraphSetPosition(&m_rcDecoderDriver, sDecoderDriverEvent.hyPosition,
                                sDecoderDriverEvent.fNoFlushRequested,
                                sDecoderDriverEvent.fSkippingTimeHole,
                                sDecoderDriverEvent.fSeekToKeyFrame);
                        }
                        break;

                    case DECODER_DRIVER_EVENT_TUNE:
                        m_rcDecoderDriver.HandleGraphTune(sDecoderDriverEvent.hyPosition);
                        break;

                    case DECODER_DRIVER_EVENT_RUN:
                        m_rcDecoderDriver.HandleGraphRun();
                        break;

                    case DECODER_DRIVER_EVENT_ERROR:
                        m_rcDecoderDriver.HandleGraphError(sDecoderDriverEvent.hr);
                        break;

                    case DECODER_DRIVER_EVENT_SET_AUDIO_ENABLE:
                        m_rcDecoderDriver.SetEnableAudio(sDecoderDriverEvent.fEnableAudio);
                        break;

                    case DECODER_DRIVER_EVENT_QUEUE_END_EVENT:
                        QueueEndEvent(sDecoderDriverEvent.eDVREngineEvent,
                            sDecoderDriverEvent.rtEcCompleteAVPosition,
                            sDecoderDriverEvent.rtEcCompleteStreamTime,
                            sDecoderDriverEvent.iArg1, sDecoderDriverEvent.iArg2);
                        FireEndEvents();
                        break;

                    case DECODER_DRIVER_EVENT_QUEUE_POSITION_EVENT:
                        QueuePositionEvent(
                            sDecoderDriverEvent.pcDecoderDriverNotifyOnPosition,
                            sDecoderDriverEvent.rtEcCompleteStreamTime);
                        FireEndEvents();
                        break;

                    case DECODER_DRIVER_EVENT_ACTION:
                        try {
                            sDecoderDriverEvent.pcAppThreadAction->Do();
                        } catch (const std::exception &) {};
                        delete sDecoderDriverEvent.pcAppThreadAction;
                        break;
                    }
                }
                catch (const std::exception& rcException)
                {
                    UNUSED(rcException);
#ifdef  UNICODE
                    DbgLog((LOG_ERROR, 2, _T("CDecoderDriverAppThread(%p)::ThreadMain() caught exception %S\n"),
                            this, rcException.what()));
#else
                    DbgLog((LOG_ERROR, 2, _T("CDecoderDriverAppThread(%p)::ThreadMain() caught exception %s\n"),
                            this, rcException.what()));
#endif
                };
            }
        }
        else
            m_rcDecoderDriver.IdleAction();
    }
    return 0;
}

void CDecoderDriverAppThread::OnThreadExit(DWORD &dwOutcome)
{
    if (m_rcDecoderDriver.m_pcDecoderDriverAppThread == this)
    {
        Release();
        m_rcDecoderDriver.m_pcDecoderDriverAppThread = NULL;
    }
}

///////////////////////////////////////////////////////////////////////
//
//  Class CDecoderDriverStreamThread -- declaration and implementation
//
///////////////////////////////////////////////////////////////////////
namespace MSDvr {
    class CDecoderDriverStreamThread : public CComponentWorkerThread
    {
    public:
        CDecoderDriverStreamThread(CDecoderDriver &rcDecoderDriver);

        void CueStreamThread();

    protected:
        virtual DWORD ThreadMain() ;
        virtual void OnThreadExit(DWORD &dwOutcome);

        ~CDecoderDriverStreamThread();

        CDecoderDriver &m_rcDecoderDriver;
        HANDLE m_hEventHandle;
    };

}

CDecoderDriverStreamThread::CDecoderDriverStreamThread(CDecoderDriver &rcDecoderDriver)
    : CComponentWorkerThread(rcDecoderDriver.m_pippmgr->GetFlushEvent())
    , m_rcDecoderDriver(rcDecoderDriver)
    , m_hEventHandle(CreateEvent(NULL, TRUE, FALSE, NULL))
{
    if (!m_hEventHandle)
    {
        throw CHResultError(GetLastError(), "CreateEvent() failure");
    }
}

CDecoderDriverStreamThread::~CDecoderDriverStreamThread()
{
    if (m_hEventHandle)
        CloseHandle(m_hEventHandle);
}

void CDecoderDriverStreamThread::CueStreamThread()
{
    SetEvent(m_hEventHandle);
}

DWORD CDecoderDriverStreamThread::ThreadMain()
{
    DWORD dwWaitResult;
    HANDLE hEventHandles[2];
    hEventHandles[0] = m_hStopEvent;
    hEventHandles[1] = m_hEventHandle;
    CPipelineRouter cPipelineRouterForSamples = m_rcDecoderDriver.m_pippmgr->GetRouter(&m_rcDecoderDriver, false, false);
    BOOL fBackgroundMode = FALSE;

    while (!m_fShutdownSignal)
    {
        if (fBackgroundMode != m_rcDecoderDriver.m_fEnableBackgroundPriority)
        {
            fBackgroundMode = m_rcDecoderDriver.m_fEnableBackgroundPriority;
            _internal_SetThreadPriorityEx(GetCurrentThread(), fBackgroundMode ? THREAD_PRIORITY_BELOW_NORMAL : THREAD_PRIORITY_NORMAL);
        }

        dwWaitResult = WaitForMultipleObjects(2, hEventHandles, FALSE, INFINITE);
        if (dwWaitResult == WAIT_OBJECT_0 + 1)
        {
            CAutoLock cAutoLock(&m_rcDecoderDriver.m_cCritSec);

            ResetEvent(m_hEventHandle);
            std::list<CDecoderDriverQueueItem>::iterator iter;

            for (iter = m_rcDecoderDriver.m_listCDecoderDriverQueueItem.begin();
                !m_fShutdownSignal && !m_rcDecoderDriver.m_fFlushing &&
                    (iter != m_rcDecoderDriver.m_listCDecoderDriverQueueItem.end());
                 iter = m_rcDecoderDriver.m_listCDecoderDriverQueueItem.begin())
            {
                try {
                    CDecoderDriverQueueItem cDecoderDriverQueueItem = *iter;
                    if ((cDecoderDriverQueueItem.m_eQueueItemType == CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_SAMPLE) &&
                        !m_rcDecoderDriver.ReadyForSample(cDecoderDriverQueueItem))
                    {
                        SetEvent(m_hEventHandle);
                        CAutoUnlock cAutoUnlock(m_rcDecoderDriver.m_cCritSec);
                        Sleep(10);
                        break;
                    }
                    iter = m_rcDecoderDriver.m_listCDecoderDriverQueueItem.erase(iter);

                    CAutoUnlock cAutoUnlock(m_rcDecoderDriver.m_cCritSec);

                    switch (cDecoderDriverQueueItem.m_eQueueItemType)
                    {
                    case CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_SAMPLE:
                        {
                            ROUTE eRoute;

                            if (!m_rcDecoderDriver.IsSampleWanted(cDecoderDriverQueueItem))
                            {
                                // We are discarding until we get a key frame.  Discard this one
                                eRoute = UNHANDLED_STOP;
                            }
                            else
                                eRoute = m_rcDecoderDriver.ProcessQueuedSample(cDecoderDriverQueueItem);
                            if ((eRoute == HANDLED_CONTINUE) || (eRoute == UNHANDLED_CONTINUE))
                            {
                                if (m_rcDecoderDriver.m_dblRate >= 0.0)
                                {
                                    if (cDecoderDriverQueueItem.m_hyLatestVideoPosition >= 0)
                                        m_rcDecoderDriver.m_hyLatestVideoTime = cDecoderDriverQueueItem.m_hyLatestVideoPosition;
                                    if (cDecoderDriverQueueItem.m_hyLatestAudioPosition >= 0)
                                        m_rcDecoderDriver.m_hyLatestAudioTime = cDecoderDriverQueueItem.m_hyLatestAudioPosition;
                                }
                                else
                                {
                                    if (cDecoderDriverQueueItem.m_hyEarliestVideoPosition >= 0)
                                        m_rcDecoderDriver.m_hyLatestVideoTime = cDecoderDriverQueueItem.m_hyEarliestVideoPosition;
                                    if (cDecoderDriverQueueItem.m_hyEarliestAudioPosition >= 0)
                                        m_rcDecoderDriver.m_hyLatestAudioTime = cDecoderDriverQueueItem.m_hyEarliestAudioPosition;
                                }
                                if (m_rcDecoderDriver.m_fSentKeyFrameSinceStop == CDecoderDriver::KEY_FRAME_NEEDED)
                                    m_rcDecoderDriver.m_fSentKeyFrameSinceStop = CDecoderDriver::COMPLETE_KEY_FRAME;
                                if (cDecoderDriverQueueItem.m_hyLatestVideoPosition >= 0)
                                    m_rcDecoderDriver.m_fSentVideoSinceStop = true;
                                if (cDecoderDriverQueueItem.m_hyLatestAudioPosition >= 0)
                                    m_rcDecoderDriver.m_fSentAudioSinceStop = true;

                                cPipelineRouterForSamples.ProcessOutputSample(
                                    *cDecoderDriverQueueItem.m_piMediaSample,
                                    *cDecoderDriverQueueItem.m_pcDVROutputPin);
                            }
                        }
                        break;

                    case CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_END_OF_STREAM:
                        m_rcDecoderDriver.AllPinsEndOfStream();
                        m_rcDecoderDriver.m_pippmgr->GetRouter(&m_rcDecoderDriver, false, false).EndOfStream();
                        break;

                    case CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_EXTENSION:
                        m_rcDecoderDriver.DoDispatchExtension(*cDecoderDriverQueueItem.m_pcExtendedRequest);
                        break;
                    }
                }
                catch (const std::exception& rcException)
                {
                    UNUSED(rcException);
#ifdef  UNICODE
                    DbgLog((LOG_ERROR, 2, _T("CDecoderDriverStreamThread(%p)::ThreadMain() caught exception %S\n"),
                            this, rcException.what()));
#else
                    DbgLog((LOG_ERROR, 2, _T("CDecoderDriverStreamThread(%p)::ThreadMain() caught exception %s\n"),
                            this, rcException.what()));
#endif
                };
            }
        }
        else if (dwWaitResult == WAIT_OBJECT_0)
        {
            DbgLog((LOG_SOURCE_STATE, 3,
                _T("CDecoderDriverStreamThread::ThreadMain(%p) detected flush, will block\n"), this));
            bool fCallEndFlush = m_rcDecoderDriver.BeginFlush();
            bool fContinueAfterFlush = m_rcDecoderDriver.m_pippmgr->WaitEndFlush();
            if (fCallEndFlush)
                m_rcDecoderDriver.EndFlush();
            if (!fContinueAfterFlush)
            {
                DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriverStreamThread::ThreadMain(%p) exiting because WaitEndFlush() returned false\n"), this));
                m_fShutdownSignal = true;
                break;
            }
            DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriverStreamThread::ThreadMain(%p) flush done, resuming\n"), this));
        }
    }
    return 0;
}

void CDecoderDriverStreamThread::OnThreadExit(DWORD &dwOutcome)
{
    if (m_rcDecoderDriver.m_pcDecoderDriverStreamThread == this)
    {
        Release();
        m_rcDecoderDriver.m_pcDecoderDriverStreamThread = NULL;
    }
}

///////////////////////////////////////////////////////////////////////
//
//  Class CDecoderDriver -- constructor and destructor
//
///////////////////////////////////////////////////////////////////////

CDecoderDriver::CDecoderDriver(void)
    : m_pcPinMappings(0)
    , m_eFilterState(State_Stopped)
    , m_eFilterStatePrior(State_Stopped)
    , m_pippmgr(0)
    , m_guidCurTimeFormat(TIME_FORMAT_MEDIA_TIME)
    , m_dblRate(1.0)
    , m_dblAbsoluteRate(1.0)
    , m_dblRatePrior(1.0)
    , m_dblAbsoluteRatePrior(1.0)
    , m_piReader(0)
    , m_piSampleConsumer(0)
    , m_rtXInterceptTime(0)
    , m_rtXInterceptTimePrior(0)
    , m_rtLatestRecdStreamEndTime(0)
    , m_rtLatestSentStreamEndTime(0)
    , m_dblMaxFrameRateForward(0.0)
    , m_dblMaxFrameRateBackward(0.0)
    , m_sAMMaxFullDataRate(0)
    , m_hyPreroll(0)
    , m_fKnowDecoderCapabilities(false)
    , m_fSegmentStartNeeded(true)
    , m_fFlushing(false)
    , m_dwSegmentNumber(0)
    , m_rtSegmentStartTime(0)
    , m_rtLatestRateChange(0)
    , m_pcDecoderDriverAppThread(0)
    , m_pcDecoderDriverStreamThread(0)
    , m_pcBaseFilter(0)
    , m_piFilterGraph(0)
    , m_piMediaControl(0)
    , m_piMediaEventSink(0)
    , m_hyLatestMediaTime(0)
    , m_eFrameSkipMode(SKIP_MODE_NORMAL)
    , m_dwFrameSkipModeSeconds(0)
    , m_eFrameSkipModeEffective(SKIP_MODE_NORMAL)
    , m_dwFrameSkipModeSecondsEffective(0)
    , m_fSawSample(false)
    , m_fPositionDiscontinuity(false)
    , m_cCritSec()
    , m_fAtEndOfStream(true)
    , m_pcCritSecApp(0)
    , m_piMediaTypeAnalyzer(NULL)
    , m_cClockState()
    , m_listCDecoderDriverQueueItem()
    , m_hyLatestVideoTime(0)
    , m_hyLatestAudioTime(0)
    , m_hyMinimumLegalTime(0)
    , m_fSentKeyFrameSinceStop(KEY_FRAME_NEEDED)
    , m_fSentAudioSinceStop(false)
    , m_fSentVideoSinceStop(false)
    , m_fPendingRateChange(false)
    , m_dwEcCompleteCount(0)
    , m_fEndOfStreamPending(false)
    , m_fThrottling(true)
    , m_dblFrameToKeyFrameRatio(s_dblFrameToKeyFrameRatio)
    , m_dblSecondsToKeyFrameRatio(s_dblSecondsToKeyFrameRatio)
    , m_eDecoderDriverMediaEndStatus(DECODER_DRIVER_NORMAL_PLAY)
    , m_fEnableBackgroundPriority(FALSE)

{
    for (int actionIdx = 0; actionIdx < MAX_IDLE_ACTIONS; ++actionIdx)
    {
        m_listIPipelineComponentIdleAction[actionIdx] = NULL;
    }

    DbgLog((LOG_DECODER_DRIVER, 2, _T("CDecoderDriver: constructed %p\n"), this));
} // CDecoderDriver::CDecoderDriver

CDecoderDriver::~CDecoderDriver(void)
{
    DbgLog((LOG_DECODER_DRIVER, 2, _T("CDecoderDriver: destroying %p\n"), this));

    Cleanup();
} // CDecoderDriver::~CDecoderDriver

///////////////////////////////////////////////////////////////////////
//
//  Class CDecoderDriver -- IPipelineComponent
//
///////////////////////////////////////////////////////////////////////

void CDecoderDriver::RemoveFromPipeline()
{
    DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver: removing %p from its pipeline\n"), this));
    Cleanup();
} // CDecoderDriver::RemoveFromPipeline

ROUTE CDecoderDriver::GetPrivateInterface(REFIID riid, void *&rpInterface)
{
    ROUTE eRoute = UNHANDLED_CONTINUE;

    if (riid == IID_IDecoderDriver)
    {
        rpInterface = ((IDecoderDriver *)this);
        eRoute = HANDLED_STOP;
    }

    return eRoute;
} // CDecoderDriver::GetPrivateInterface

ROUTE CDecoderDriver::ConfigurePipeline(
                        UCHAR           iNumPins,
                        CMediaType      cMediaTypes[],
                        UINT            iSizeCustom,
                        BYTE            Custom[])
{
    if (m_piMediaTypeAnalyzer)
    {
        UCHAR bPinIndex;
        for (bPinIndex = 0; bPinIndex < iNumPins; ++bPinIndex)
        {
            CMediaTypeDescription *pcMediaTypeDescription =
                m_piMediaTypeAnalyzer->AnalyzeMediaType(cMediaTypes[bPinIndex]);
            if (!pcMediaTypeDescription)
                throw CHResultError(E_INVALIDARG);
            delete pcMediaTypeDescription;
        }
    }

    CAutoLock cAutoLock(&m_cCritSec);

    GetReader();    // called for the side-effect of setting a data member
    GetSampleConsumer(); // ditto
    RefreshDecoderCapabilities();
    ResetLatestTimes();
    return HANDLED_CONTINUE;
}

ROUTE CDecoderDriver::NotifyFilterStateChange(FILTER_STATE eFilterState)
{
    DbgLog((LOG_SOURCE_DISPATCH, 3, _T("CDecoderDriver(%p): changed to state %d\n"), this, (int) eFilterState));

    CAutoLock cAutoLock(&m_cCritSec);

    CDVRSourceFilter &rcDVRSourceFilter = m_pippmgr->GetSourceFilter();
    m_cClockState.CacheFilterClock(rcDVRSourceFilter);
    switch (eFilterState)
    {
    case State_Paused:
        GetGraphInterfaces();
        m_cClockState.Pause();
        switch (m_eFilterState)
        {
        case State_Stopped:
            m_fAtEndOfStream = false;
            if (!m_pcPinMappings)
            {
                InitPinMappings();
            }
            StartStreamThread();
            break;

        default:
            if (m_piMediaEventSink)
            {
                m_piMediaEventSink->Notify(AV_NO_GLITCH_DVR_STOPPED, GetTickCount(), 0);
            }
            break;
        }
        break;

    case State_Running:
        m_cClockState.Run(rcDVRSourceFilter.GetStreamBase());
        if ((m_dblRate > 0.0) &&
            (m_eDecoderDriverMediaEndStatus == DECODER_DRIVER_START_OF_MEDIA_SENT))
        {
            m_eDecoderDriverMediaEndStatus = DECODER_DRIVER_NORMAL_PLAY;
        }
        else if ((m_dblRate < 0.0) &&
                 (m_eDecoderDriverMediaEndStatus == DECODER_DRIVER_END_OF_MEDIA_SENT))
        {
            m_eDecoderDriverMediaEndStatus = DECODER_DRIVER_NORMAL_PLAY;
        }
        if (m_piMediaEventSink)
        {
            m_piMediaEventSink->Notify(AV_NO_GLITCH_DVR_NORMAL_SPEED, GetTickCount(), (m_dblRate == 1.0) ? TRUE : FALSE);
            m_piMediaEventSink->Notify(AV_NO_GLITCH_DVR_RUNNING, GetTickCount(), 0);
        }
        break;

    case State_Stopped:
        StopStreamThread();
        m_cClockState.Stop();
        m_fSawSample = false;
        m_fPositionDiscontinuity = false;
        m_rtLatestSentStreamEndTime = 0;
        m_rtLatestRecdStreamEndTime = 0;
        FlushSamples();
        ResetLatestTimes();
        m_fSentKeyFrameSinceStop = KEY_FRAME_NEEDED;
        m_fSentAudioSinceStop = false;
        m_fSentVideoSinceStop = false;
        m_fSegmentStartNeeded = true;
        m_dblRate = 1.0;
        m_dblAbsoluteRate = 1.0;
        m_dblRatePrior = 1.0;
        m_dblAbsoluteRatePrior = 1.0;
        m_dwSegmentNumber = 0;
        m_rtSegmentStartTime = 0;
        m_rtLatestRateChange = 0;
        m_eFrameSkipMode = SKIP_MODE_NORMAL;
        m_dwFrameSkipModeSeconds = 0;
        m_eFrameSkipModeEffective = SKIP_MODE_NORMAL;
        m_dwFrameSkipModeSecondsEffective = 0;
        if (m_piReader)
            m_piReader->SetFrameSkipMode(SKIP_MODE_NORMAL, 0);
        if (m_piMediaEventSink)
        {
            m_piMediaEventSink->Notify(AV_NO_GLITCH_DVR_STOPPED, GetTickCount(), 0);
            m_piMediaEventSink->Notify(AV_NO_GLITCH_DVR_NORMAL_SPEED, GetTickCount(), TRUE);
        }
        m_piFilterGraph.Release();
        m_piMediaControl.Release();
        m_piMediaEventSink.Release();
        if (m_pippmgr)
        {
            CDVRClock &rcDVRClock = rcDVRSourceFilter.GetDVRClock();
            rcDVRClock.ResetRateAdjustment();
        }
        break;
    }

    if (m_eFilterState != eFilterState)
        m_eFilterStatePrior = m_eFilterState;
    m_eFilterState = eFilterState;
    RefreshDecoderCapabilities();
    return HANDLED_CONTINUE;
} // CDecoderDriver::NotifyFilterStateChange

ROUTE CDecoderDriver::DispatchExtension(
                        CExtendedRequest &rcExtendedRequest)
{
    DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver(%p)::DispatchExtension()\n"), this));
    ROUTE eRoute = UNHANDLED_CONTINUE;

    if (rcExtendedRequest.m_iPipelineComponentPrimaryTarget == this)
    {
        DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver(%p)::DispatchExtension() ... cmd %d for the decoder driver\n"),
            this, rcExtendedRequest.m_eExtendedRequestType));
        CAutoLock cAutoLock(&m_cCritSec);

        if (m_listCDecoderDriverQueueItem.empty())
            eRoute = DoDispatchExtension(rcExtendedRequest);
        else
        {
            eRoute = HANDLED_STOP;
            if ((m_fFlushing || (m_eFilterState == State_Stopped)) &&
                (rcExtendedRequest.m_eFlushAndStopBehavior == CExtendedRequest::DISCARD_ON_FLUSH))
            {
                // Discard this request
                DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver(%p)::DispatchExtension() ... discarding due to stop/flush\n"), this));
            }
            else
            {
                CDecoderDriverQueueItem cDecoderDriverQueueItem;
                cDecoderDriverQueueItem.m_eQueueItemType = CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_EXTENSION;
                cDecoderDriverQueueItem.m_pcExtendedRequest = &rcExtendedRequest;
                m_listCDecoderDriverQueueItem.push_back(cDecoderDriverQueueItem);
                if (m_pcDecoderDriverStreamThread)
                    m_pcDecoderDriverStreamThread->CueStreamThread();
                DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver(%p)::DispatchExtension() ...queuing for stream thread\n"), this));
            }
        }
    }
    return eRoute;
} // CDecoderDriver::DispatchExtension

///////////////////////////////////////////////////////////////////////
//
//  Class CDecoderDriver -- IPlaybackPipelineComponent
//
///////////////////////////////////////////////////////////////////////

unsigned char CDecoderDriver::AddToPipeline(IPlaybackPipelineManager &rippmgr)
{
    DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver(%p)::AddToPipeline()\n"), this));
    CAutoLock cAutoLock(&m_cCritSec);

    m_pippmgr = &rippmgr;
    CDVRSourceFilter &rcBaseFilter = m_pippmgr->GetSourceFilter();

    m_pcCritSecApp = rcBaseFilter.GetAppLock();
    m_pcBaseFilter = &rcBaseFilter;

    StartAppThread();

    return 1;  // 1 streaming thread
} // CDecoderDriver::AddToPipeline

class CAutoUnlocker
{
public:
    CAutoUnlocker(CCritSec &rcCritSec)
        : m_rcCritSec(rcCritSec)
    {
        m_rcCritSec.Unlock();
    }

    ~CAutoUnlocker()
    {
        m_rcCritSec.Lock();
    }

private:
    CCritSec &m_rcCritSec;
};

ROUTE CDecoderDriver::ProcessOutputSample(
    IMediaSample &riMediaSample,
    CDVROutputPin &rcDVROutputPin)
{
    CAutoLock cAutoLock(&m_cCritSec);

    if (m_fFlushing || (m_eFilterState == State_Stopped) || !m_pcDecoderDriverStreamThread)
        return HANDLED_STOP;

    if ((m_eFrameSkipMode != SKIP_MODE_NORMAL) &&
        (riMediaSample.IsSyncPoint() != S_OK))
        return HANDLED_STOP;


    CDecoderDriverQueueItem cDecoderDriverQueueItem;
    cDecoderDriverQueueItem.m_eQueueItemType = CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_SAMPLE;
    cDecoderDriverQueueItem.m_pcDVROutputPin = &rcDVROutputPin;
    cDecoderDriverQueueItem.m_piMediaSample = &riMediaSample;
    ExtractPositions(riMediaSample, rcDVROutputPin,
        cDecoderDriverQueueItem.m_hyEarliestAudioPosition,
        cDecoderDriverQueueItem.m_hyEarliestVideoPosition,
        cDecoderDriverQueueItem.m_hyNominalStartPosition,
        cDecoderDriverQueueItem.m_hyNominalEndPosition,
        cDecoderDriverQueueItem.m_hyLatestAudioPosition,
        cDecoderDriverQueueItem.m_hyLatestVideoPosition);
    m_listCDecoderDriverQueueItem.push_back(cDecoderDriverQueueItem);
    m_pcDecoderDriverStreamThread->CueStreamThread();
    return HANDLED_STOP;
} // CDecoderDriver::ProcessOutputSample

ROUTE CDecoderDriver::EndOfStream()
{
    CAutoLock cAutoLock(&m_cCritSec);

    ROUTE eRoute = m_fAtEndOfStream ? HANDLED_STOP : UNHANDLED_CONTINUE;
    if (!m_fAtEndOfStream)
    {
        if (m_fFlushing || (m_eFilterState == State_Stopped) || !m_pcDecoderDriverStreamThread)
            eRoute = HANDLED_STOP;
        else if (m_listCDecoderDriverQueueItem.empty())
            AllPinsEndOfStream();
        else
        {
            eRoute = HANDLED_STOP;
            CDecoderDriverQueueItem cDecoderDriverQueueItem;
            cDecoderDriverQueueItem.m_eQueueItemType = CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_END_OF_STREAM;
            m_listCDecoderDriverQueueItem.push_back(cDecoderDriverQueueItem);
            m_pcDecoderDriverStreamThread->CueStreamThread();
        }
    }
    return eRoute;
}

///////////////////////////////////////////////////////////////////////
//
//  Class CDecoderDriver -- IDecoderDriver
//
///////////////////////////////////////////////////////////////////////

void CDecoderDriver::ImplementThrottling(bool fThrottle)
{
    m_fThrottling = fThrottle;
}

void CDecoderDriver::SetEndOfStreamPending()
{
    m_fEndOfStreamPending = true;
}

IPipelineComponent *CDecoderDriver::GetPipelineComponent()
{
    return this;
} // CDecoderDriver::GetPipelineComponent

void CDecoderDriver::DeferToAppThread(CAppThreadAction *pcAppThreadAction)
{
    if (pcAppThreadAction)
    {
        CAutoLock cAutoLock(&m_cCritSec);

        ASSERT(m_pcDecoderDriverAppThread);
        m_pcDecoderDriverAppThread->SendAction(pcAppThreadAction);
    }
} // CDecoderDriver::DeferToAppThrad

HRESULT CDecoderDriver::GetPreroll(LONGLONG &rhyPreroll)
{
    {
        CAutoLock cAutoLock(&m_cCritSec);

        RefreshDecoderCapabilities();
    }
    rhyPreroll = m_hyPreroll;
    return S_OK;
} // CDecoderDriver::GetPreroll

bool CDecoderDriver::IsRateSupported(double dblRate)
{
    return ((dblRate <= g_dblMaxSupportedSpeed) &&
            (dblRate >= - g_dblMaxSupportedSpeed) &&
            (dblRate != 0.0));
} // CDecoderDriver::IsRateSupported

void CDecoderDriver::SetNewRate(double dblRate)
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::SetNewRate(%lf)\n"), this, dblRate));

    CAutoLock cAutoLock(&m_cCritSec);

    ASSERT(m_pcDecoderDriverAppThread);
    m_pcDecoderDriverAppThread->SendNewRate(dblRate);
}

void CDecoderDriver::ImplementNewRate(IPipelineComponent *piPipelineComponentOrigin, double dblRate, BOOL fFlushInProgress)
{
    bool fDoRateSet = false;

    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::ImplementNewRate(%lf)\n"), this, dblRate));

    {
        CAutoLock cAutoLock(&m_cCritSec);

        if (!m_pippmgr)
        {
            ImplementGraphConfused(E_FAIL);
            return;
        }

        // This method should always be called on an application thread.
        // So it is safe to go ahead and do things that only application threads
        // can do safely.

        IReader *piReader = GetReader();
        FRAME_SKIP_MODE eFrameSkipModeCurrent = (dblRate < 0.0) ? SKIP_MODE_REVERSE_FAST : SKIP_MODE_NORMAL;
        FRAME_SKIP_MODE eFrameSkipMode = SKIP_MODE_NORMAL;
        DWORD dwFrameSkipModeSeconds = 0;
        ComputeModeForRate(dblRate, eFrameSkipMode, dwFrameSkipModeSeconds);
        if (piReader)
        {
            eFrameSkipModeCurrent = piReader->GetFrameSkipMode();
            if (SkipModeChanged(eFrameSkipModeCurrent, eFrameSkipMode,
                                piReader->GetFrameSkipModeSeconds(), dwFrameSkipModeSeconds))
            {
                if (piReader->SetFrameSkipMode(eFrameSkipMode, dwFrameSkipModeSeconds) != 0)
                {
                    ImplementGraphConfused(E_FAIL);
                    return;
                }
            }
        }
        if (SkipModeChanged(m_eFrameSkipMode, eFrameSkipMode, m_dwFrameSkipModeSeconds, dwFrameSkipModeSeconds))
        {
#ifndef SHIP_BUILD
            switch (eFrameSkipMode)
            {
            case SKIP_MODE_NORMAL:
                DbgLog((LOG_SOURCE_STATE, 4, _T("CDecoderDriver::ImplementNewRate() -- setting reader mode SKIP_MODE_NORMAL\n")));
                break;
            case SKIP_MODE_FAST:
                DbgLog((LOG_SOURCE_STATE, 4, _T("CDecoderDriver::ImplementNewRate() -- setting reader mode SKIP_MODE_FAST\n")));
                break;
            case SKIP_MODE_FAST_NTH:
                DbgLog((LOG_SOURCE_STATE, 4, _T("CDecoderDriver::ImplementNewRate() -- setting reader mode SKIP_MODE_FAST_NTH with N=%u seconds\n"),
                    dwFrameSkipModeSeconds ));
                break;
            case SKIP_MODE_REVERSE_FAST:
                DbgLog((LOG_SOURCE_STATE, 4, _T("CDecoderDriver::ImplementNewRate() -- setting reader mode SKIP_MODE_REVERSE_FAST\n")));
                break;
            case SKIP_MODE_REVERSE_FAST_NTH:
                DbgLog((LOG_SOURCE_STATE, 4, _T("CDecoderDriver::ImplementNewRate() -- setting reader mode SKIP_MODE_REVERSE_FAST_NTH with N=%u seconds\n"),
                    dwFrameSkipModeSeconds ));
                break;
            default:
                DbgLog((LOG_SOURCE_STATE, 4, _T("CDecoderDriver::ImplementNewRate() -- setting reader mode to non-standard value %d\n"), eFrameSkipMode));
                break;
            }
#endif
            m_eFrameSkipMode = eFrameSkipMode;
            m_dwFrameSkipModeSeconds = dwFrameSkipModeSeconds;
            CComPtr<IMediaEventSink> piMediaEventSink;
            piMediaEventSink.Attach(GetMediaEventSink());
            if (piMediaEventSink)
            {
                DbgLog((LOG_EVENT_DETECTED, 3, _T("CDecoderDriver()::ImplementNewRate() ... sending EC_QUALITY_CHANGE event\n") ));
                piMediaEventSink->Notify(EC_QUALITY_CHANGE, 0, 0);
            }
        }

        if (dblRate == m_dblRate)
        {
            DbgLog((LOG_SOURCE_STATE, 4, _T("CDecoderDriver::ImplementNewRate():  the rate is unchanged\n")));
        }
        else
        {
            fDoRateSet = true;
        }
    }
    if (fDoRateSet)
    {
        CDecoderDriverFlushRequest cDecoderDriverFlushRequest(this);

        if (!fFlushInProgress && IsFlushNeededForRateChange())
            cDecoderDriverFlushRequest.BeginFlush();

        if (fFlushInProgress)
        {
            // implies we are on an application thread and no worry about interleaving
            // interleaving with samples.

            EnactRateChange(dblRate);
        }
        else
        {
            CSmartRefPtr<CDecoderDriverEnactRateChange> pcDecoderDriverEnactRateChange =
                new CDecoderDriverEnactRateChange(this, dblRate);
            pcDecoderDriverEnactRateChange.m_pT->Release();
            CPipelineRouter cPipelineRouter = m_pippmgr->GetRouter(piPipelineComponentOrigin, false, true);
            cPipelineRouter.DispatchExtension(*pcDecoderDriverEnactRateChange);
        }

        // The destructor for cDecoderDriverFlushRequest will fire at the end of
        // this scope -- whether normal exit or via exception. That takes care of
        // ensuring that the flush ends.
    }

    if (fDoRateSet)
    {
        CComPtr<IMediaEventSink> piMediaEventSink;
        piMediaEventSink.Attach(GetMediaEventSink());
        if (piMediaEventSink)
        {
            piMediaEventSink->Notify(AV_NO_GLITCH_DVR_NORMAL_SPEED, GetTickCount(), (1.0 == dblRate) ? TRUE : FALSE);
        }
    }

} // CDecoderDriver::ImplementNewRate

void CDecoderDriver::ImplementNewPosition(IPipelineComponent *piPipelineComponent,
                                          LONGLONG hyPosition,
    bool fNoFlushRequested, bool fSkippingTimeHole, bool fSeekToKeyFrame,
    bool fOnAppThread)
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::ImplementNewPosition(%I64d,%u,%u)\n"),
        this, hyPosition, (unsigned) fNoFlushRequested, (unsigned) fSkippingTimeHole));

    if (!fNoFlushRequested && !fOnAppThread)
    {
        CAutoLock cAutoLock(&m_cCritSec);

        ASSERT(m_pcDecoderDriverAppThread);
        m_pcDecoderDriverAppThread->SendNewPosition(hyPosition, fNoFlushRequested, fSkippingTimeHole, fSeekToKeyFrame);
    }
    else
        HandleGraphSetPosition(piPipelineComponent, hyPosition, fNoFlushRequested, fSkippingTimeHole, fSeekToKeyFrame);
} // CDecoderDriver::ImplementNewPosition

void CDecoderDriver::ImplementEndPlayback(PLAYBACK_END_MODE ePlaybackEndMode, IPipelineComponent *piRequester)
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::ImplementEndPlayback(%d)\n"),
        this, (int) ePlaybackEndMode));

    ASSERT(piRequester);
    CPipelineRouter cPipelineRouter = m_pippmgr->GetRouter(piRequester, false, true);
    switch (ePlaybackEndMode)
    {
    case PLAYBACK_AT_STOP_POSITION:
        // Playback reached the stop position. No special stop behaviors were requested.
        cPipelineRouter.EndOfStream();
        break;

    case PLAYBACK_AT_STOP_POSITION_NEW_SEGMENT:
        {
            // Playback reached the stop position. The AM_SEEKING_Segment stop behavior was requested.
            CSmartRefPtr<CDecoderDriverEndSegment> pcDecoderDriverEndSegment = new CDecoderDriverEndSegment(this);
            pcDecoderDriverEndSegment.m_pT->Release(); // drop down to just the CSmartRefPtr ref
            cPipelineRouter.DispatchExtension(*pcDecoderDriverEndSegment);
        }
        break;

    case PLAYBACK_AT_BEGINNING:
        // Playback reached the beginning of the pause buffer or bound recording.
        cPipelineRouter.EndOfStream();
        break;

    case PLAYBACK_AT_END:
        // Playback reached the end of a bound recording or there is nothing in the pause buffer.
        cPipelineRouter.EndOfStream();
        break;
    }

} // CDecoderDriver::ImplementEndPlayback

void CDecoderDriver::ImplementTuneEnd(bool fTuneIsLive,
                                      LONGLONG hyChannelStartPos, bool fCalledFromAppThread)
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::ImplementTuneEnd(%u, %I64d, %u)\n"),
        this, (unsigned) fTuneIsLive, hyChannelStartPos, (unsigned) fCalledFromAppThread));

    if (fCalledFromAppThread)
    {
        if (fTuneIsLive)
            HandleGraphTune(hyChannelStartPos);
    }
    else
    {
        CAutoLock cAutoLock(&m_cCritSec);

        ASSERT(m_pcDecoderDriverAppThread);
        if (fTuneIsLive)
            m_pcDecoderDriverAppThread->SendTuneEnd(hyChannelStartPos);
    }
} // CDecoderDriver::ImplementTuneEnd

bool CDecoderDriver::ImplementBeginFlush()
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::ImplementBeginFlush()\n"), this));

#ifdef WIN_CE_KERNEL_TRACKING_EVENTS
    {
        const wchar_t *pwszMsg = L"CDecoderDriver(%p)::ImplementBeginFlush() -- entry\n";
        CELOGDATA(TRUE,
            CELID_RAW_WCHAR,
            (PVOID)pwszMsg,
            (1 + wcslen(pwszMsg)) * sizeof(wchar_t),
            1,
            CELZONE_MISC);
    }
#endif // WIN_CE_KERNEL_TRACKING_EVENTS


    if (!BeginFlush())
    {
#ifdef WIN_CE_KERNEL_TRACKING_EVENTS
        {
            const wchar_t *pwszMsg = L"CDecoderDriver(%p)::ImplementBeginFlush() -- already flushing, exit\n";
            CELOGDATA(TRUE,
                CELID_RAW_WCHAR,
                (PVOID)pwszMsg,
                (1 + wcslen(pwszMsg)) * sizeof(wchar_t),
                1,
                CELZONE_MISC);
        }
#endif // WIN_CE_KERNEL_TRACKING_EVENTS
        return false;
    }

    try {
        {
            if (m_pippmgr)
                m_pippmgr->StartSync();

            CDVRSourceFilter &rcDVRSourceFilter = m_pippmgr->GetSourceFilter();
            CDVRClock &rcDVRClock = rcDVRSourceFilter.GetDVRClock();
            rcDVRClock.ResetRateAdjustment();
        }
    }
    catch (const std::exception &)
    {
        // We to ensure that we clean up by calling the matching EndFlush():
        try {
            EndFlush();
        } catch (const std::exception &) {};

        throw;
    }

    try {
        FlushSamples();
    }
    catch (const std::exception &)
    {
        // We to ensure that we clean up by calling the matching EndFlush()
        // and EndSync.

        try {
            EndFlush();
        } catch (const std::exception &) {};
        try {
            if (m_pippmgr)
                m_pippmgr->EndSync();
        } catch (const std::exception &) {};
        throw;
    }
    m_fAtEndOfStream = false;
#ifdef WIN_CE_KERNEL_TRACKING_EVENTS
    {
        const wchar_t *pwszMsg = L"CDecoderDriver(%p)::ImplementBeginFlush() -- exit\n";
        CELOGDATA(TRUE,
            CELID_RAW_WCHAR,
            (PVOID)pwszMsg,
            (1 + wcslen(pwszMsg)) * sizeof(wchar_t),
            1,
            CELZONE_MISC);
    }
#endif // WIN_CE_KERNEL_TRACKING_EVENTS
    return true;
} // CDecoderDriver::ImplementBeginFlush

void CDecoderDriver::ImplementEndFlush()
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::ImplementEndFlush()\n"), this));

#ifdef WIN_CE_KERNEL_TRACKING_EVENTS
    {
        const wchar_t *pwszMsg = L"CDecoderDriver(%p)::ImplementEndFlush() -- entry\n";
        CELOGDATA(TRUE,
            CELID_RAW_WCHAR,
            (PVOID)pwszMsg,
            (1 + wcslen(pwszMsg)) * sizeof(wchar_t),
            1,
            CELZONE_MISC);
    }
#endif // WIN_CE_KERNEL_TRACKING_EVENTS


    // We need to make sure that an exception in EndFlush() does not
    // prevent us from executing EndSync:

    try {
        EndFlush();
    }
    catch (const std::exception &)
    {
        if (m_pippmgr)
            m_pippmgr->EndSync();

        throw;
    }

    if (m_pippmgr)
        m_pippmgr->EndSync();

#ifdef WIN_CE_KERNEL_TRACKING_EVENTS
    {
        const wchar_t *pwszMsg = L"CDecoderDriver(%p)::ImplementEndFlush() -- exit\n";
        CELOGDATA(TRUE,
            CELID_RAW_WCHAR,
            (PVOID)pwszMsg,
            (1 + wcslen(pwszMsg)) * sizeof(wchar_t),
            1,
            CELZONE_MISC);
    }
#endif // WIN_CE_KERNEL_TRACKING_EVENTS
} // CDecoderDriver::ImplementEndFlush

void CDecoderDriver::ImplementRun(bool fOnAppThread)
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::ImplementRun()\n"), this));

    if (fOnAppThread)
    {
        HandleGraphRun();
    }
    else
    {
        CAutoLock cAutoLock(&m_cCritSec);

        ASSERT(m_pcDecoderDriverAppThread);
        m_pcDecoderDriverAppThread->SendRun();
    }
} // CDecoderDriver::ImplementRun

void CDecoderDriver::ImplementGraphConfused(HRESULT hr)
{
    DbgLog((LOG_ERROR, 3, _T("CDecoderDriver(%p)::ImplementGraphConfused(%d)\n"), this, hr));

    CAutoLock cAutoLock(&m_cCritSec);

    ASSERT(m_pcDecoderDriverAppThread);
    m_pcDecoderDriverAppThread->SendGraphConfused(hr);
} // CDecoderDriver::ImplementGraphConfused

void CDecoderDriver::ImplementDisabledInputs(bool fSampleComingFirst)
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::ImplementDisabledInputs()\n"), this));

    CAutoLock cAutoLock(&m_cCritSec);

} // CDecoderDriver::ImplementDisabledInputs

void CDecoderDriver::ImplementLoad()
{
    if (!m_fSegmentStartNeeded)
        EndCurrentSegment();
    m_fSawSample = false;
    m_fPositionDiscontinuity = false;
    FlushSamples();
} // CDecoderDriver::ImplementLoad

bool CDecoderDriver::IsAtEOS()
{
    return ((m_eFilterState != State_Stopped) && (m_fAtEndOfStream || m_fEndOfStreamPending));
}

HRESULT CDecoderDriver::IsSeekRecommendedForRate(double dblRate, LONGLONG &hyRecommendedPosition)
{
    hyRecommendedPosition = -1;

    CAutoLock cAutoLock(&m_cCritSec);

    if ((m_eFilterState == State_Stopped) ||
        (dblRate == m_dblRate) ||
        !m_fSawSample ||
        (m_fSentKeyFrameSinceStop == KEY_FRAME_NEEDED) ||
        m_fFlushing)
    {
        // Either we are flushed, will shortly flush because we are stopping, or
        // haven't sent a sample downstream.  There won't be any added delay due
        // to samples queued downstream.
        return S_FALSE;
    }

    if (m_fPositionDiscontinuity || m_fAtEndOfStream)
    {
        // Yikes -- we know there is a seek but not where it will land. So
        // say that we recommend a flush but don't specify where -- let the
        // consumer pick.
        return S_OK;
    }

    std::list<CDecoderDriverQueueItem>::const_iterator iter;
    for (iter = m_listCDecoderDriverQueueItem.begin();
         iter != m_listCDecoderDriverQueueItem.end();
         ++iter)
    {
        if (m_uPendingDiscontinuities ||
            m_fEndOfStreamPending)
        {
            // Again, we know that we should flush-and-seek but we don't know where.
            // Let the consumer pick.
            return S_OK;
        }
    }

    REFERENCE_TIME rtStreamTime = m_cClockState.GetStreamTime();
    if (m_rtLatestSentStreamEndTime - rtStreamTime < s_hyMaximumWaitForRate)
        return S_FALSE;

    hyRecommendedPosition = m_rtLatestKeyDownstream;
    return S_OK;
} // CDecoderDriver::IsSeekRecommendedForRate

void CDecoderDriver::IssueNotification(IPipelineComponent *piPipelineComponent,
                                       long lEventID, long lParam1, long lParam2,
            bool fDeliverNow, bool fDeliverOnlyIfStreaming)
{
    CAutoLock cAutoLock(&m_cCritSec);
    CComPtr<IMediaEventSink> piMediaEventSink;
    piMediaEventSink.Attach(GetMediaEventSink());

    if (fDeliverNow)
    {
        DbgLog((LOG_EVENT_DETECTED, 3, _T("CDecoderDriver(%p)::IssueNotification() ... sending event %ld, param1 %ld, param2 %ld\n"),
                this, lEventID, lParam1, lParam2 ));

        if (piMediaEventSink)
            piMediaEventSink->Notify(lEventID, (LONG_PTR) lParam1, (LONG_PTR) lParam2);
    }
    else if ((m_eFilterState == State_Stopped) || !m_pcDecoderDriverStreamThread)
    {
        if (piMediaEventSink && !fDeliverOnlyIfStreaming)
        {
            DbgLog((LOG_EVENT_DETECTED, 3, _T("CDecoderDriver(%p)::IssueNotification() ... sending event %ld, param1 %ld, param2 %ld\n"),
                this, lEventID, lParam1, lParam2 ));

            piMediaEventSink->Notify(lEventID, (LONG_PTR) lParam1, (LONG_PTR) lParam2);
        }
    }
    else
    {
        CSmartRefPtr<CDecoderDriverSendNotification> pcDecoderDriverSendNotification =
            new CDecoderDriverSendNotification(this, lEventID, lParam1, lParam2, fDeliverOnlyIfStreaming);
        pcDecoderDriverSendNotification.m_pT->Release();

        CPipelineRouter cPipelineRouter = m_pippmgr->GetRouter(piPipelineComponent, false, true);
        cPipelineRouter.DispatchExtension(*pcDecoderDriverSendNotification);
    }
} // CDecoderDriver::IssueNotification

LONGLONG CDecoderDriver::EstimatePlaybackPosition(bool fExemptEndOfStream, bool fAdviseIfEndOfMedia)
{
    CAutoLock cAutoLock(&m_cCritSec);

    // Step 0:  if we have sent an end-of-media event and are asked to return a sentinel
    //          in such a scenario, do so.

    if (fAdviseIfEndOfMedia)
    {
        switch (m_eDecoderDriverMediaEndStatus)
        {
        case DECODER_DRIVER_START_OF_MEDIA_SENT:
            return DECODER_DRIVER_PLAYBACK_POSITION_START_OF_MEDIA;
        
        case DECODER_DRIVER_END_OF_MEDIA_SENT:
            return DECODER_DRIVER_PLAYBACK_POSITION_END_OF_MEDIA;

        default:
            break;
        }
    }

    // Step 1:  verify that the situation allows us to reliably convert
    // between A/V positions and stream times. If not, return the
    // sentinel -1:

    if ((m_fAtEndOfStream && !fExemptEndOfStream) ||
        !m_fSawSample ||
        m_fFlushing ||
        m_fPositionDiscontinuity)
        return DECODER_DRIVER_PLAYBACK_POSITION_UNKNOWN;

    // Step 2: use the current stream time to estimate what A/V position
    // is currently being rendered:

    REFERENCE_TIME rtNow = m_cClockState.GetStreamTime();
    LONGLONG hyEstimatedPos = (LONGLONG)
        (m_rtXInterceptTime + (REFERENCE_TIME) ((m_dblRate * (double) rtNow)));

    // Step 3:  validate the result by capping it with the A/V position
    // of the next sample in the (known) queue:

    std::list<CDecoderDriverQueueItem>::iterator iter;
    bool fFoundReasonToStop = false;
    
    for (iter = m_listCDecoderDriverQueueItem.begin();
        !fFoundReasonToStop && (iter != m_listCDecoderDriverQueueItem.end());
        ++iter)
    {
        CDecoderDriverQueueItem &cDecoderDriverQueueItem = *iter;
        switch (cDecoderDriverQueueItem.m_eQueueItemType)
        {
        case CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_SAMPLE:
            if ((m_dblRate > 0) &&
                (hyEstimatedPos > cDecoderDriverQueueItem.m_hyNominalStartPosition))
                hyEstimatedPos = cDecoderDriverQueueItem.m_hyNominalStartPosition - 1;
            else if ((m_dblRate < 0) &&
                (hyEstimatedPos < cDecoderDriverQueueItem.m_hyNominalEndPosition))
                hyEstimatedPos = cDecoderDriverQueueItem.m_hyNominalEndPosition + 1;
            fFoundReasonToStop = true;
            break;

        case CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_END_OF_STREAM:
            if (!fExemptEndOfStream)
                fFoundReasonToStop = true;
            break;

        case CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_EXTENSION:
            if ((cDecoderDriverQueueItem.m_pcExtendedRequest->m_iPipelineComponentPrimaryTarget == this) &&
                (cDecoderDriverQueueItem.m_pcExtendedRequest->m_eExtendedRequestType ==
                CExtendedRequest::DECODER_DRIVER_SEEK_COMPLETE))
            {
                // This is a pending seek -- don't trust previously sent samples
                return DECODER_DRIVER_PLAYBACK_POSITION_UNKNOWN;
            }
            break;

        default:
            ASSERT(0);
            break;
        }
    }

    return hyEstimatedPos;
} // CDecoderDriver::EstimatePlaybackPosition

void CDecoderDriver::RegisterIdleAction(IPipelineComponentIdleAction *piPipelineComponentIdleAction)
{
    CAutoLock cAutoLock(&m_cCritSec);

    bool fRegisteredAction = false;
    for (int actionIdx = 0; actionIdx < MAX_IDLE_ACTIONS; ++actionIdx)
    {
        if (m_listIPipelineComponentIdleAction[actionIdx] == NULL)
        {
            if (!fRegisteredAction)
            {
                m_listIPipelineComponentIdleAction[actionIdx] = piPipelineComponentIdleAction;
                fRegisteredAction = true;
            }
        }
        else if (m_listIPipelineComponentIdleAction[actionIdx] == piPipelineComponentIdleAction)
        {
            m_listIPipelineComponentIdleAction[actionIdx] = NULL;
        }
    }
} // CDecoderDriver::RegisterIdleAction

void CDecoderDriver::UnregisterIdleAction(IPipelineComponentIdleAction *piPipelineComponentIdleAction)
{
    CAutoLock cAutoLock(&m_cCritSec);

    for (int actionIdx = 0; actionIdx < MAX_IDLE_ACTIONS; ++actionIdx)
    {
        if (m_listIPipelineComponentIdleAction[actionIdx] == piPipelineComponentIdleAction)
        {
            m_listIPipelineComponentIdleAction[actionIdx] = NULL;
        }
    }
} // CDecoderDriver::UnregisterIdleAction

void CDecoderDriver::IdleAction()
{
    for (int actionIdx = 0; actionIdx < MAX_IDLE_ACTIONS; ++actionIdx)
    {
        IPipelineComponentIdleAction *piPipelineComponentIdleAction =
            m_listIPipelineComponentIdleAction[actionIdx];
        if (piPipelineComponentIdleAction)
        {
            try {
                piPipelineComponentIdleAction->DoAction();
            } catch (const std::exception &) {};
        }
    }
} // CDecoderDriver::IdleAction

void CDecoderDriver::SetBackgroundPriorityMode(BOOL fUseBackgroundPriority)
{
    // Not critical enough to warrant locking:

    m_fEnableBackgroundPriority = fUseBackgroundPriority;
}

///////////////////////////////////////////////////////////////////////
//
//  Class CDecoderDriver -- protected methods
//
///////////////////////////////////////////////////////////////////////

void CDecoderDriver::Cleanup()
{
    m_eFilterState = State_Stopped;
    m_eFilterStatePrior = State_Stopped;
    if (m_pcPinMappings)
    {
        delete m_pcPinMappings;
        m_pcPinMappings = NULL;
    }
    m_cClockState.Clear();
    m_listCDecoderDriverQueueItem.clear();
    for (int actionIdx = 0; actionIdx < MAX_IDLE_ACTIONS; ++actionIdx)
    {
        m_listIPipelineComponentIdleAction[actionIdx] = NULL;
    }
    m_guidCurTimeFormat = TIME_FORMAT_MEDIA_TIME;
    m_fThrottling = true;
    m_dblRate = 1.0;
    m_dblAbsoluteRate = 1.0;
    m_eFrameSkipMode = SKIP_MODE_NORMAL;
    m_dwFrameSkipModeSeconds = 0;
    m_eFrameSkipModeEffective = SKIP_MODE_NORMAL;
    m_dwFrameSkipModeSecondsEffective = 0;
    m_piReader = 0;
    m_piSampleConsumer = 0;
    m_rtXInterceptTime = 0;
    m_rtLatestRecdStreamEndTime = 0;
    m_rtLatestSentStreamEndTime = 0;
    m_dblMaxFrameRateForward = 0.0;
    m_dblMaxFrameRateBackward = 0.0;
    m_hyLatestMediaTime = 0;
    m_fFlushing = false;
    m_sAMMaxFullDataRate = 0;
    m_hyPreroll = 0;
    m_fKnowDecoderCapabilities = false;
    m_rtLatestRateChange = 0;
    StopAppThread();
    m_pcBaseFilter = 0;
    m_piMediaControl = 0;
    m_piMediaEventSink = 0;
    m_piFilterGraph = 0;
    m_pippmgr = 0;
    m_fSawSample = false;
    m_fAtEndOfStream = false;
    m_fEndOfStreamPending = false;
    m_uPendingDiscontinuities = 0;
    m_pcCritSecApp= 0;
    ResetLatestTimes();
    m_fPositionDiscontinuity = false;
    m_fSentKeyFrameSinceStop = KEY_FRAME_NEEDED;
    m_fSentAudioSinceStop = false;
    m_fSentVideoSinceStop = false;
    m_fPendingRateChange = false;
    m_eDecoderDriverMediaEndStatus = DECODER_DRIVER_NORMAL_PLAY;

} // CDecoderDriver::Cleanup

void CDecoderDriver::RefreshDecoderCapabilities()
{
    if (!m_fKnowDecoderCapabilities && m_pippmgr)
    {
        DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver(%p)::RefreshDecoderCapabilities()\n"), this));

        // Start with limitations due to the decoder driver:
        m_hyPreroll = 0;
        m_dblMaxFrameRateForward = g_dblMaxFullFrameRateForward;
        m_dblMaxFrameRateBackward = g_dblMaxFullFrameRateBackward;
        m_sAMMaxFullDataRate = (AM_MaxFullDataRate) (g_uRateScaleFactor / g_dblMaxSupportedSpeed);

        QueryGraphForCapabilities();
        
        m_fKnowDecoderCapabilities = true;

        DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver(%p)::RefreshDecoderCapabilities():  preroll = %I64d, max fwd frames = %g, max rev frames = %g, max speed = %ld\n"),
            this, m_hyPreroll, m_dblMaxFrameRateForward, m_dblMaxFrameRateBackward, m_sAMMaxFullDataRate));
    }
} // CDecoderDriver::RefreshDecoderCapabilities

IReader *CDecoderDriver::GetReader(void)
{
    if (!m_piReader)
    {
        try {
            CPipelineRouter cPipelineRouter = m_pippmgr->GetRouter(NULL, false, false);
            void *pvReader = NULL;
            cPipelineRouter.GetPrivateInterface(IID_IReader, pvReader);
            m_piReader = static_cast<IReader*>(pvReader);
        }
        catch (const std::exception& rcException)
        {
            UNUSED (rcException);  // suppress release build warning
#ifdef UNICODE
            DbgLog((LOG_ERROR, 3, _T("CDecoderDriver(%p)::GetReader():  caught exception %S\n"), this, rcException.what()));
#else
            DbgLog((LOG_ERROR, 3, _T("CDecoderDriver(%p)::GetReader():  caught exception %s\n"), this, rcException.what()));
#endif
        }
    }
    return m_piReader;
} // CDecoderDriver::GetReader

ISampleConsumer *CDecoderDriver::GetSampleConsumer(void)
{
    if (!m_piSampleConsumer)
    {
        try {
            CPipelineRouter cPipelineRouter = m_pippmgr->GetRouter(NULL, false, false);
            void *pvSampleConsumer = NULL;
            cPipelineRouter.GetPrivateInterface(IID_ISampleConsumer, pvSampleConsumer);
            m_piSampleConsumer = static_cast<ISampleConsumer*>(pvSampleConsumer);
        }
        catch (const std::exception& rcException)
        {
            UNUSED (rcException);  // suppress release build warning
#ifdef UNICODE
            DbgLog((LOG_ERROR, 3, _T("CDecoderDriver(%p)::GetSampleConsumer():  caught exception %S\n"), this, rcException.what()));
#else
            DbgLog((LOG_ERROR, 3, _T("CDecoderDriver(%p)::GetSampleConsumer():  caught exception %s\n"), this, rcException.what()));
#endif
        }
    }
    return m_piSampleConsumer;
} // CDecoderDriver::GetReader

void CDecoderDriver::StartAppThread()
{
    DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver(%p)::StartAppThread()\n"), this));

    ASSERT(!m_pcDecoderDriverAppThread);

    m_pcDecoderDriverAppThread = new CDecoderDriverAppThread(*this);
    try {
        m_pcDecoderDriverAppThread->StartThread();
    }
    catch (const std::exception&)
    {
        m_pcDecoderDriverAppThread->Release();
        m_pcDecoderDriverAppThread = NULL;
        throw;
    };
} // CDecoderDriver::StartAppThread

void CDecoderDriver::StopAppThread()
{
    DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver(%p)::StopAppThread()\n"), this));

    CDecoderDriverAppThread *pcDecoderDriverAppThread = m_pcDecoderDriverAppThread;
    if (pcDecoderDriverAppThread)
    {
        pcDecoderDriverAppThread->AddRef();
        pcDecoderDriverAppThread->SignalThreadToStop();
        pcDecoderDriverAppThread->SleepUntilStopped();
        pcDecoderDriverAppThread->Release();
        ASSERT(m_pcDecoderDriverAppThread == NULL);
    }
} // CDecoderDriver::StopAppThread

void CDecoderDriver::StartStreamThread()
{
    DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver(%p)::StartStreamThread()\n"), this));

    // Heads up:  parental control needs to be able to run a sequence of
    // actions in a fashion that is effectively atomic with respect to
    // a/v reaching the renderers. Right now, parental control is doing
    // so by raising the priority of the thread in PC to a registry-supplied
    // value (PAL priority 1 = CE priority 249). If you change the code
    // here to raise the priority of the DVR engine playback graph
    // streaming threads, be sure to check that either these threads or
    // the DVR splitter output pin threads are still running at a priority
    // less urgent than parental control.

    ASSERT(!m_pcDecoderDriverStreamThread);

    m_pcDecoderDriverStreamThread = new CDecoderDriverStreamThread(*this);
    try {
        m_pcDecoderDriverStreamThread->StartThread();
    }
    catch (const std::exception&)
    {
        m_pcDecoderDriverStreamThread->Release();
        m_pcDecoderDriverStreamThread = NULL;
        throw;
    };
} // CDecoderDriver::StartStreamThread

void CDecoderDriver::StopStreamThread()
{
    DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver(%p)::StopStreamThread()\n"), this));

    CDecoderDriverStreamThread *pcDecoderDriverStreamThread = m_pcDecoderDriverStreamThread;
    if (pcDecoderDriverStreamThread)
    {
        pcDecoderDriverStreamThread->AddRef();
        pcDecoderDriverStreamThread->SignalThreadToStop();
        {
            CAutoUnlock cAutoUnlock(m_cCritSec);
            pcDecoderDriverStreamThread->SleepUntilStopped();
            }
        pcDecoderDriverStreamThread->Release();
        ASSERT(m_pcDecoderDriverStreamThread == NULL);
    }
    FlushSamples();
} // CDecoderDriver::StopStreamThread

void CDecoderDriver::HandleSetGraphRate(double dblRate)
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::HandleSetGraphRate(%lf)\n"),
        this, dblRate));


    HRESULT hr = E_NOTIMPL;
    IStreamBufferMediaSeeking *piStreamBufferMediaSeeking = NULL;
    if (m_pcBaseFilter)
    {
        hr = m_pcBaseFilter->QueryInterface(__uuidof(IStreamBufferMediaSeeking), (void**) &piStreamBufferMediaSeeking);
        if (SUCCEEDED(hr))
        {
            CComPtr<IStreamBufferMediaSeeking> ccomPtrIStreamBufferMediaSeeking = NULL;
            ccomPtrIStreamBufferMediaSeeking.Attach(piStreamBufferMediaSeeking);
            hr = ccomPtrIStreamBufferMediaSeeking->SetRate(dblRate);
        }
    }
    if (FAILED(hr))
    {
        DbgLog((LOG_ERROR, 3, _T("CDecoderDriver(%p)::HandleSetGraphRate(%lf) failed, HRESULT %d\n"),
            this, dblRate, hr));
        HandleGraphError(hr);
    }
} // CDecoderDriver::HandleSetGraphRate

void CDecoderDriver::HandleGraphSetPosition(IPipelineComponent *piPipelineComponentOrigin,
                                            LONGLONG hyPosition,
                                            bool fNoFlushRequested,
                                            bool fSkippingTimeHole,
                                            bool fSeekToKeyFrame)
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::HandleGraphSetPosition(%I64d,%u,%u,%u)\n"),
        this, hyPosition, (unsigned) fNoFlushRequested, (unsigned) fSkippingTimeHole,
        (unsigned)fSeekToKeyFrame));

#ifdef WIN_CE_KERNEL_TRACKING_EVENTS
    {
        wchar_t pwszMsg[256];
        swprintf(pwszMsg,  L"CDecoderDriver()::HandleGraphSetPosition(%I64d,%u,%u,%u)\n",
            hyPosition, (unsigned) fNoFlushRequested, (unsigned) fSkippingTimeHole,
            (unsigned)fSeekToKeyFrame );
        CELOGDATA(TRUE,
            CELID_RAW_WCHAR,
            (PVOID)pwszMsg,
            (1 + wcslen(pwszMsg)) * sizeof(wchar_t),
            1,
            CELZONE_MISC);
    }
#endif // WIN_CE_KERNEL_TRACKING_EVENTS

    if (!fNoFlushRequested)
    {
        CDecoderDriverFlushRequest cDecoderDriverFlushRequest(this);

        cDecoderDriverFlushRequest.BeginFlush();

        PrePositionChange();
        ISampleConsumer *piSampleConsumer = GetSampleConsumer();
        piSampleConsumer->SetPositionFlushComplete(hyPosition, fSeekToKeyFrame);
        PostPositionChange();

        // And now, no matter whether we exit normally or via an exception
        // the destructor for cDecoderDriverFlushRequest will end any flush
        // it started. If the flush fails by exception, we do want to let the
        // exception be thrown to indicate the failure -- a no-flush seek is
        // not correct operation in this case.
    }

    CAutoLock cAutoLock(&m_cCritSec);
    if (State_Stopped != m_eFilterState)
    {
        CSmartRefPtr<CDecoderDriverSeekComplete> pcDecoderDriverSeekComplete;
        pcDecoderDriverSeekComplete = new CDecoderDriverSeekComplete(this);
        pcDecoderDriverSeekComplete.m_pT->Release();
        ++m_uPendingDiscontinuities;
        CPipelineRouter cPipelineRouter = m_pippmgr->GetRouter(piPipelineComponentOrigin, false, true);
        cPipelineRouter.DispatchExtension(*pcDecoderDriverSeekComplete);
    }

    if (fSkippingTimeHole)
    {
        // We need to mark the pins as requiring a discontinguous marker on their
        // next sample:

        UCHAR bPinCount, bPinIndex;
        bPinCount = m_pcPinMappings->GetPinCount();
        
        for (bPinIndex = 0; bPinIndex < bPinCount; ++bPinIndex)
        {
            SPinState &rsDecoderPinState = m_pcPinMappings->GetPinState(bPinIndex);
            rsDecoderPinState.fPinFlushed = true;
        }

        CComPtr<IMediaEventSink> piMediaEventSink;
        piMediaEventSink.Attach(GetMediaEventSink());
        if (piMediaEventSink)
        {
            DWORD dwStartPosInMilliseconds, dwDurationInMilliseconds;
            dwStartPosInMilliseconds = (DWORD) (m_hyLatestMediaTime / 10000);  // 10000 * 100 nanosec unit = 1 millisecond unit
            dwDurationInMilliseconds = (DWORD) ((hyPosition - m_hyLatestMediaTime) / 10000);
        
            DbgLog((LOG_EVENT_DETECTED, 3, _T("CDecoderDriver(%p)::HandleGraphSetPosition() ... sending event STREAMBUFFER_EC_TIMEHOLE, param1 %ld, param2 %ld\n"),
                this, dwStartPosInMilliseconds, dwDurationInMilliseconds ));

            piMediaEventSink->Notify(STREAMBUFFER_EC_TIMEHOLE, dwStartPosInMilliseconds, dwDurationInMilliseconds);
        }
    }
} // HandleGraphSetPosition

void CDecoderDriver::HandleGraphTune(LONGLONG hyChannelStartPos)
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::HandleGraphTune(%I64d)\n"), this, hyChannelStartPos));

    CAutoLock cAutoLockApp(m_pcCritSecApp);

    ISampleConsumer *piSampleConsumer = GetSampleConsumer();
    if (piSampleConsumer)
        piSampleConsumer->SeekToTunePosition(hyChannelStartPos);

    CComPtr<IMediaEventSink> piMediaEventSink;
    piMediaEventSink.Attach(GetMediaEventSink());
    if (piMediaEventSink)
    {
        DbgLog((LOG_EVENT_DETECTED, 3, _T("CDecoderDriver(%p)::HandleGraphTune() ... sending event DVRENGINE_EVENT_PLAYBACK_TUNE_HANDLED\n"),
                this ));

        piMediaEventSink->Notify(DVRENGINE_EVENT_PLAYBACK_TUNE_HANDLED, 0, piSampleConsumer ? piSampleConsumer->GetLoadIncarnation() : 0);
    }
} // CDecoderDriver::HandleGraphTune

void CDecoderDriver::HandleGraphRun()
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::HandleGraphRun()\n"), this));


    if ((m_eFilterState != State_Paused) && !m_fAtEndOfStream)
    {
        DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::HandleGraphRun() -- no-op since not paused or end-of-stream\n"), this));
        return;
    }

    if (m_fAtEndOfStream)
    {
        CAutoLock cAutoLock(m_pcCritSecApp);

        DbgLog((LOG_SOURCE_STATE, 4, _T("CDecoderDriver(%p)::HandleGraphRun() -- flushing to clear end of stream\n"), this));

        CDecoderDriverFlushRequest cDecoderDriverFlushRequest(this);

        cDecoderDriverFlushRequest.BeginFlush();
        cDecoderDriverFlushRequest.EndFlush();
    }
    if (m_piMediaControl && (m_eFilterState == State_Paused))
    {
        DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::HandleGraphRun() -- telling IMediaControl\n"), this));
        m_piMediaControl->Run();
    }
} // CDecoderDriver::HandleGraphRun

void CDecoderDriver::HandleGraphError(HRESULT hr)
{
    DbgLog((LOG_ERROR, 3, _T("CDecoderDriver(%p)::HandleGraphError(%d)\n"), this, hr));


    // Do not hold locks here -- to avoid deadlock, we must be clear of all locks before
    // calling into IMediaControl::Stop().

    HRESULT hrStop = E_NOTIMPL;

    if (m_eFilterState == State_Stopped)
    {
        DbgLog((LOG_ERROR, 3, _T("CDecoderDriver(%p)::HandleGraphError() -- no-op since already stopped\n"), this));
        return;
    }

    if (m_piMediaControl)
    {
        hrStop = m_piMediaControl->Stop();
    }
    // TODO:  Strip this else if once we have a real source filter:
    else
    {
        DbgLog((LOG_ERROR, 3, _T("CDecoderDriver(%p)::HandleGraphError() -- no IMediaControl, giving up\n"), this));
    }

    CComPtr<IMediaEventSink> piMediaEventSink;
    piMediaEventSink.Attach(GetMediaEventSink());
    if (piMediaEventSink)
    {
        DbgLog((LOG_EVENT_DETECTED, 3, _T("CDecoderDriver(%p)::HandleGraphError() ... sending event EC_STREAM_ERROR_STOPPED, error 0x%x\n"),
                this, hr ));

        piMediaEventSink->Notify(EC_STREAM_ERROR_STOPPED, hr, 0);
        piMediaEventSink->Notify(AV_GLITCH_DVR_ERROR_HALT, GetTickCount(), 0);
    }
} // CDecoderDriver::HandleGraphError

void CDecoderDriver::AllPinsEndOfStream()
{
    DWORD dwECCompleteCount;

    {
        CAutoLock cAutoLock(&m_cCritSec);

        if (m_fAtEndOfStream || m_fFlushing)
            return;

        m_fAtEndOfStream = true;
        m_fEndOfStreamPending = false;
        m_uPendingDiscontinuities = 0;
        dwECCompleteCount = ++m_dwEcCompleteCount;
        ISampleConsumer *piSampleConsumer = GetSampleConsumer();
        DWORD dwLoadIncarnation = piSampleConsumer ? piSampleConsumer->GetLoadIncarnation() : 0;

        CComPtr<IMediaEventSink> piMediaEventSink;
        piMediaEventSink.Attach(GetMediaEventSink());
        if (piMediaEventSink)
        {
            DbgLog((LOG_EVENT_DETECTED, 3, _T("CDecoderDriver(%p)::AllPinsEndOfStream() ... sending event DVR_SOURCE_EC_COMPLETE_PENDING, param1 %ld, param2 %ld\n"),
                this, dwECCompleteCount, 0 ));

            piMediaEventSink->Notify(DVR_SOURCE_EC_COMPLETE_PENDING, dwECCompleteCount, dwLoadIncarnation);
        }

        ASSERT(m_pcDecoderDriverAppThread);
        m_pcDecoderDriverAppThread->SendPendingEndEvent(
            DVR_SOURCE_EC_COMPLETE_DONE,
            m_cClockState.GetStreamTime(),
            GetFinalSamplePosition(),
            (long) dwECCompleteCount,
            dwLoadIncarnation);
    }
} // CDecoderDriver::AllPinsEndOfStream

void CDecoderDriver::SetEnableAudio(bool fEnableAudio)
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::SetEnableAudio(%u)\n"), this, (unsigned) fEnableAudio));

    if (!m_pippmgr)
    {
        DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::SetEnableAudio(%u) -- no pipeline manager, giving up\n"), this, (unsigned) fEnableAudio));
        return;
    }

    CBaseFilter &rcBaseFilter = m_pippmgr->GetFilter();

    // Iterate over the filter graph looking for IAMStreamSelect on a filter
    // that has 1+ audio input pins.

    IFilterGraph *pFilterGraph = rcBaseFilter.GetFilterGraph();
    if (!pFilterGraph)
    {
        DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::SetEnableAudio(%u) -- no filter graph, giving up\n"), this, (unsigned) fEnableAudio));
        return;
    }

    IEnumFilters *pFilterIter;

    if (FAILED(pFilterGraph->EnumFilters(&pFilterIter)))
    {
        DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::SetEnableAudio(%u) -- unable to enumerate filters, giving up\n"), this, (unsigned) fEnableAudio));
        return;
    }

    CComPtr<IEnumFilters> cComPtrIEnumFilters = NULL;
    cComPtrIEnumFilters.Attach(pFilterIter);

    IBaseFilter *pFilter;
    ULONG iFiltersFound;

    while (SUCCEEDED(pFilterIter->Next(1, &pFilter, &iFiltersFound)) &&
        (iFiltersFound > 0))
    {
        CComPtr<IBaseFilter> cComPtrIBaseFilter = NULL;
        cComPtrIBaseFilter.Attach(pFilter);

        if (pFilter == &rcBaseFilter)
            continue;

        IAMStreamSelect *piAMStreamSelect;
        if (FAILED(pFilter->QueryInterface(IID_IAMStreamSelect, (void **) &piAMStreamSelect)))
            continue;

        CComPtr<IAMStreamSelect> cComPtrIAMStreamSelect = NULL;
        cComPtrIAMStreamSelect.Attach(piAMStreamSelect);

        /* Get the count of streams (if possible) and iterate over them */
        DWORD cbStreams, dwStreamIdx;
        if (FAILED(piAMStreamSelect->Count(&cbStreams)))
            continue;

        for (dwStreamIdx = 0; dwStreamIdx < cbStreams; ++dwStreamIdx)
        {
            IUnknown *pUnk = NULL;
            AM_MEDIA_TYPE *pmt = NULL;
            if (FAILED(piAMStreamSelect->Info(dwStreamIdx, &pmt, NULL /* flags */, NULL /* CID */,
                    NULL /* group */, NULL /* stream name */, NULL /* pObject */, &pUnk)))
                continue;
            if (pUnk)
                pUnk->Release();
            if (pmt)
            {
                bool fIsAudio = ((pmt->majortype == MEDIATYPE_Audio) ||
                                 (pmt->majortype == MEDIATYPE_Midi));
                DeleteMediaType(pmt);
                if (fIsAudio)
                {
                    HRESULT hr = piAMStreamSelect->Enable(dwStreamIdx,
                        fEnableAudio ? AMSTREAMSELECTENABLE_ENABLEALL : 0);
                    if (FAILED(hr))
                    {
                        DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::SetEnableAudio(%u) -- IAMStreamSelect::Enable() returned HRESULT %d\n"),
                            this, (unsigned) fEnableAudio, hr));
                    }
                }
            }
        }
    }
}

void CDecoderDriver::StartNewSegment()
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver::StartNewSegment() -- rate %lf, [%I64d,%I64d]\n"),
        m_dblRate, m_rtLatestSentStreamEndTime, (REFERENCE_TIME)0x7fffffffffffffffLL));
    m_fSegmentStartNeeded = false;
    m_rtSegmentStartTime = m_rtLatestSentStreamEndTime;
    if (m_pcPinMappings)
    {
        UCHAR bPinCount = m_pcPinMappings->GetPinCount();
        UCHAR bPinIndex;
        for (bPinIndex = 0; bPinIndex < bPinCount; ++bPinIndex)
        {
            CDVROutputPin *piPin = static_cast<CDVROutputPin*>(&(m_pcPinMappings->GetPin(bPinIndex)));
            piPin->DeliverNewSegment(m_rtLatestSentStreamEndTime,
                (REFERENCE_TIME)0x7fffffffffffffffLL,
                m_dblRate);
        }
    }

    CComPtr<IMediaEventSink> piMediaEventSink;
    piMediaEventSink.Attach(GetMediaEventSink());
    if (piMediaEventSink)
    {
        REFERENCE_TIME rtSegmentStartTime = m_rtLatestSentStreamEndTime;

        DbgLog((LOG_EVENT_DETECTED, 3, _T("CDecoderDriver(%p)::StartNewSegment() ... sending event EC_SEGMENT_STARTED, param1 %I64dd, param2 %ld\n"),
                this, rtSegmentStartTime, m_dwSegmentNumber ));

        piMediaEventSink->Notify(EC_SEGMENT_STARTED, (LONG_PTR) &rtSegmentStartTime, m_dwSegmentNumber);
    }
} // CDeciderDriver::StartNewSegment

void CDecoderDriver::EndCurrentSegment()
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver::EndCurrentSegment()\n")));
    m_fSegmentStartNeeded = true;
    CComPtr<IMediaEventSink> piMediaEventSink;
    piMediaEventSink.Attach(GetMediaEventSink());
    if (piMediaEventSink)
    {
        CAutoLock cAutoLock(&m_cCritSec);

        REFERENCE_TIME rtSegmentEndDuration = m_rtLatestSentStreamEndTime - m_rtSegmentStartTime;

        DbgLog((LOG_EVENT_DETECTED, 3, _T("CDecoderDriver(%p)::EndCurrentSegment() ... sending event EC_END_OF_SEGMENT, param1 %I64d, param2 %ld\n"),
                this, rtSegmentEndDuration, m_dwSegmentNumber ));

        piMediaEventSink->Notify(EC_END_OF_SEGMENT, (LONG_PTR) &rtSegmentEndDuration, m_dwSegmentNumber);
    }
    ++m_dwSegmentNumber;
}

bool CDecoderDriver::BeginFlush()
{
    CAutoLock cAutoLock(&m_cCritSec);

    m_fAtEndOfStream = false;

    if (m_fFlushing)
    {
        DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::BeginFlush() -- ignoring since we're already flushing\n"), this));
        return false;
    }

    if (m_eFilterState == State_Stopped)
    {
        DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::BeginFlush() -- ignoring since we're stopped\n"), this));
        return false;
    }
    if (!m_pippmgr)
    {
        DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::BeginFlush() -- ignoring since no pipeline mgr\n"), this));
        return false;
    }

    m_fFlushing = true;

    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::BeginFlush() -- just set m_fFlushing to true\n"), this));

    if (m_pcPinMappings)
    {
        UCHAR bPinIndex;
        UCHAR bPinCount = m_pcPinMappings->GetPinCount();
        
        for (bPinIndex = 0; bPinIndex < bPinCount; ++bPinIndex)
        {
            SPinState &rsDecoderPinState = m_pcPinMappings->GetPinState(bPinIndex);
            rsDecoderPinState.fPinFlushing = true;
        }
    }

    FlushSamples();
    return true;
} // CDecoderDriver::BeginFlush

void CDecoderDriver::EndFlush()
{
    CAutoLock cAutoLockDecoder(&m_cCritSec);

    if (!m_fFlushing)
    {
        DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::ImplementEndFlush() -- ignoring since we're not flushing\n"), this));
        return;
    }

    if (m_pcPinMappings)
    {
        UCHAR bPinIndex;
        UCHAR bPinCount = m_pcPinMappings->GetPinCount();
        
        for (bPinIndex = 0; bPinIndex < bPinCount; ++bPinIndex)
        {
            SPinState &rsDecoderPinState = m_pcPinMappings->GetPinState(bPinIndex);
            rsDecoderPinState.fPinFlushed = true;
            rsDecoderPinState.fPinFlushing = false;
        }
    }
    m_rtLatestRecdStreamEndTime = 0;
    m_rtLatestSentStreamEndTime = 0;
    m_fSentKeyFrameSinceStop = KEY_FRAME_NEEDED;
    m_fSentAudioSinceStop = false;
    m_fSentVideoSinceStop = false;
    ResetLatestTimes();
    m_fFlushing = false;

    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::EndFlush() -- just set m_fFlushing to false\n"), this));

} // CDecoderDriver::EndFlush

void CDecoderDriver::NoteDiscontinuity()
{
    CAutoLock cAutoLock(&m_cCritSec);

    if (m_pcPinMappings)
    {
        UCHAR bPinIndex;
        UCHAR bPinCount = m_pcPinMappings->GetPinCount();
        
        for (bPinIndex = 0; bPinIndex < bPinCount; ++bPinIndex)
        {
            SPinState &rsDecoderPinState = m_pcPinMappings->GetPinState(bPinIndex);
            rsDecoderPinState.fPinFlushed = true;
        }
    }
    ResetLatestTimes();
} // CDecoderDriver::NoteDiscontinuity

bool CDecoderDriver::ReadyForSample(const CDecoderDriverQueueItem &rcDecoderDriverQueueItem)
{
    if (m_fFlushing)
        return false;

    if (S_OK == rcDecoderDriverQueueItem.m_piMediaSample->IsDiscontinuity())
        ResetLatestTimes();

    switch (m_eFilterState)
    {
    case State_Stopped:
        return false;

    case State_Paused:
        if (ThrottleDeliveryWhenPaused())
            return ((m_fSentKeyFrameSinceStop != COMPLETE_KEY_FRAME) ||
                !m_fSentAudioSinceStop ||
                !m_fSentVideoSinceStop);
        return true;
        break;

    case State_Running:
        UpdateXInterceptAsNeeded(rcDecoderDriverQueueItem);
        if (!ThrottleDeliveryWhenRunning())
            return true;
        break;
    }

    // We must be in state running:

    if (!m_fSawSample ||
        m_fPositionDiscontinuity ||
        (m_fSentKeyFrameSinceStop != COMPLETE_KEY_FRAME) ||
        !m_fSentAudioSinceStop ||
        !m_fSentVideoSinceStop)
        return true;

    LONGLONG rtVideoStart = m_hyLatestVideoTime;
    LONGLONG rtAudioStart = m_hyLatestAudioTime;
    LONGLONG hySampleStart = 0;
    if (m_dblRate > 0.0)
    {
        // When playing forward, we delay only until the earliest packet in the
        // media sample:

        if (rcDecoderDriverQueueItem.m_hyEarliestVideoPosition >= 0)
            rtVideoStart = rcDecoderDriverQueueItem.m_hyEarliestVideoPosition;
        if (rcDecoderDriverQueueItem.m_hyEarliestAudioPosition >= 0)
            rtAudioStart = rcDecoderDriverQueueItem.m_hyEarliestAudioPosition;
        if (rtVideoStart < 0)
            hySampleStart = rtAudioStart;
        else if (rtAudioStart < 0)
            hySampleStart = rtVideoStart;
        else
            hySampleStart = (rtVideoStart > rtAudioStart) ? rtAudioStart : rtVideoStart;
    }
    else
    {
        // When playing backward, we delay on the latest packet:
        if (rcDecoderDriverQueueItem.m_hyLatestVideoPosition >= 0)
            rtVideoStart = rcDecoderDriverQueueItem.m_hyLatestVideoPosition;
        if (rcDecoderDriverQueueItem.m_hyLatestAudioPosition >= 0)
            rtAudioStart = rcDecoderDriverQueueItem.m_hyLatestAudioPosition;
        if (rtVideoStart < 0)
            hySampleStart = rtAudioStart;
        else if (rtAudioStart < 0)
            hySampleStart = rtVideoStart;
        else
            hySampleStart = (rtVideoStart < rtAudioStart) ? rtAudioStart : rtVideoStart;
    }
    if (hySampleStart < m_hyMinimumLegalTime)
        hySampleStart = m_hyMinimumLegalTime;
    LONGLONG hyAdjustedSampleStart = (LONGLONG) ((hySampleStart - m_rtXInterceptTime) / m_dblRate);
    REFERENCE_TIME rtStreamTime = m_cClockState.GetStreamTime();
    if (hyAdjustedSampleStart < rtStreamTime)
    {
        // This is bad -- somehow we've started lagging. Might just be a debugger
        // at work, though. At any rate, keep up playback quality by adjusting our
        // x-intercept to provide good timing:

        DbgLog((LOG_SOURCE_DISPATCH, 3,
            _T("CDecoderDriver::ReadyForSample():  IMPORTANT -- fell behind %I64d (%I64d [%I64d] -> %I64d), catching up\n"),
            rtStreamTime - hyAdjustedSampleStart,
            hyAdjustedSampleStart, hySampleStart, rtStreamTime));

        m_rtXInterceptTime = (hySampleStart - (LONGLONG) ((rtStreamTime + s_hyMinAllowance) * m_dblRate));
        hyAdjustedSampleStart = (REFERENCE_TIME) ((hySampleStart - m_rtXInterceptTime) / m_dblRate);
    }

    DbgLog((LOG_SOURCE_DISPATCH, 5,
        _T("CDecoderDriver::ReadyForSample():  sample time %I64d, now %I64d, lead time %I64d -- %s\n"),
        hyAdjustedSampleStart, rtStreamTime, s_hyAllowanceForDownstreamProcessing,
        (hyAdjustedSampleStart <= rtStreamTime + s_hyAllowanceForDownstreamProcessing) ?
            _T("ready") : _T("sleep")));
    return (hyAdjustedSampleStart <= rtStreamTime + s_hyAllowanceForDownstreamProcessing);
} // CDecoderDriver::ReadyForSample

void CDecoderDriver::ExtractPositions(IMediaSample &riMediaSample,
                CDVROutputPin &rcDVROutputPin,
                LONGLONG &hyEarliestAudioPosition,
                LONGLONG &hyEarliestVideoPosition,
                LONGLONG &hyNominalStartPosition,
                LONGLONG &hyNominalEndPosition,
                LONGLONG &hyLatestAudioPosition,
                LONGLONG &hyLatestVideoPosition)
{
    // The A/V position is now in IMediaSample::GetMediaTime().  The content restriction
    // information is stamped into IMediaSample::GetTime().

    REFERENCE_TIME rtSampleStart, rtSampleEnd;
    HRESULT hr = riMediaSample.GetMediaTime(&rtSampleStart, &rtSampleEnd);
    ASSERT(SUCCEEDED(hr));
    hyEarliestAudioPosition = rtSampleStart;
    hyEarliestVideoPosition = rtSampleStart;
    hyNominalStartPosition = rtSampleStart;
    hyNominalEndPosition = rtSampleEnd;
    hyLatestAudioPosition = rtSampleEnd;
    hyLatestVideoPosition = rtSampleEnd;
} // CDecoderDriver::ExtractPositions

void CDecoderDriver::ResetLatestTimes()
{
    DbgLog((LOG_DECODER_DRIVER, 4, _T("CDecoderDriver::ResetLatestTimes()\n")));
    m_fPositionDiscontinuity = true;
    m_hyLatestVideoTime = -1;
    m_hyLatestAudioTime = -1;
    m_hyMinimumLegalTime = 0;
    m_rtLatestKeyDownstream = -1;
} // CDecoderDriver::ResetLatestTimes

ROUTE CDecoderDriver::ProcessQueuedSample(CDecoderDriverQueueItem &rcDecoderDriverQueueItem)
{
    CAutoLock cAutoLock(&m_cCritSec);

    // Look for the usual reasons for discarding an incoming sample:
    //  * the graph is stopped
    //  * the pin is flushing
    //  * a non-key frame received when we're discarding all but key frames
    //  * an audio frame received when we're at a high or low playback rate

    if ((m_eFilterState == State_Stopped) || m_fAtEndOfStream || m_fFlushing || (m_dblRate == 0.0))
        return HANDLED_STOP;

    IPin *piPin = rcDecoderDriverQueueItem.m_pcDVROutputPin;
    UCHAR bPinIndex = m_pcPinMappings->FindPinPos(piPin);
    SPinState &rsDecoderPinState = m_pcPinMappings->GetPinState(bPinIndex);

    if (rsDecoderPinState.fPinFlushing)
        return HANDLED_STOP;

    if ((m_eFrameSkipModeEffective != SKIP_MODE_NORMAL) &&
        (rcDecoderDriverQueueItem.m_piMediaSample->IsSyncPoint() != S_OK))
        return HANDLED_STOP;

    if ((m_dblRate != 1.0) && rsDecoderPinState.pcMediaTypeDescription &&
        rsDecoderPinState.pcMediaTypeDescription->m_fIsAudio)
        return HANDLED_STOP;

    /* Adjust the stream times per playback rate: */

    REFERENCE_TIME rtSampleStart = rcDecoderDriverQueueItem.m_hyNominalStartPosition;
    REFERENCE_TIME rtSampleEnd = rcDecoderDriverQueueItem.m_hyNominalStartPosition;;
    REFERENCE_TIME rtOrigStart = rtSampleStart;
    REFERENCE_TIME rtOrigEnd = rtSampleEnd;

    UpdateXInterceptAsNeeded(rcDecoderDriverQueueItem);

    // If this is the first sample on this pin after setting the position or tuning,
    // mark it as discontiguous:
    if (rsDecoderPinState.fPinFlushed)
    {
        rsDecoderPinState.fPinFlushed = false;
        rcDecoderDriverQueueItem.m_piMediaSample->SetDiscontinuity(TRUE);
    }

    m_rtLatestRecdStreamEndTime = (m_dblRate > 0.0) ? rtSampleEnd : rtSampleStart;
    rtSampleStart = (REFERENCE_TIME) ((rtSampleStart - m_rtXInterceptTime) / m_dblRate);
    rtSampleEnd = (REFERENCE_TIME) ((rtSampleEnd - m_rtXInterceptTime) / m_dblRate);
    if (m_dblRate < 0.0)
    {
        REFERENCE_TIME rtTemp = rtSampleStart;
        rtSampleStart = rtSampleEnd;
        rtSampleEnd = rtTemp;
    }
// TODO:  sort out the correct form of this code for the
//      bound-to-recording-in-progress scenario in which the sink
//      and source clocks drift apart.
#ifndef SHIP_BUILD
    LONGLONG rtNow = m_cClockState.GetStreamTime();
    DbgLog((LOG_SOURCE_DISPATCH, 3, _T("CDecoderDriver::ProcessOutputSample():  sample drift %ld ms (%I64d now vs %I64d sample)\n"),
        (long) (rtNow - rtSampleStart), rtNow, rtSampleStart));
#endif

    m_rtLatestSentStreamEndTime = rtSampleEnd;
    HRESULT hrSet = SetPresentationTime(*rcDecoderDriverQueueItem.m_piMediaSample, rtSampleStart, rtSampleEnd);
    if (FAILED(hrSet))
    {
        DbgLog((LOG_ERROR, 2, _T("CDecoderDriver::ProcessOutputSample():  SetPresentationTime() returned %d\n"),
            hrSet));
        return UNHANDLED_STOP;
    }

#ifndef SHIP_BUILD
    rcDecoderDriverQueueItem.m_piMediaSample->GetMediaTime(&rtSampleStart, &rtSampleEnd);
    DbgLog((LOG_SOURCE_DISPATCH, 5, _T("CDecoderDriver::ProcessOutputSample(): %u/%u -> %u/%u [%I64d/%I64d] %s %s, %d bytes data\n"),
        (DWORD) (rtOrigStart / 10000),
        (DWORD) (rtOrigEnd / 10000),
        (DWORD) (rtSampleStart / 10000),
        (DWORD) (rtSampleEnd / 10000),
        rtSampleStart,
        rtSampleEnd,
        (rcDecoderDriverQueueItem.m_piMediaSample->IsSyncPoint() == S_OK) ? _T("KEY") : _T(""),
        (rcDecoderDriverQueueItem.m_piMediaSample->IsDiscontinuity() == S_OK) ? _T("DISCONTINUITY") : _T(""),
        rcDecoderDriverQueueItem.m_piMediaSample->GetActualDataLength()));
#endif
    // If this is the first sample in a new segment and we're supposed to end the
    // segment with a segment-ended notification, issue the start segment notification
    // now:
    if (m_fSegmentStartNeeded)
        StartNewSegment();
    
    if (m_pcPinMappings->IsPrimaryPin(bPinIndex))
    {
        LONGLONG mediaStartTime, mediaEndTime;
        if (SUCCEEDED(rcDecoderDriverQueueItem.m_piMediaSample->GetMediaTime(&mediaStartTime, &mediaEndTime)))
        {
            m_hyLatestMediaTime = (mediaEndTime >= 0) ? mediaEndTime : mediaStartTime;
        }
    }

    if (S_OK == rcDecoderDriverQueueItem.m_piMediaSample->IsSyncPoint())
        m_rtLatestKeyDownstream = rcDecoderDriverQueueItem.m_hyNominalStartPosition;
    return HANDLED_CONTINUE;
} // CDecoderDriver::ProcessOutputSample

void CDecoderDriver::UpdateXInterceptAsNeeded(const CDecoderDriverQueueItem &rcDecoderDriverQueueItem)
{
    if (!m_fSawSample || (m_fThrottling && (m_fPositionDiscontinuity || m_fPendingRateChange)))
    {
        // The goal is to have the new presentation time, that is:
        //    (rtSampleStart - rtNextIntercept) / m_dblRate
        // be roughly the same as the current clock time plus a
        // delta that is sufficient to have the sample still be on-time
        // when it arrives downstream.

        LONGLONG hyWanted = m_cClockState.GetStreamTime();
        if (m_fSawSample)
            hyWanted += s_hyAllowanceForDownstreamProcessing;
        if (hyWanted < m_rtLatestSentStreamEndTime)
            hyWanted = m_rtLatestSentStreamEndTime + 1;

        LONGLONG hyVideoPosition = (m_dblRate > 0.0) ?
            rcDecoderDriverQueueItem.m_hyEarliestVideoPosition :
            rcDecoderDriverQueueItem.m_hyLatestVideoPosition;
        if (hyVideoPosition < 0)
        {
            // This sample has no video and yet we must use it as a way of
            // getting our initial position. We'll fall back to an audio position
            // if that is later than our reset (i.e., normal initial) position.
            if (m_hyLatestVideoTime >= 0)
                hyVideoPosition = m_hyLatestVideoTime;
            else
                hyVideoPosition = rcDecoderDriverQueueItem.m_hyEarliestAudioPosition;
        }
        if (hyVideoPosition < m_hyMinimumLegalTime)
            hyVideoPosition = m_hyMinimumLegalTime;

        if (m_fSawSample && m_fPendingRateChange)
        {
            m_rtXInterceptTimePrior = m_rtXInterceptTime;
            m_rtLatestRateChange = hyVideoPosition;
        }

        REFERENCE_TIME rtOldIntercept = m_rtXInterceptTime;
        m_rtXInterceptTime = (hyVideoPosition - (LONGLONG) (hyWanted * m_dblRate));

        if (!m_fSawSample || !m_fPendingRateChange)
            m_rtXInterceptTimePrior = m_rtXInterceptTime;

        DbgLog((LOG_SOURCE_DISPATCH, 3,
            _T("CDecoderDriver::UpdateXInterceptAsNeeded(): setting x-intercept %s:  %I64d -> %I64d\n"),
            m_fSawSample ? (m_fPendingRateChange ? _T("for rate change") : _T("for jump in sample position"))
                    : _T("for initial sample"),
            rtOldIntercept, m_rtXInterceptTime));
        DbgLog((LOG_SOURCE_DISPATCH, 3,
            _T("    UpdateXInterceptAsNeeded():  sample video=[%I64d,%I64d], latest=%I64d, audio=%I64d , stream end=%I64d\n"),
            rcDecoderDriverQueueItem.m_hyEarliestVideoPosition,
            rcDecoderDriverQueueItem.m_hyLatestVideoPosition,
            m_hyLatestVideoTime, rcDecoderDriverQueueItem.m_hyEarliestAudioPosition,
            m_rtLatestSentStreamEndTime));

        m_fSawSample = true;
        m_fPositionDiscontinuity = false;
        m_fPendingRateChange = false;
    }
} // CDecoderDriver::UpdateXInterceptAsNeeded

void CDecoderDriver::EnactRateChange(double dblRate)
{
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver::EnactRateChange(%lf) -- entry\n"), dblRate));
    {
        CAutoLock cAutoLock(&m_cCritSec);

        if ((dblRate != 1.0) && (m_dblRate == 1.0))
        {
            ASSERT(m_pcDecoderDriverAppThread);
            m_pcDecoderDriverAppThread->SendSetAudioEnable(false);
        }
        else if ((dblRate == 1.0) && (m_dblRate != 1.0))
        {
            ASSERT(m_pcDecoderDriverAppThread);
            m_pcDecoderDriverAppThread->SendSetAudioEnable(true);
        }

        ComputeModeForRate(dblRate, m_eFrameSkipModeEffective, m_dwFrameSkipModeSecondsEffective);
        double oldRate = m_dblRate;
        m_dblRatePrior = m_dblRate;
        m_dblRate = dblRate;
        m_dblAbsoluteRatePrior = m_dblAbsoluteRate;
        m_dblAbsoluteRate = (m_dblRate < 0.0) ? -m_dblRate : m_dblRate;
        m_fPendingRateChange = true;

        if (((m_dblRate > 0.0) && (oldRate < 0.0)) ||
            ((m_dblRate < 0.0) && (oldRate > 0.0)))
        {
            // We can't use the old latest incoming positions because they
            // are going in the wrong direction (i.e., we would do the wrong
            // thing in picking sample versus saved values:
            m_hyLatestVideoTime = -1;
            m_hyLatestAudioTime = -1;
        }
        DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver::EnactRateChange(%lf) .. change is pending\n"), dblRate));
    }
    
    // As far as the downstream filters are concerned, we are still playing at
    // a rate of 1x so we don't need to issue a NewSegment() with the new rate.

    // TODO:  Do we need to talk to downstream decoders / renderers about
    // the rate?
    m_fSegmentStartNeeded = true;

    CComPtr<IMediaEventSink> piMediaEventSink;
    piMediaEventSink.Attach(GetMediaEventSink());
    if (piMediaEventSink)
    {
        DWORD dwOldRate = (DWORD) (m_dblRatePrior * 10000);
        DWORD dwNewRate = (DWORD) (dblRate * 10000);

        DbgLog((LOG_EVENT_DETECTED, 3, _T("CDecoderDriver(%p)::EnactRateChange() ... sending event STREAMBUFFER_EC_RATE_CHANGED, param1 %ld, param2 %ld\n"),
            this, dwOldRate, dwNewRate ));

        piMediaEventSink->Notify(STREAMBUFFER_EC_RATE_CHANGED, (LONG_PTR) dwOldRate, (LONG_PTR) dwNewRate);
    }
    DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver::EnactRateChange(%lf) -- exit\n"), dblRate));
} // CDecoderDriver::EnactRateChange

void CDecoderDriver::FlushSamples()
{
    CAutoLock cAutoLock(&m_cCritSec);
    std::list<CDecoderDriverQueueItem>::iterator iter;
    
    m_fEndOfStreamPending = false;
    m_uPendingDiscontinuities= 0;
    m_eDecoderDriverMediaEndStatus = DECODER_DRIVER_NORMAL_PLAY;

    for (iter = m_listCDecoderDriverQueueItem.begin();
        iter != m_listCDecoderDriverQueueItem.end();
        )
    {
        CDecoderDriverQueueItem &cDecoderDriverQueueItem = *iter;
        switch (cDecoderDriverQueueItem.m_eQueueItemType)
        {
        case CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_SAMPLE:
        case CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_END_OF_STREAM:
            DbgLog((LOG_SOURCE_DISPATCH, 4, _T("CDecoderDriver::FlushSamples():  discarding a %s\n"),
                (cDecoderDriverQueueItem.m_eQueueItemType == CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_SAMPLE) ?
                _T("sample") : _T("end-of-stream") ));
            iter = m_listCDecoderDriverQueueItem.erase(iter);
            break;
        case CDecoderDriverQueueItem::DECODER_DRIVER_ITEM_EXTENSION:
            switch (cDecoderDriverQueueItem.m_pcExtendedRequest->m_eFlushAndStopBehavior)
            {
            case CExtendedRequest::DISCARD_ON_FLUSH:
                DbgLog((LOG_DECODER_DRIVER, 4, _T("CDecoderDriver::FlushSamples():  discarding an extension, cmd %d\n"),
                    cDecoderDriverQueueItem.m_pcExtendedRequest->m_eExtendedRequestType ));
                iter = m_listCDecoderDriverQueueItem.erase(iter);
                break;
            case CExtendedRequest::RETAIN_ON_FLUSH:
                ++iter;
                break;
            case CExtendedRequest::EXECUTE_ON_FLUSH:
                {
                    CExtendedRequest *pcExtendedRequest = cDecoderDriverQueueItem.m_pcExtendedRequest;
                    if (pcExtendedRequest)
                        pcExtendedRequest->AddRef();
                    iter = m_listCDecoderDriverQueueItem.erase(iter);
                    try {
                        DoDispatchExtension(*pcExtendedRequest);
                    } catch (const std::exception &) {};
                    if (pcExtendedRequest)
                        pcExtendedRequest->Release();
                }
                break;
            }
            break;
        };
    }

    if (m_pcDecoderDriverAppThread)
        m_pcDecoderDriverAppThread->FlushPendingEndEvents();
} // CDecoderDriver::FlushSamples

void CDecoderDriver::SendNotification(long lEventID, long lParam1, long lParam2)
{
    DbgLog((LOG_EVENT_DETECTED, 3, _T("CDecoderDriver(%p)::SendNotification() ... sending event %ld, param1 %ld, param2 %ld\n"),
                this, lEventID, lParam1, lParam2 ));
    CComPtr<IMediaEventSink> piMediaEventSink;
    piMediaEventSink.Attach(GetMediaEventSink());
    if (piMediaEventSink)
        piMediaEventSink->Notify(lEventID, (LONG_PTR) lParam1, (LONG_PTR) lParam2);
} // CDecoderDriver::SendNotification

ROUTE CDecoderDriver::DoDispatchExtension(CExtendedRequest &rcExtendedRequest)
{
    DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver(%p)::DoDispatchExtension() ... cmd %d\n"),
        this, rcExtendedRequest.m_eExtendedRequestType ));

    CAutoLock cAutoLock(&m_cCritSec);
    ROUTE eRoute = HANDLED_STOP;

    if ((m_fFlushing || (m_eFilterState == State_Stopped)) &&
        (rcExtendedRequest.m_eFlushAndStopBehavior == CExtendedRequest::DISCARD_ON_FLUSH))
    {
        DbgLog((LOG_DECODER_DRIVER, 3, _T("CDecoderDriver(%p)::DispatchExtension() ... discarding due to stop/flush\n"), this));
        return eRoute;
    }

    switch (rcExtendedRequest.m_eExtendedRequestType)
    {
    case CExtendedRequest::DECODER_DRIVER_END_SEGMENT:
        DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::DispatchExtension() ... ending the current segment\n"), this));
        EndCurrentSegment();
        break;

    case CExtendedRequest::DECODER_DRIVER_SEEK_COMPLETE:
        DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::DispatchExtension() ... noting a seek completion\n"), this));
        ASSERT(m_uPendingDiscontinuities);
        --m_uPendingDiscontinuities;
        NoteDiscontinuity();
        break;


    case CExtendedRequest::DECODER_DRIVER_SEND_NOTIFICATION:
        {
            CDecoderDriverSendNotification *pcDecoderDriverSendNotification =
                static_cast<CDecoderDriverSendNotification *>(&rcExtendedRequest);

            DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::DispatchExtension() ... sending event %ld, param1 %ld, param2 %ld\n"),
                this,
                pcDecoderDriverSendNotification->m_lEventId,
                pcDecoderDriverSendNotification->m_lParam1,
                pcDecoderDriverSendNotification->m_lParam2 ));
            switch (pcDecoderDriverSendNotification->m_lEventId)
            {
            case DVR_SOURCE_EC_COMPLETE_DONE:
            case DVRENGINE_EVENT_BEGINNING_OF_PAUSE_BUFFER:
            case DVRENGINE_EVENT_END_OF_PAUSE_BUFFER:
            case DVRENGINE_EVENT_RECORDING_END_OF_STREAM:
                ASSERT(m_pcDecoderDriverAppThread);
                m_pcDecoderDriverAppThread->SendPendingEndEvent(
                    (DVR_ENGINE_EVENTS) pcDecoderDriverSendNotification->m_lEventId,
                    m_cClockState.GetStreamTime(),
                    GetFinalSamplePosition(),
                    pcDecoderDriverSendNotification->m_lParam1,
                    pcDecoderDriverSendNotification->m_lParam2);
                break;

            default:
                SendNotification(
                            pcDecoderDriverSendNotification->m_lEventId,
                            pcDecoderDriverSendNotification->m_lParam1,
                            pcDecoderDriverSendNotification->m_lParam2);
                break;
            }
        }
        break;

    case CExtendedRequest::DECODER_DRIVER_ENACT_RATE:
        {
            CDecoderDriverEnactRateChange *pcDecoderDriverEnactRateChange =
                static_cast<CDecoderDriverEnactRateChange *>(&rcExtendedRequest);

            DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::DispatchExtension() ... noting change to rate %lf\n"),
                this, pcDecoderDriverEnactRateChange->m_dblRate));
            EnactRateChange(pcDecoderDriverEnactRateChange->m_dblRate);
        }
        break;

    case CExtendedRequest::DECODER_DRIVER_CALLBACK_AT_POSITION:
        {
            CDecoderDriverNotifyOnPosition *pcDecoderDriverNotifyOnPosition =
                static_cast<CDecoderDriverNotifyOnPosition *>(&rcExtendedRequest);

            DbgLog((LOG_SOURCE_STATE, 3, _T("CDecoderDriver(%p)::DispatchExtension() ... queuing request for position notification %I64d\n"),
                this, pcDecoderDriverNotifyOnPosition->m_hyTargetPosition ));
                m_pcDecoderDriverAppThread->SendPendingPositionWatch(
                    pcDecoderDriverNotifyOnPosition,
                    m_cClockState.GetStreamTime());
        }
        break;
    }
    return eRoute;
} // CDecoderDriver::DoDispatchExtension

void CDecoderDriver::InitPinMappings()
{
    ASSERT(m_pippmgr);
    ASSERT(m_pcBaseFilter); // programming error if NULL
    CDVRSourceFilter &rcDVRSourceFilter = m_pippmgr->GetSourceFilter();
    if (rcDVRSourceFilter.GetPinCount() == 0)
        return;

    m_pcPinMappings = new CPinMappings(*m_pcBaseFilter, m_piMediaTypeAnalyzer,
            &m_pippmgr->GetPrimaryOutput(), true);
} // CDecoderDriver::InitPinMappings

HRESULT CDecoderDriver::SetPresentationTime(IMediaSample &riMediaSample, REFERENCE_TIME &rtStartTime, REFERENCE_TIME &rtEndTime)
{
    return riMediaSample.SetTime(&rtStartTime, &rtEndTime);
} // CDecoderDriver::SetPresentationTime

void CDecoderDriver::QueryGraphForCapabilities()
{
    if (!m_pippmgr)
        return;

    CBaseFilter &rcBaseFilter = m_pippmgr->GetFilter();

    // Iterate over the filter graph to see if there are more restrictions:

    IFilterGraph *pFilterGraph = rcBaseFilter.GetFilterGraph();
    if (!pFilterGraph)
        return;

    CComPtr<IEnumFilters> pFilterIter;

    if (SUCCEEDED(pFilterGraph->EnumFilters(&pFilterIter)))
    {
        IBaseFilter *pFilter;
        ULONG iFiltersFound;

        while (SUCCEEDED(pFilterIter->Next(1, &pFilter, &iFiltersFound)) &&
            (iFiltersFound > 0))
        {
            if (pFilter != &rcBaseFilter)
            {
                IKsPropertySet *pKsPropertySet;
                if (SUCCEEDED(pFilter->QueryInterface(IID_IKsPropertySet, (void **) &pKsPropertySet)))
                {
                    /* Ask for the full-frame, maximum forward and backward rates, rate change protocol */
                    DWORD cbReturned;
                    AM_MaxFullDataRate lMaxDataRate;
                    if (SUCCEEDED(pKsPropertySet->Get(AM_KSPROPSETID_TSRateChange,
                                                    AM_RATE_MaxFullDataRate, NULL, 0,
                                                    &lMaxDataRate, sizeof(lMaxDataRate), &cbReturned)))
                    {
                        if (lMaxDataRate < m_sAMMaxFullDataRate)
                            m_sAMMaxFullDataRate = lMaxDataRate;
                    }
#ifndef _WIN32_WCE
                    AM_QueryRate frameRateReturns;
                    if (SUCCEEDED(pKsPropertySet->Get(AM_KSPROPSETID_TSRateChange,
                                                    AM_RATE_QueryFullFrameRate, NULL, 0,
                                                    &frameRateReturns, sizeof(frameRateReturns), &cbReturned)))
                    {
                        double dblMaxForward = (((double) frameRateReturns.lMaxForwardFullFrame/ (double)g_uRateScaleFactor));
                        if (dblMaxForward < m_dblMaxFrameRateForward)
                            m_dblMaxFrameRateForward = dblMaxForward;
                        double dblMaxBackward = (((double) frameRateReturns.lMaxReverseFullFrame / (double)g_uRateScaleFactor));
                        if (dblMaxBackward < (double)m_dblMaxFrameRateBackward)
                            m_dblMaxFrameRateBackward = dblMaxBackward;
                    }
#endif /* _WIN32_WCE */
                    pKsPropertySet->Release();
                }
            }
            pFilter->Release();
        }
    }
} // CDecoderDriver::QueryGraphForCapabilities

bool CDecoderDriver::IsSampleWanted(CDecoderDriverQueueItem &rcDecoderDriverQueueItem)
{
    if ((m_eFilterState == State_Paused) &&
        (m_fSentKeyFrameSinceStop == CDecoderDriver::KEY_FRAME_NEEDED) &&
        (rcDecoderDriverQueueItem.m_piMediaSample->IsSyncPoint() != S_OK))
        return false;
    return true;
} // CDecoderDriver::IsSampleWanted

void CDecoderDriver::ComputeModeForRate(double dblRate, FRAME_SKIP_MODE &eFrameSkipMode, DWORD &dwFrameSkipModeSeconds)
{
    double dblAbsRate = fabs(dblRate);
    double dblKeyFrameRate = dblAbsRate / m_dblFrameToKeyFrameRatio;
    double dblKeyFramePerSecondsRate =  dblKeyFrameRate / m_dblSecondsToKeyFrameRatio;

    eFrameSkipMode = (dblRate > 0.0) ? SKIP_MODE_NORMAL : SKIP_MODE_REVERSE_FAST;
    dwFrameSkipModeSeconds = 0;

    if ((dblRate > 0.0) && (dblAbsRate > m_dblMaxFrameRateForward))
    {
        if (dblKeyFrameRate <= m_dblMaxFrameRateForward)
            eFrameSkipMode = SKIP_MODE_FAST;
        else
        {
            eFrameSkipMode = SKIP_MODE_FAST_NTH;
            dwFrameSkipModeSeconds = 1 + (DWORD) (dblKeyFramePerSecondsRate / m_dblMaxFrameRateForward);
        }
    }
    else if ((dblRate < 0.0) && (dblKeyFrameRate > m_dblMaxFrameRateBackward))
    {
        eFrameSkipMode = SKIP_MODE_REVERSE_FAST_NTH;
        dwFrameSkipModeSeconds = 1 + (DWORD) (dblKeyFramePerSecondsRate / m_dblMaxFrameRateBackward);
        if (dwFrameSkipModeSeconds < 1)
            dwFrameSkipModeSeconds = 1;
    }
    DbgLog((LOG_SOURCE_STATE, 3,
        TEXT("CDecoderDriver::ComputeModeForRate(%lf) -> mode %d, i-frame every %u seconds\n"),
            dblRate, (int) eFrameSkipMode, dwFrameSkipModeSeconds ));;
} // CDecoderDriver::ComputeModeForRate()

void CDecoderDriver::GetGraphInterfaces()
{
    // NOOP if we have all our interfaces.  Note, this could be problematic
    // if we are added to one graph, get our interfaces, and then are removed
    // and added to a different graph.  But that was a limitation of the previous
    // implementation as well so we'll live with it.
    if (m_piFilterGraph && m_piMediaControl && m_piMediaEventSink)
        return;

    // NULL out our cached graph interfaces.  NOTE: We're not calling Release
    // on these.  See next comment for details.
    m_piFilterGraph = NULL;
    m_piMediaControl = NULL;
    m_piMediaEventSink = NULL;

    if (!m_pcBaseFilter)
    {
        ASSERT(FALSE);
        return;
    }

    m_piFilterGraph = m_pcBaseFilter->GetFilterGraph();
    if (m_piFilterGraph)
    {
        // We cannot keep a reference on the filter graph because the
        // filter graph holds a reference to the source filter and hence
        // decoder driver. Ditto for the media control and media event
        // sink.  However, GetFilterGraph does not AddRef the filter graph
        // so we just hold onto the returned value as is.  We must release
        // the media control and events though since QI does AddRef.
        IMediaControl *piMediaControl = NULL;
        HRESULT hr = m_piFilterGraph->QueryInterface(IID_IMediaControl, (void **)&piMediaControl);
        if (SUCCEEDED(hr))
        {
            m_piMediaControl.Attach(piMediaControl);
        }
        ASSERT(m_piMediaControl);

        IMediaEventSink *piMediaEventSink = NULL;
        hr = m_piFilterGraph->QueryInterface(IID_IMediaEventSink, (void **)&piMediaEventSink);
        if (SUCCEEDED(hr))
        {
            m_piMediaEventSink.Attach(piMediaEventSink);
        }
        ASSERT(m_piMediaEventSink);
    }
} // CDecoderDriver::GetGraphInterfaces()

REFERENCE_TIME CDecoderDriver::GetFinalSamplePosition()
{
    return m_fSawSample ? m_rtLatestSentStreamEndTime : -1;
} // CDecoderDriver::GetFinalSamplePosition

DWORD CDecoderDriver::ComputeTimeUntilFinalSample(
            REFERENCE_TIME rtAVPosition,
            REFERENCE_TIME rtDispatchStreamTime,
            REFERENCE_TIME &rtAVPositionLastKnownWhileRunning,
            REFERENCE_TIME &rtLastPollTime)
{
    if (rtAVPosition == -1)
        return 0;

    REFERENCE_TIME rtNow = m_cClockState.GetStreamTime();
    REFERENCE_TIME rtSample = (REFERENCE_TIME) ((rtAVPosition - m_rtXInterceptTime) / m_dblRate);
    if (m_eFilterState == State_Running)
    {
        if ((rtAVPositionLastKnownWhileRunning == rtSample) && (rtNow - rtLastPollTime > 250LL * 10000LL))
            return 0;  // stuck
        if (rtAVPositionLastKnownWhileRunning != rtSample)
            rtLastPollTime = rtNow;
        rtAVPositionLastKnownWhileRunning = rtSample;
    }

    // We have converted everything to stream times so we don't have to worry about
    // the playback direction:

    if (rtSample <= rtNow)
        return 0;
    return (DWORD) ((rtSample - rtNow) / 10000LL);
} // CDecoderDriver::ComputeTimeUntilFinalSample

bool CDecoderDriver::SkipModeChanged(FRAME_SKIP_MODE eFrameSkipMode1, FRAME_SKIP_MODE eFrameSkipMode2,
                             DWORD dwFrameSkipModeSeconds1, DWORD dwFrameSkipModeSeconds2)
{
    if (eFrameSkipMode1 != eFrameSkipMode2)
        return true;            // change due to mode value changing

    // At this point, we know that the skip modes are identical:

    if ((eFrameSkipMode1 == SKIP_MODE_FAST_NTH) || (eFrameSkipMode1 == SKIP_MODE_REVERSE_FAST_NTH))
    {
        if (dwFrameSkipModeSeconds1 != dwFrameSkipModeSeconds2)
            return true;        // change due to seconds interval changing
    }

    return false;
} // CDecoderDriver::SkipModeChanged

IMediaEventSink *CDecoderDriver::GetMediaEventSink()
{
    CComPtr<IMediaEventSink> piMediaEventSink = m_piMediaEventSink;
    if (piMediaEventSink)
    {
        return piMediaEventSink.Detach();
    }

    if (State_Stopped == m_eFilterState)
    {
        if (!m_pcBaseFilter)
        {
            return NULL;
        }

        IFilterGraph *piFilterGraph = m_pcBaseFilter->GetFilterGraph();
        if (piFilterGraph)
        {
            HRESULT hr = piFilterGraph->QueryInterface(IID_IMediaEventSink, (void **)&piMediaEventSink.p);
            if (SUCCEEDED(hr))
            {
                return piMediaEventSink.Detach();   // the reference was bumped by QueryInterface()
            }
        }
    }

    return NULL;
} // CDecoderDriver::GetMediaEventSink

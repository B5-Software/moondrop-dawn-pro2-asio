#ifndef __IASIODRV_H
#define __IASIODRV_H

#include <windows.h>
#include <unknwn.h>
#include "asio.h"

// CLSID for Dawn Pro 2 ASIO: {B9B1E0A1-35D8-011D-8000-000000000001}
// {b9b1e0a1-35d8-011d-8000-000000000001}
static const GUID CLSID_DawnPro2Asio = 
{ 0xb9b1e0a1, 0x35d8, 0x011d, { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } };

// IID for IASIO: {254CD580-08A0-11D1-A120-0020AFD77242}
static const GUID IID_IASIO = 
{ 0x254cd580, 0x08a0, 0x11d1, { 0xa1, 0x20, 0x00, 0x20, 0xaf, 0xd7, 0x72, 0x42 } };

#undef INTERFACE
#define INTERFACE IASIO

DECLARE_INTERFACE_(IASIO, IUnknown)
{
    // IUnknown
    STDMETHOD(QueryInterface) (THIS_ REFIID riid, void** ppv) PURE;
    STDMETHOD_(ULONG, AddRef) (THIS) PURE;
    STDMETHOD_(ULONG, Release) (THIS) PURE;

    // IASIO
    STDMETHOD_(ASIOBool, init) (THIS_ void *sysHandle) PURE;
    STDMETHOD_(void, getDriverName) (THIS_ char *name) PURE;
    STDMETHOD_(long, getDriverVersion) (THIS) PURE;
    STDMETHOD_(void, getErrorMessage) (THIS_ char *string) PURE;
    STDMETHOD_(ASIOError, start) (THIS) PURE;
    STDMETHOD_(ASIOError, stop) (THIS) PURE;
    STDMETHOD_(ASIOError, getChannels) (THIS_ long *numInputChannels, long *numOutputChannels) PURE;
    STDMETHOD_(ASIOError, getLatencies) (THIS_ long *inputLatency, long *outputLatency) PURE;
    STDMETHOD_(ASIOError, getBufferSize) (THIS_ long *minSize, long *maxSize, long *preferredSize, long *granularity) PURE;
    STDMETHOD_(ASIOError, canSampleRate) (THIS_ ASIOSampleRate sampleRate) PURE;
    STDMETHOD_(ASIOError, getSampleRate) (THIS_ ASIOSampleRate *sampleRate) PURE;
    STDMETHOD_(ASIOError, setSampleRate) (THIS_ ASIOSampleRate sampleRate) PURE;
    STDMETHOD_(ASIOError, getClockSources) (THIS_ ASIOClockSource *clocks, long *numSources) PURE;
    STDMETHOD_(ASIOError, setClockSource) (THIS_ long reference) PURE;
    STDMETHOD_(ASIOError, getSamplePosition) (THIS_ ASIOSamples *sPos, ASIOTimeStamp *tStamp) PURE;
    STDMETHOD_(ASIOError, getChannelInfo) (THIS_ ASIOChannelInfo *info) PURE;
    STDMETHOD_(ASIOError, createBuffers) (THIS_ ASIOBufferInfo *bufferInfos, long numChannels, long bufferSize, ASIOCallbacks *callbacks) PURE;
    STDMETHOD_(ASIOError, disposeBuffers) (THIS) PURE;
    STDMETHOD_(ASIOError, controlPanel) (THIS) PURE;
    STDMETHOD_(ASIOError, future) (THIS_ long selector, void *opt) PURE;
    STDMETHOD_(ASIOError, outputReady) (THIS) PURE;
};

#endif

/*
 * MOONDROP DAWN PRO 2 Native DSD ASIO Driver
 * Author: B5-Software
 * License: MIT License
 */

#ifndef __DAWN_ASIO_H
#define __DAWN_ASIO_H

#include "iasiodrv.h"
#include <windows.h>
#include <stdint.h>
#include <stddef.h>

// ============================================================================
// Minimal libusbK ABI (loaded dynamically from libusbK.dll at runtime)
// ============================================================================

typedef void* KLST_HANDLE;
typedef void* KLST_DEVINFO_HANDLE;
typedef void* KUSB_HANDLE;
typedef void* PKISO_CONTEXT;

#pragma pack(push, 1)
typedef struct _D2_SETUP_PACKET {
    unsigned char  RequestType;
    unsigned char  Request;
    unsigned short Value;
    unsigned short Index;
    unsigned short Length;
} D2_SETUP_PACKET;
#pragma pack(pop)

typedef struct _D2_KISO_PACKET {
    unsigned long  Offset;
    unsigned short Length;
    unsigned short Status;
} D2_KISO_PACKET;

typedef struct _D2_KISO_CONTEXT {
    long           Flags;          // KISO_FLAG
    unsigned long  StartFrame;
    short          ErrorCount;
    short          NumberOfPackets;
    unsigned long  UrbHdrStatus;
    D2_KISO_PACKET IsoPackets[1];  // flexible array (access via offsetof)
} D2_KISO_CONTEXT;

#define D2_KISO_FLAG_SET_START_FRAME 0x00000001L

typedef BOOL (WINAPI *PFN_LstK_Init)(KLST_HANDLE* DeviceList, UINT Flags);
typedef BOOL (WINAPI *PFN_LstK_FindByVidPid)(KLST_HANDLE DeviceList, INT Vid, INT Pid, KLST_DEVINFO_HANDLE* DeviceInfo);
typedef BOOL (WINAPI *PFN_LstK_Free)(KLST_HANDLE DeviceList);
typedef BOOL (WINAPI *PFN_UsbK_Init)(KUSB_HANDLE* InterfaceHandle, KLST_DEVINFO_HANDLE DevInfo);
typedef BOOL (WINAPI *PFN_UsbK_Free)(KUSB_HANDLE InterfaceHandle);
typedef BOOL (WINAPI *PFN_UsbK_SetAltInterface)(KUSB_HANDLE InterfaceHandle, UCHAR NumberOrIndex, BOOL IsIndex, UCHAR AltSettingNumber);
typedef BOOL (WINAPI *PFN_UsbK_ControlTransfer)(KUSB_HANDLE InterfaceHandle, D2_SETUP_PACKET SetupPacket, PUCHAR Buffer, UINT BufferLength, PUINT LengthTransferred, LPOVERLAPPED Overlapped);
typedef BOOL (WINAPI *PFN_UsbK_IsoReadPipe)(KUSB_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, UINT BufferLength, LPOVERLAPPED Overlapped, PKISO_CONTEXT IsoContext);
typedef BOOL (WINAPI *PFN_UsbK_IsoWritePipe)(KUSB_HANDLE InterfaceHandle, UCHAR PipeID, PVOID Buffer, UINT BufferLength, LPOVERLAPPED Overlapped, PKISO_CONTEXT IsoContext);
typedef BOOL (WINAPI *PFN_UsbK_GetCurrentFrameNumber)(KUSB_HANDLE InterfaceHandle, PUINT FrameNumber);
typedef BOOL (WINAPI *PFN_UsbK_GetOverlappedResult)(KUSB_HANDLE InterfaceHandle, LPOVERLAPPED Overlapped, PUINT lpNumberOfBytesTransferred, BOOL bWait);
typedef BOOL (WINAPI *PFN_IsoK_Init)(PKISO_CONTEXT* IsoContext, INT NumberOfPackets, INT StartFrame);
typedef BOOL (WINAPI *PFN_IsoK_ReUse)(PKISO_CONTEXT IsoContext);
typedef BOOL (WINAPI *PFN_IsoK_SetPacket)(PKISO_CONTEXT IsoContext, INT PacketIndex, const D2_KISO_PACKET* IsoPacket);
typedef BOOL (WINAPI *PFN_IsoK_Free)(PKISO_CONTEXT IsoContext);

// ============================================================================
// ASIO driver
// ============================================================================

class CDawnPro2Asio : public IASIO
{
public:
    CDawnPro2Asio();
    virtual ~CDawnPro2Asio();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IASIO
    STDMETHODIMP_(ASIOBool) init(void *sysHandle);
    STDMETHODIMP_(void) getDriverName(char *name);
    STDMETHODIMP_(long) getDriverVersion();
    STDMETHODIMP_(void) getErrorMessage(char *string);
    STDMETHODIMP_(ASIOError) start();
    STDMETHODIMP_(ASIOError) stop();
    STDMETHODIMP_(ASIOError) getChannels(long *numInputChannels, long *numOutputChannels);
    STDMETHODIMP_(ASIOError) getLatencies(long *inputLatency, long *outputLatency);
    STDMETHODIMP_(ASIOError) getBufferSize(long *minSize, long *maxSize, long *preferredSize, long *granularity);
    STDMETHODIMP_(ASIOError) canSampleRate(ASIOSampleRate sampleRate);
    STDMETHODIMP_(ASIOError) getSampleRate(ASIOSampleRate *sampleRate);
    STDMETHODIMP_(ASIOError) setSampleRate(ASIOSampleRate sampleRate);
    STDMETHODIMP_(ASIOError) getClockSources(ASIOClockSource *clocks, long *numSources);
    STDMETHODIMP_(ASIOError) setClockSource(long reference);
    STDMETHODIMP_(ASIOError) getSamplePosition(ASIOSamples *sPos, ASIOTimeStamp *tStamp);
    STDMETHODIMP_(ASIOError) getChannelInfo(ASIOChannelInfo *info);
    STDMETHODIMP_(ASIOError) createBuffers(ASIOBufferInfo *bufferInfos, long numChannels, long bufferSize, ASIOCallbacks *callbacks);
    STDMETHODIMP_(ASIOError) disposeBuffers();
    STDMETHODIMP_(ASIOError) controlPanel();
    STDMETHODIMP_(ASIOError) future(long selector, void *opt);
    STDMETHODIMP_(ASIOError) outputReady();

private:
    LONG m_refCount;
    HWND m_hwnd;
    bool m_active;
    bool m_isDSD;
    ASIOSampleRate m_sampleRate;
    long m_bufferSize;
    ASIOCallbacks *m_callbacks;
    ASIOBufferInfo *m_bufferInfos;

    // ASIO ping-pong buffers [channel][buffer index]
    void *m_channelBuffers[2][2];
    int m_bufferIndex;
    uint64_t m_samplePos;

    // libusbK (dynamic)
    HMODULE m_hLib;
    PFN_LstK_Init m_pLstK_Init;
    PFN_LstK_FindByVidPid m_pLstK_FindByVidPid;
    PFN_LstK_Free m_pLstK_Free;
    PFN_UsbK_Init m_pUsbK_Init;
    PFN_UsbK_Free m_pUsbK_Free;
    PFN_UsbK_SetAltInterface m_pUsbK_SetAltInterface;
    PFN_UsbK_ControlTransfer m_pUsbK_ControlTransfer;
    PFN_UsbK_IsoReadPipe m_pUsbK_IsoReadPipe;
    PFN_UsbK_IsoWritePipe m_pUsbK_IsoWritePipe;
    PFN_UsbK_GetCurrentFrameNumber m_pUsbK_GetCurrentFrameNumber;
    PFN_UsbK_GetOverlappedResult m_pUsbK_GetOverlappedResult;
    PFN_IsoK_Init m_pIsoK_Init;
    PFN_IsoK_ReUse m_pIsoK_ReUse;
    PFN_IsoK_SetPacket m_pIsoK_SetPacket;
    PFN_IsoK_Free m_pIsoK_Free;

    KLST_HANDLE m_kList;
    KUSB_HANDLE m_kUsb;
    bool m_deviceOpen;

    LONG m_glitchCount;

    // Streaming thread
    HANDLE m_hThread;
    HANDLE m_hStopEvent;
    DWORD m_streamThreadId;
    static DWORD WINAPI StreamingThreadProc(LPVOID lpParam);
    void StreamLoop();

    // Helpers
    bool LoadLibusbK();
    void UnloadLibusbK();
    bool OpenDevice();
    void CloseDevice();
    bool SetSampleFrequency(ULONG freq);
};

// COM Class Factory
class CDawnClassFactory : public IClassFactory
{
public:
    CDawnClassFactory() : m_refCount(1) {}
    virtual ~CDawnClassFactory() {}

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();
    STDMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv);
    STDMETHODIMP LockServer(BOOL fLock);

private:
    LONG m_refCount;
};

#endif

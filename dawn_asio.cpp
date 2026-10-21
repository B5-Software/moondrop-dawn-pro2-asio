// dawn_asio.cpp - MOONDROP DAWN PRO 2 Native DSD ASIO Driver
// Author: B5-Software
// License: MIT License
//
// Native DSD256 (UAC2 AltSetting 4, Raw DSD) + PCM.
// Transport: libusbK KISO with asynchronous hardware feedback (Endpoint 0x83).

#include "dawn_asio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mmsystem.h>
#include <stdarg.h>

static FILE *g_log = NULL;
static void D2LOG(const char *fmt, ...)
{
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

static inline BYTE D2Rev8(BYTE b)
{
    b = (BYTE)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = (BYTE)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = (BYTE)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

static DWORD D2RegGet(const char *name, DWORD def)
{
    HKEY hk;
    DWORD v = def, sz = sizeof(DWORD), type = 0;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\MOONDROP DAWN PRO 2 ASIO", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hk, name, NULL, &type, (LPBYTE)&v, &sz) != ERROR_SUCCESS || type != REG_DWORD)
            v = def;
        RegCloseKey(hk);
    }
    return v;
}

// Stream engine constants (validated in spike tests)
#define DAWN_RING        3     // OUT URBs in flight
#define DAWN_NP          40    // packets per OUT URB (40 microframes = 5 ms)
#define DAWN_OUT_PERIOD  5     // USB frames per OUT URB
#define DAWN_FBNP        8     // feedback packets per URB (8 ms)
#define DAWN_FB_PERIOD   8
#define DAWN_LEAD        6     // initial start-frame lead
#define DAWN_MAXPKT      776   // endpoint wMaxPacketSize (alt 4)
#define DAWN_DSD_SAMPLES_PER_BYTE 8  // ASIOSTDSDInt8MSB1: 8 DSD bits packed per byte

static LONG g_serverLocks = 0;
static HINSTANCE g_hInstance = NULL;

// ============================================================================
// COM Infrastructure
// ============================================================================

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

STDAPI DllCanUnloadNow(void)
{
    return (g_serverLocks == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = NULL;

    if (IsEqualCLSID(rclsid, CLSID_DawnPro2Asio)) {
        CDawnClassFactory *pFactory = new CDawnClassFactory();
        if (!pFactory) return E_OUTOFMEMORY;
        HRESULT hr = pFactory->QueryInterface(riid, ppv);
        pFactory->Release();
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

static const char *DRIVER_NAME = "MOONDROP DAWN PRO 2 ASIO";
static const char *CLSID_STR = "{B9B1E0A1-35D8-011D-8000-000000000001}";

STDAPI DllRegisterServer(void)
{
    char modulePath[MAX_PATH];
    GetModuleFileNameA(g_hInstance, modulePath, MAX_PATH);

    HKEY hKey;
    char subKey[256];

    snprintf(subKey, sizeof(subKey), "CLSID\\%s", CLSID_STR);
    if (RegCreateKeyExA(HKEY_CLASSES_ROOT, subKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE *)DRIVER_NAME, (DWORD)strlen(DRIVER_NAME) + 1);
        RegCloseKey(hKey);
    }

    snprintf(subKey, sizeof(subKey), "CLSID\\%s\\InprocServer32", CLSID_STR);
    if (RegCreateKeyExA(HKEY_CLASSES_ROOT, subKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE *)modulePath, (DWORD)strlen(modulePath) + 1);
        RegSetValueExA(hKey, "ThreadingModel", 0, REG_SZ, (const BYTE *)"Both", 5);
        RegCloseKey(hKey);
    }

    snprintf(subKey, sizeof(subKey), "SOFTWARE\\ASIO\\%s", DRIVER_NAME);
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, subKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "CLSID", 0, REG_SZ, (const BYTE *)CLSID_STR, (DWORD)strlen(CLSID_STR) + 1);
        RegSetValueExA(hKey, "Description", 0, REG_SZ, (const BYTE *)DRIVER_NAME, (DWORD)strlen(DRIVER_NAME) + 1);
        RegCloseKey(hKey);
    }

    snprintf(subKey, sizeof(subKey), "SOFTWARE\\ASIO\\%s", DRIVER_NAME);
    if (RegCreateKeyExA(HKEY_CURRENT_USER, subKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "CLSID", 0, REG_SZ, (const BYTE *)CLSID_STR, (DWORD)strlen(CLSID_STR) + 1);
        RegSetValueExA(hKey, "Description", 0, REG_SZ, (const BYTE *)DRIVER_NAME, (DWORD)strlen(DRIVER_NAME) + 1);
        RegCloseKey(hKey);
    }

    return S_OK;
}

STDAPI DllUnregisterServer(void)
{
    char subKey[256];
    snprintf(subKey, sizeof(subKey), "CLSID\\%s\\InprocServer32", CLSID_STR);
    RegDeleteKeyA(HKEY_CLASSES_ROOT, subKey);
    snprintf(subKey, sizeof(subKey), "CLSID\\%s", CLSID_STR);
    RegDeleteKeyA(HKEY_CLASSES_ROOT, subKey);

    snprintf(subKey, sizeof(subKey), "SOFTWARE\\ASIO\\%s", DRIVER_NAME);
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, subKey);
    RegDeleteKeyA(HKEY_CURRENT_USER, subKey);
    return S_OK;
}

// ============================================================================
// Class Factory
// ============================================================================

STDMETHODIMP CDawnClassFactory::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
        *ppv = static_cast<IClassFactory *>(this);
        AddRef();
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CDawnClassFactory::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) CDawnClassFactory::Release()
{
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) delete this;
    return count;
}

STDMETHODIMP CDawnClassFactory::CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv)
{
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    if (!ppv) return E_POINTER;
    *ppv = NULL;

    CDawnPro2Asio *pDriver = new CDawnPro2Asio();
    if (!pDriver) return E_OUTOFMEMORY;
    HRESULT hr = pDriver->QueryInterface(riid, ppv);
    pDriver->Release();
    return hr;
}

STDMETHODIMP CDawnClassFactory::LockServer(BOOL fLock)
{
    if (fLock) InterlockedIncrement(&g_serverLocks);
    else InterlockedDecrement(&g_serverLocks);
    return S_OK;
}

// ============================================================================
// CDawnPro2Asio Implementation
// ============================================================================

CDawnPro2Asio::CDawnPro2Asio()
    : m_refCount(1),
      m_hwnd(NULL),
      m_active(false),
      m_isDSD(false),
      m_sampleRate(11289600.0),
      m_bufferSize(2048),
      m_callbacks(NULL),
      m_bufferInfos(NULL),
      m_bufferIndex(0),
      m_samplePos(0),
      m_hLib(NULL),
      m_pLstK_Init(NULL),
      m_pLstK_FindByVidPid(NULL),
      m_pLstK_Free(NULL),
      m_pUsbK_Init(NULL),
      m_pUsbK_Free(NULL),
      m_pUsbK_SetAltInterface(NULL),
      m_pUsbK_ControlTransfer(NULL),
      m_pUsbK_IsoReadPipe(NULL),
      m_pUsbK_IsoWritePipe(NULL),
      m_pUsbK_GetCurrentFrameNumber(NULL),
      m_pUsbK_GetOverlappedResult(NULL),
      m_pIsoK_Init(NULL),
      m_pIsoK_ReUse(NULL),
      m_pIsoK_SetPacket(NULL),
      m_pIsoK_Free(NULL),
      m_kList(NULL),
      m_kUsb(NULL),
      m_deviceOpen(false),
      m_glitchCount(0),
      m_hThread(NULL),
      m_hStopEvent(NULL),
      m_streamThreadId(0)
{
    InterlockedIncrement(&g_serverLocks);
    memset(m_channelBuffers, 0, sizeof(m_channelBuffers));
}

CDawnPro2Asio::~CDawnPro2Asio()
{
    stop();
    disposeBuffers();
    CloseDevice();
    UnloadLibusbK();
    InterlockedDecrement(&g_serverLocks);
}

STDMETHODIMP CDawnPro2Asio::QueryInterface(REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    // ASIO hosts (incl. foo_out_asio / foo_out_asio+dsd) CoCreate with the driver CLSID
    // as riid; always hand out IASIO.
    *ppv = static_cast<IASIO *>(this);
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CDawnPro2Asio::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) CDawnPro2Asio::Release()
{
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) delete this;
    return count;
}

// ============================================================================
// libusbK dynamic loading / device open
// ============================================================================

bool CDawnPro2Asio::LoadLibusbK()
{
    if (m_hLib) return true;

    char path[MAX_PATH] = {0};
    if (g_hInstance) GetModuleFileNameA(g_hInstance, path, MAX_PATH);
    char *slash = strrchr(path, '\\');
    if (slash) {
        slash[1] = 0;
        size_t n = strlen(path);
        if (n + 12 < MAX_PATH) strcpy(path + n, "libusbK.dll");
        m_hLib = LoadLibraryA(path);
    }
    if (!m_hLib) m_hLib = LoadLibraryA("libusbK.dll");
    if (!m_hLib) return false;

#define DAWN_RESOLVE(fn) \
    m_p##fn = (PFN_##fn)GetProcAddress(m_hLib, #fn); \
    if (!m_p##fn) { UnloadLibusbK(); return false; }

    DAWN_RESOLVE(LstK_Init)
    DAWN_RESOLVE(LstK_FindByVidPid)
    DAWN_RESOLVE(LstK_Free)
    DAWN_RESOLVE(UsbK_Init)
    DAWN_RESOLVE(UsbK_Free)
    DAWN_RESOLVE(UsbK_SetAltInterface)
    DAWN_RESOLVE(UsbK_ControlTransfer)
    DAWN_RESOLVE(UsbK_IsoReadPipe)
    DAWN_RESOLVE(UsbK_IsoWritePipe)
    DAWN_RESOLVE(UsbK_GetCurrentFrameNumber)
    DAWN_RESOLVE(UsbK_GetOverlappedResult)
    DAWN_RESOLVE(IsoK_Init)
    DAWN_RESOLVE(IsoK_ReUse)
    DAWN_RESOLVE(IsoK_SetPacket)
    DAWN_RESOLVE(IsoK_Free)

#undef DAWN_RESOLVE
    return true;
}

void CDawnPro2Asio::UnloadLibusbK()
{
    m_pLstK_Init = NULL;
    m_pLstK_FindByVidPid = NULL;
    m_pLstK_Free = NULL;
    m_pUsbK_Init = NULL;
    m_pUsbK_Free = NULL;
    m_pUsbK_SetAltInterface = NULL;
    m_pUsbK_ControlTransfer = NULL;
    m_pUsbK_IsoReadPipe = NULL;
    m_pUsbK_IsoWritePipe = NULL;
    m_pUsbK_GetCurrentFrameNumber = NULL;
    m_pUsbK_GetOverlappedResult = NULL;
    m_pIsoK_Init = NULL;
    m_pIsoK_ReUse = NULL;
    m_pIsoK_SetPacket = NULL;
    m_pIsoK_Free = NULL;
    if (m_hLib) { FreeLibrary(m_hLib); m_hLib = NULL; }
}

bool CDawnPro2Asio::OpenDevice()
{
    if (m_deviceOpen) return true;
    if (!LoadLibusbK()) return false;

    KLST_HANDLE list = NULL;
    if (!m_pLstK_Init(&list, 0)) return false;

    KLST_DEVINFO_HANDLE info = NULL;
    if (!m_pLstK_FindByVidPid(list, 0x35D8, 0x011D, &info)) {
        m_pLstK_Free(list);
        return false;
    }

    KUSB_HANDLE h = NULL;
    if (!m_pUsbK_Init(&h, info)) {
        m_pLstK_Free(list);
        return false;
    }

    m_kList = list;
    m_kUsb = h;
    m_deviceOpen = true;
    return true;
}

void CDawnPro2Asio::CloseDevice()
{
    if (m_kUsb) { m_pUsbK_Free(m_kUsb); m_kUsb = NULL; }
    if (m_kList) { m_pLstK_Free(m_kList); m_kList = NULL; }
    m_deviceOpen = false;
}

bool CDawnPro2Asio::SetSampleFrequency(ULONG freq)
{
    if (!m_kUsb) return false;

    // UAC2 Clock Source Frequency SET_CUR (validated with libusbK on the device)
    D2_SETUP_PACKET pkt;
    pkt.RequestType = 0x21;
    pkt.Request = 0x01;
    pkt.Value = 0x0100;
    pkt.Index = 0x0101;
    pkt.Length = 4;

    ULONG value = freq;
    UINT transferred = 0;
    return m_pUsbK_ControlTransfer(m_kUsb, pkt, (PUCHAR)&value, 4, &transferred, NULL) == TRUE;
}

// ============================================================================
// IASIO Implementation
// ============================================================================

STDMETHODIMP_(ASIOBool) CDawnPro2Asio::init(void *sysHandle)
{
    m_hwnd = (HWND)sysHandle;
    OpenDevice();
    return ASIOTrue;
}

STDMETHODIMP_(void) CDawnPro2Asio::getDriverName(char *name)
{
    if (name) strcpy(name, DRIVER_NAME);
}

STDMETHODIMP_(long) CDawnPro2Asio::getDriverVersion()
{
    return 3;
}

STDMETHODIMP_(void) CDawnPro2Asio::getErrorMessage(char *string)
{
    if (string) {
        if (m_deviceOpen) strcpy(string, "No error.");
        else strcpy(string, "MOONDROP DAWN PRO 2 (libusbK) not present.");
    }
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::getChannels(long *numInputChannels, long *numOutputChannels)
{
    if (numInputChannels) *numInputChannels = 0;
    if (numOutputChannels) *numOutputChannels = 2;
    return ASE_OK;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::getLatencies(long *inputLatency, long *outputLatency)
{
    if (inputLatency) *inputLatency = 0;
    if (outputLatency) {
        *outputLatency = (long)(m_bufferSize + m_sampleRate * 0.005); // buffer + ~5 ms USB pipeline
    }
    return ASE_OK;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::getBufferSize(long *minSize, long *maxSize, long *preferredSize, long *granularity)
{
    if (minSize) *minSize = 64;
    if (maxSize) *maxSize = 65536;
    if (preferredSize) *preferredSize = m_isDSD ? 32768 : 2048;
    if (granularity) *granularity = 64;
    return ASE_OK;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::canSampleRate(ASIOSampleRate sampleRate)
{
    if (sampleRate == 44100.0 || sampleRate == 48000.0 ||
        sampleRate == 88200.0 || sampleRate == 96000.0 ||
        sampleRate == 176400.0 || sampleRate == 192000.0 ||
        sampleRate == 352800.0 || sampleRate == 384000.0) {
        return ASE_OK;
    }
    if (sampleRate == 2822400.0 || sampleRate == 5644800.0 || sampleRate == 11289600.0) {
        return ASE_OK; // DSD64 / DSD128 / DSD256
    }
    return ASE_NoClock;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::getSampleRate(ASIOSampleRate *sampleRate)
{
    if (sampleRate) *sampleRate = m_sampleRate;
    return ASE_OK;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::setSampleRate(ASIOSampleRate sampleRate)
{
    if (canSampleRate(sampleRate) == ASE_OK) {
        m_sampleRate = sampleRate;
        m_isDSD = (sampleRate >= 2822400.0);
        if (m_callbacks && m_callbacks->sampleRateDidChange) {
            m_callbacks->sampleRateDidChange(m_sampleRate);
        }
        return ASE_OK;
    }
    return ASE_NoClock;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::getClockSources(ASIOClockSource *clocks, long *numSources)
{
    if (!clocks || !numSources) return ASE_InvalidParameter;
    *numSources = 1;
    clocks[0].index = 0;
    clocks[0].associatedChannel = -1;
    clocks[0].associatedGroup = -1;
    clocks[0].isCurrentSource = ASIOTrue;
    strcpy(clocks[0].name, "Dawn Pro 2 Internal Clock");
    return ASE_OK;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::setClockSource(long reference)
{
    return (reference == 0) ? ASE_OK : ASE_NotPresent;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::getSamplePosition(ASIOSamples *sPos, ASIOTimeStamp *tStamp)
{
    if (sPos) {
        sPos->lo = (unsigned long)(m_samplePos & 0xFFFFFFFFULL);
        sPos->hi = (unsigned long)(m_samplePos >> 32);
    }
    if (tStamp) {
        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);
        tStamp->lo = qpc.LowPart;
        tStamp->hi = qpc.HighPart;
    }
    return ASE_OK;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::getChannelInfo(ASIOChannelInfo *info)
{
    if (!info) return ASE_InvalidParameter;
    if (info->isInput) return ASE_InvalidParameter;
    if (info->channel < 0 || info->channel >= 2) return ASE_InvalidParameter;

    info->isActive = ASIOTrue;
    info->channelGroup = 0;
    if (m_isDSD) {
        info->type = ASIOSTDSDInt8MSB1; // DSD 1 bit data, 8 samples per byte, first sample in MSB
    } else {
        DWORD pm = D2RegGet("PcmMode", 0);
        info->type = (pm == 1) ? ASIOSTInt16LSB : ASIOSTInt32LSB;
    }
    snprintf(info->name, sizeof(info->name), "Dawn Pro 2 %s", (info->channel == 0) ? "L" : "R");
    return ASE_OK;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::createBuffers(ASIOBufferInfo *bufferInfos, long numChannels, long bufferSize, ASIOCallbacks *callbacks)
{
    if (!bufferInfos || !callbacks || numChannels < 1) return ASE_InvalidParameter;
    if (bufferSize < 64) bufferSize = 64;
    if (bufferSize > 65536) bufferSize = 65536;

    disposeBuffers();
    m_bufferSize = bufferSize;
    m_callbacks = callbacks;
    m_bufferInfos = bufferInfos;
    m_bufferIndex = 0;
    m_samplePos = 0;

    size_t bytesPerBuffer = (size_t)m_bufferSize * 4;
    for (int ch = 0; ch < 2; ch++) {
        m_channelBuffers[ch][0] = malloc(bytesPerBuffer);
        m_channelBuffers[ch][1] = malloc(bytesPerBuffer);
        if (m_channelBuffers[ch][0]) memset(m_channelBuffers[ch][0], 0, bytesPerBuffer);
        if (m_channelBuffers[ch][1]) memset(m_channelBuffers[ch][1], 0, bytesPerBuffer);
    }

    for (long i = 0; i < numChannels; i++) {
        if (!bufferInfos[i].isInput && bufferInfos[i].channelNum < 2) {
            long ch = bufferInfos[i].channelNum;
            bufferInfos[i].buffers[0] = m_channelBuffers[ch][0];
            bufferInfos[i].buffers[1] = m_channelBuffers[ch][1];
        }
    }
    return ASE_OK;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::disposeBuffers()
{
    stop();
    for (int ch = 0; ch < 2; ch++) {
        if (m_channelBuffers[ch][0]) { free(m_channelBuffers[ch][0]); m_channelBuffers[ch][0] = NULL; }
        if (m_channelBuffers[ch][1]) { free(m_channelBuffers[ch][1]); m_channelBuffers[ch][1] = NULL; }
    }
    m_bufferInfos = NULL;
    m_callbacks = NULL;
    return ASE_OK;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::controlPanel()
{
    MessageBoxA(m_hwnd,
        "MOONDROP DAWN PRO 2 ASIO (libusbK KISO)\n"
        "Native DSD64/128/256 (alt setting 4) and PCM up to 384 kHz.\n"
        "Status: Ready",
        "Dawn Pro 2 Control Panel", MB_OK | MB_ICONINFORMATION);
    return ASE_OK;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::future(long selector, void *opt)
{
    switch (selector) {
        case kAsioCanDoIoFormat: {
            ASIOIoFormat *fmt = (ASIOIoFormat *)opt;
            if (fmt && (fmt->FormatType == kASIODSDFormat || fmt->FormatType == kASIOPCMFormat)) {
                return (ASIOError)ASE_SUCCESS;
            }
            return ASE_NotPresent;
        }
        case kAsioSetIoFormat: {
            ASIOIoFormat *fmt = (ASIOIoFormat *)opt;
            if (fmt) {
                if (fmt->FormatType == kASIODSDFormat) {
                    m_isDSD = true;
                    return (ASIOError)ASE_SUCCESS;
                } else if (fmt->FormatType == kASIOPCMFormat) {
                    m_isDSD = false;
                    return (ASIOError)ASE_SUCCESS;
                }
            }
            return ASE_NotPresent;
        }
        case kAsioGetIoFormat: {
            ASIOIoFormat *fmt = (ASIOIoFormat *)opt;
            if (fmt) {
                fmt->FormatType = m_isDSD ? kASIODSDFormat : kASIOPCMFormat;
                return (ASIOError)ASE_SUCCESS;
            }
            return ASE_NotPresent;
        }
    }
    return ASE_NotPresent;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::outputReady()
{
    return ASE_OK;
}

// ============================================================================
// Streaming Engine (libusbK KISO)
// ============================================================================

DWORD WINAPI CDawnPro2Asio::StreamingThreadProc(LPVOID lpParam)
{
    CDawnPro2Asio *pThis = (CDawnPro2Asio *)lpParam;
    pThis->m_streamThreadId = GetCurrentThreadId();
    pThis->StreamLoop();
    return 0;
}

void CDawnPro2Asio::StreamLoop()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    timeBeginPeriod(1);

    const UCHAR EP_OUT = 0x07;
    const UCHAR EP_FB  = 0x83;
    const ULONG MAXPKT = DAWN_MAXPKT;
    const ULONG URBBUF = (ULONG)DAWN_NP * MAXPKT;
    const bool dsd = m_isDSD;

    // Format knobs (editable via HKCU\Software\MOONDROP DAWN PRO 2 ASIO)
    int dsdRev = (int)(D2RegGet("DsdRev", 0) ? 1 : 0);
    int dsdSwap = (int)(D2RegGet("DsdSwap", 0) ? 1 : 0);
    int pcmMode = (int)D2RegGet("PcmMode", 0);     // 1 = 16-bit wire, 0 = 32-bit wire
    if (pcmMode != 0 && pcmMode != 1) pcmMode = 0;
    int pcmAlt = (int)D2RegGet("PcmAlt", 3);       // 1 = S16 alt, 3 = S32 alt
    if (pcmAlt < 1 || pcmAlt > 3) pcmAlt = 3;

    int revBits = dsd ? dsdRev : 0;
    int swapLR = dsd ? dsdSwap : 0;
    ULONG paceUnit = dsd ? 8 : (pcmMode == 1 ? 4 : 8);

    // Pacing: F = Q16.16 units (8-byte stereo frames) per USB frame (microframe row).
    // DSD256: 44.1 frames/uframe -> 2890137/2890138; the device feedback reports this directly.
    // PCM: samples/uframe = rate/8000 (44.1k -> 5.5125 -> packets of 5/6 samples = 40/48 B)
    uint32_t nominalF = dsd ? (uint32_t)(m_sampleRate * 0.256 + 0.5)
                            : (uint32_t)(m_sampleRate * 8.192 + 0.5);
    if (nominalF == 0) nominalF = 1;
    uint32_t F = nominalF;
    uint64_t acc = 0;

    // Bytes consumed per ASIO buffer per channel
    ULONG inPerCh = dsd ? (ULONG)(m_bufferSize / DAWN_DSD_SAMPLES_PER_BYTE)
                        : (ULONG)(m_bufferSize * (pcmMode == 1 ? 2 : 4));
    ULONG pullOut = inPerCh * 2;

    DWORD enableLog = D2RegGet("EnableLog", 0);
    if (enableLog) {
        char tempPath[MAX_PATH];
        DWORD tlen = GetTempPathA(MAX_PATH, tempPath);
        if (tlen > 0 && tlen < MAX_PATH - 32) {
            strcat(tempPath, "dawn_pro2_asio.log");
            g_log = fopen(tempPath, "w");
        }
    }
    D2LOG("=== StreamLoop dsd=%d rate=%.0f bufferSize=%ld F=%u inPerCh=%u pullOut=%u rev=%d swap=%d pm=%d palt=%d\n",
          dsd ? 1 : 0, m_sampleRate, m_bufferSize, F, inPerCh, pullOut,
          revBits, swapLR, pcmMode, pcmAlt);

    BYTE  *staging = (BYTE*)malloc(pullOut + 64);
    // Driver-side jitter buffer: ~100 ms of wire data (absorbs ASIO host jitter)
    ULONG  preLevel = dsd ? (ULONG)(2822400.0 * 0.10) : (ULONG)(m_sampleRate * 8.0 * 0.10);
    ULONG  remCap  = preLevel + pullOut + URBBUF + 8192;
    BYTE  *rem     = (BYTE*)malloc(remCap);
    ULONG  remLen  = 0;

    struct Urb {
        PKISO_CONTEXT ctx;
        OVERLAPPED    ov;
        HANDLE        evt;
        BYTE         *buf;
        ULONG         len;
        ULONG         offs[DAWN_NP];
        bool          inFlight;
    };

    Urb *urbs = (Urb*)calloc(DAWN_RING, sizeof(Urb));
    Urb *fbu  = (Urb*)calloc(1, sizeof(Urb));

    bool ok = (staging != NULL) && (rem != NULL) && (urbs != NULL) && (fbu != NULL) && m_kUsb;

    for (int i = 0; ok && i < DAWN_RING; i++) {
        urbs[i].ctx = NULL;
        ok = m_pIsoK_Init(&urbs[i].ctx, DAWN_NP, -1) == TRUE;
        urbs[i].buf = (BYTE*)malloc(URBBUF);
        memset(&urbs[i].ov, 0, sizeof(OVERLAPPED));
        urbs[i].evt = CreateEventA(NULL, TRUE, FALSE, NULL);
        urbs[i].ov.hEvent = urbs[i].evt;
        urbs[i].inFlight = false;
        if (!urbs[i].ctx || !urbs[i].buf || !urbs[i].evt) ok = false;
    }
    if (ok) {
        fbu->ctx = NULL;
        ok = m_pIsoK_Init(&fbu->ctx, DAWN_FBNP, -1) == TRUE;
        fbu->buf = (BYTE*)malloc(DAWN_FBNP * 4);
        memset(&fbu->ov, 0, sizeof(OVERLAPPED));
        fbu->evt = CreateEventA(NULL, TRUE, FALSE, NULL);
        fbu->ov.hEvent = fbu->evt;
        fbu->inFlight = false;
        if (!fbu->ctx || !fbu->buf || !fbu->evt) ok = false;
    }

    // Hardware setup: alt 4 = raw DSD, alt 3 = PCM 32-bit; then clock
    if (ok) {
        UCHAR alt = dsd ? 4 : (UCHAR)pcmAlt;
        BOOL altOk = m_pUsbK_SetAltInterface(m_kUsb, 1, TRUE, alt);
        if (!altOk) altOk = m_pUsbK_SetAltInterface(m_kUsb, 2, FALSE, alt);
        ULONG clockFreq = dsd ? (ULONG)(m_sampleRate / 32.0) : (ULONG)m_sampleRate;
        BOOL clkOk = SetSampleFrequency(clockFreq);
        D2LOG("alt=%d altOk=%d clockFreq=%u clkOk=%d\n", alt, altOk, clockFreq, clkOk);
    }

    ULONG nextFrame = 0;
    {
        UINT f = 0;
        BOOL fr = m_pUsbK_GetCurrentFrameNumber(m_kUsb, &f);
        if (fr) nextFrame = (ULONG)f;
        nextFrame += DAWN_LEAD;
        D2LOG("frameRead ret=%d val=%u nextFrame=%u\n", fr, f, nextFrame);
    }
    ULONG fbNextFrame = nextFrame;

    // ---- stream helpers ----

    // Pull one ASIO buffer, convert to wire format, append to rem[]
    // UAC2 Format:
    // Raw DSD (AltSetting 4): 2 subslots of 4 bytes (32-bit) = 8 bytes per frame
    // 32-bit PCM (AltSetting 3): 2 subslots of 4 bytes (32-bit) = 8 bytes per frame
    // 16-bit PCM (AltSetting 1): 2 subslots of 2 bytes (16-bit) = 4 bytes per frame
    auto pull = [&]() -> bool {
        if (!m_callbacks || !m_callbacks->bufferSwitch) return false;
        if (WaitForSingleObject(m_hStopEvent, 0) != WAIT_TIMEOUT) return false;
        const BYTE *L = (const BYTE*)m_channelBuffers[0][m_bufferIndex];
        const BYTE *R = (const BYTE*)m_channelBuffers[1][m_bufferIndex];
        m_callbacks->bufferSwitch(m_bufferIndex, ASIOTrue);
        m_bufferIndex ^= 1;
        m_samplePos += (uint64_t)m_bufferSize;
        if (!L || !R || inPerCh == 0) return false;
        BYTE *out = staging;
        ULONG outLen = 0;

        if (dsd) {
            // Raw DSD (AltSetting 4): 32-bit subslots (4 bytes per channel per frame, 8 bytes total)
            ULONG nFrames = inPerCh / 4;
            for (ULONG g = 0; g < nFrames; g++) {
                const BYTE *ls = swapLR ? (R + g * 4) : (L + g * 4);
                const BYTE *rs = swapLR ? (L + g * 4) : (R + g * 4);

                // Left Subslot: 4 contiguous bytes
                for (int k = 0; k < 4; k++) {
                    *out++ = revBits ? D2Rev8(ls[k]) : ls[k];
                }
                // Right Subslot: 4 contiguous bytes
                for (int k = 0; k < 4; k++) {
                    *out++ = revBits ? D2Rev8(rs[k]) : rs[k];
                }
            }
            outLen = nFrames * 8;
        } else if (pcmMode == 1) {
            // 16-bit PCM (AltSetting 1): 16-bit subslots (2 bytes per channel per frame, 4 bytes total)
            const uint16_t *pL = (const uint16_t*)L;
            const uint16_t *pR = (const uint16_t*)R;
            uint16_t *dst = (uint16_t*)out;
            ULONG nSamples = inPerCh / 2;
            for (ULONG s = 0; s < nSamples; s++) {
                dst[s * 2 + 0] = swapLR ? pR[s] : pL[s];
                dst[s * 2 + 1] = swapLR ? pL[s] : pR[s];
            }
            outLen = nSamples * 4;
        } else {
            // 32-bit PCM (AltSetting 3): 32-bit subslots (4 bytes per channel per frame, 8 bytes total)
            const uint32_t *pL = (const uint32_t*)L;
            const uint32_t *pR = (const uint32_t*)R;
            uint32_t *dst = (uint32_t*)out;
            ULONG nSamples = inPerCh / 4;
            for (ULONG s = 0; s < nSamples; s++) {
                dst[s * 2 + 0] = swapLR ? pR[s] : pL[s];
                dst[s * 2 + 1] = swapLR ? pL[s] : pR[s];
            }
            outLen = nSamples * 8;
        }

        if (remLen + outLen > remCap) outLen = remCap - remLen;
        if (outLen) {
            memcpy(rem + remLen, staging, outLen);
            remLen += outLen;
        }
        return true;
    };

    // Compute per-frame packet sizes (feedback paced) and fill buffer from rem[]
    auto fillUrb = [&](Urb *u) {
        u->len = 0;
        for (int p = 0; p < DAWN_NP; p++) {
            acc += F;
            ULONG n = (ULONG)(acc >> 16);
            acc -= ((uint64_t)n) << 16;
            ULONG nb = n * paceUnit;       // whole stereo frames only
            if (nb > MAXPKT) nb = MAXPKT;
            u->offs[p] = u->len;
            u->len += nb;
        }
        ULONG need = u->len, off = 0;
        while (need > 0) {
            if (remLen == 0) {
                if (!pull()) {
                    memset(u->buf + off, dsd ? 0x69 : 0x00, need); // DSD or PCM silence fallback
                    break;
                }
                if (remLen == 0) break;
            }
            ULONG take = (need < remLen) ? need : remLen;
            memcpy(u->buf + off, rem, take);
            off += take;
            need -= take;
            remLen -= take;
            memmove(rem, rem + take, remLen);
        }
    };

    auto submitUrb = [&](Urb *u) -> bool {
        UINT cur = 0;
        BOOL hasCur = m_pUsbK_GetCurrentFrameNumber(m_kUsb, &cur);
        ULONG s = nextFrame;
        if (hasCur && (int)(s - cur) < 2) s = (ULONG)cur + DAWN_LEAD; // resync
        m_pIsoK_ReUse(u->ctx);
        D2_KISO_CONTEXT *cx = (D2_KISO_CONTEXT*)u->ctx;
        cx->Flags = D2_KISO_FLAG_SET_START_FRAME;
        cx->StartFrame = s;
        for (int p = 0; p < DAWN_NP; p++) {
            D2_KISO_PACKET pk;
            pk.Offset = u->offs[p];
            pk.Length = 0;
            pk.Status = 0;
            m_pIsoK_SetPacket(u->ctx, p, &pk);
        }
        ResetEvent(u->evt);
        SetLastError(0);
        BOOL r = m_pUsbK_IsoWritePipe(m_kUsb, EP_OUT, u->buf, u->len, &u->ov, u->ctx);
        D2LOG("submit buf=%p s=%u cur=%u len=%u ret=%d err=%lu\n", u->buf, s, cur, u->len, (int)r, GetLastError());
        if (!r && GetLastError() != ERROR_IO_PENDING) {
            u->inFlight = false;
            return false;
        }
        u->inFlight = true;
        nextFrame = s + DAWN_OUT_PERIOD;
        return true;
    };

    auto submitFb = [&]() {
        UINT cur = 0;
        BOOL hasCur = m_pUsbK_GetCurrentFrameNumber(m_kUsb, &cur);
        ULONG s = fbNextFrame;
        if (hasCur && (int)(s - cur) < 2) s = (ULONG)cur + DAWN_LEAD;
        m_pIsoK_ReUse(fbu->ctx);
        D2_KISO_CONTEXT *cx = (D2_KISO_CONTEXT*)fbu->ctx;
        cx->Flags = D2_KISO_FLAG_SET_START_FRAME;
        cx->StartFrame = s;
        for (int p = 0; p < DAWN_FBNP; p++) {
            D2_KISO_PACKET pk;
            pk.Offset = p * 4;
            pk.Length = 0;
            pk.Status = 0;
            m_pIsoK_SetPacket(fbu->ctx, p, &pk);
        }
        ResetEvent(fbu->evt);
        SetLastError(0);
        BOOL r = m_pUsbK_IsoReadPipe(m_kUsb, EP_FB, fbu->buf, DAWN_FBNP * 4, &fbu->ov, fbu->ctx);
        if (!r && GetLastError() != ERROR_IO_PENDING) {
            fbu->inFlight = false;
            return;
        }
        fbu->inFlight = true;
        fbNextFrame = s + DAWN_FB_PERIOD;
    };

    // ---- main loop ----
    int submitFails = 0;

    if (ok) {
        for (int i = 0; i < DAWN_RING; i++) {
            fillUrb(&urbs[i]);
            if (!submitUrb(&urbs[i])) { submitFails++; break; }
        }
        submitFb();

        while (WaitForSingleObject(m_hStopEvent, 0) == WAIT_TIMEOUT) {
            HANDLE evs[DAWN_RING];
            int map[DAWN_RING];
            int n = 0;
            for (int i = 0; i < DAWN_RING; i++) {
                if (urbs[i].inFlight) {
                    evs[n] = urbs[i].evt;
                    map[n] = i;
                    n++;
                }
            }
            if (n == 0) break;

            DWORD w = WaitForMultipleObjects((DWORD)n, evs, FALSE, 200);
            Urb *done = NULL;
            if (w >= WAIT_OBJECT_0 && w < WAIT_OBJECT_0 + (DWORD)n) {
                done = &urbs[map[w - WAIT_OBJECT_0]];
                UINT xfer = 0;
                m_pUsbK_GetOverlappedResult(m_kUsb, &done->ov, &xfer, FALSE);
                done->inFlight = false;
                D2_KISO_CONTEXT *cx = (D2_KISO_CONTEXT*)done->ctx;
                D2LOG("done buf=%p errs=%d hdr=0x%08X xfer=%u\n", done->buf, cx->ErrorCount, cx->UrbHdrStatus, xfer);
                if (cx->ErrorCount != 0 || cx->UrbHdrStatus != 0)
                    InterlockedIncrement(&m_glitchCount);
            }

            // feedback housekeeping (non-blocking)
            if (fbu->inFlight && WaitForSingleObject(fbu->evt, 0) == WAIT_OBJECT_0) {
                UINT x = 0;
                m_pUsbK_GetOverlappedResult(m_kUsb, &fbu->ov, &x, FALSE);
                fbu->inFlight = false;
                D2_KISO_PACKET *fp = (D2_KISO_PACKET*)((BYTE*)fbu->ctx + offsetof(D2_KISO_CONTEXT, IsoPackets));
                for (int i = DAWN_FBNP - 1; i >= 0; i--) {
                    if (fp[i].Length == 4 && fp[i].Status == 0) {
                        uint32_t v = 0;
                        memcpy(&v, fbu->buf + i * 4, 4);
                        if (v > nominalF / 2 && v < nominalF * 2) { F = v; D2LOG("fbF=%u\n", v); }
                        break;
                    }
                }
            }
            if (!fbu->inFlight) submitFb();

            // top up the jitter buffer
            {
                int guard = 0;
                while (remLen < preLevel && guard++ < 64) {
                    if (!pull()) break;
                }
            }

            if (done) {
                if (WaitForSingleObject(m_hStopEvent, 0) != WAIT_TIMEOUT) break;
                fillUrb(done);
                if (submitUrb(done)) submitFails = 0;
                else if (++submitFails > 20) break;
            }
        }
    }

    // ---- teardown ----
    if (urbs) {
        for (int i = 0; i < DAWN_RING; i++) {
            if (urbs[i].inFlight) {
                WaitForSingleObject(urbs[i].evt, 500);
                urbs[i].inFlight = false;
            }
            if (urbs[i].ctx) m_pIsoK_Free(urbs[i].ctx);
            if (urbs[i].buf) free(urbs[i].buf);
            if (urbs[i].evt) CloseHandle(urbs[i].evt);
        }
    }
    if (fbu) {
        if (fbu->inFlight) WaitForSingleObject(fbu->evt, 500);
        if (fbu->ctx) m_pIsoK_Free(fbu->ctx);
        if (fbu->buf) free(fbu->buf);
        if (fbu->evt) CloseHandle(fbu->evt);
    }
    free(urbs);
    free(fbu);
    free(staging);
    free(rem);

    if (m_kUsb) m_pUsbK_SetAltInterface(m_kUsb, 1, TRUE, 0);

    D2LOG("teardown\n");
    if (g_log) { fclose(g_log); g_log = NULL; }

    timeEndPeriod(1);
}

// ============================================================================
// Start / Stop
// ============================================================================

STDMETHODIMP_(ASIOError) CDawnPro2Asio::start()
{
    if (m_active) return ASE_OK;

    if (!m_deviceOpen) {
        if (!OpenDevice()) return ASE_NotPresent;
    }
    if (m_bufferSize <= 0) m_bufferSize = 2048;
    m_bufferIndex = 0;
    m_samplePos = 0;

    m_hStopEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!m_hStopEvent) return ASE_InvalidMode;

    m_active = true;
    m_hThread = CreateThread(NULL, 0, StreamingThreadProc, this, 0, NULL);
    if (!m_hThread) {
        CloseHandle(m_hStopEvent);
        m_hStopEvent = NULL;
        m_active = false;
        return ASE_InvalidMode;
    }
    return ASE_OK;
}

STDMETHODIMP_(ASIOError) CDawnPro2Asio::stop()
{
    if (!m_active) return ASE_OK;
    m_active = false;

    if (m_hStopEvent) SetEvent(m_hStopEvent);

    if (m_hThread) {
        if (GetCurrentThreadId() != m_streamThreadId) {
            WaitForSingleObject(m_hThread, 3000);
            CloseHandle(m_hThread);
            m_hThread = NULL;
        }
    }
    if (m_hStopEvent && GetCurrentThreadId() != m_streamThreadId) {
        CloseHandle(m_hStopEvent);
        m_hStopEvent = NULL;
    }
    return ASE_OK;
}

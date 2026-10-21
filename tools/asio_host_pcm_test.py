# asio_host_pcm_test.py - PCM segment test via our ASIO driver
import sys, ctypes, math, time, struct, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.argv = ['x']
import asio_host_test as A
from ctypes import c_long, c_void_p, c_double, c_ubyte

RATE = 44100.0
BUFSIZE = 2048
MODE = sys.argv[1] if len(sys.argv) > 1 else 'seg'
TOTAL = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0
SEGLEN = 2.0
SEGS = [(1.0, 0.0), (0.0, 1.0), (1.0, 1.0), (0.0, 0.0)] if MODE == 'seg' else [(1.0, 1.0)]
FREQS = [400.0, 3000.0] if MODE == 'cont' else [1000.0, 1000.0]
AMP = 0.5

def main():
    dll = ctypes.WinDLL(A.DLL)
    dll.DllGetClassObject.restype = c_long
    dll.DllGetClassObject.argtypes = [c_void_p, c_void_p, ctypes.POINTER(c_void_p)]
    clsid = A.GUID(0xB9B1E0A1, 0x35D8, 0x011D, (c_ubyte * 8)(0x80, 0, 0, 0, 0, 0, 0, 1))
    iunk = A.GUID(0, 0, 0, (c_ubyte * 8)(0xC0, 0, 0, 0, 0, 0, 0, 0x46))
    iasio_iid = A.GUID(0x254CD580, 0x08A0, 0x11D1, (c_ubyte * 8)(0xA1, 0x20, 0x00, 0x20, 0xAF, 0xD7, 0x72, 0x42))
    factory = c_void_p()
    hr = dll.DllGetClassObject(ctypes.byref(clsid), ctypes.byref(iunk), ctypes.byref(factory))
    if hr != 0: return 2
    create = A.vfn(factory, 3, c_long, c_void_p, c_void_p, ctypes.POINTER(c_void_p))
    iasio = c_void_p()
    hr = create(factory, None, ctypes.byref(iasio_iid), ctypes.byref(iasio))
    if hr != 0: return 2
    init = A.vfn(iasio, 3, c_long, c_void_p)
    setSampleRate = A.vfn(iasio, 14, c_long, c_double)
    getChannelInfo = A.vfn(iasio, 18, c_long, ctypes.POINTER(A.ASIOChannelInfo))
    createBuffers = A.vfn(iasio, 19, c_long, ctypes.POINTER(A.ASIOBufferInfo), c_long, c_long, ctypes.POINTER(A.ASIOCallbacks))
    disposeBuffers = A.vfn(iasio, 20, c_long)
    start = A.vfn(iasio, 7, c_long)
    stop = A.vfn(iasio, 8, c_long)
    future = A.vfn(iasio, 22, c_long, c_long, c_void_p)
    r = init(iasio, None)
    print('init -> %d' % r, flush=True)
    if r != 1: return 2
    print('setSampleRate(44100) -> %d' % setSampleRate(iasio, RATE), flush=True)
    fo = A.ASIOIoFormat(); fo.FormatType = 0
    r = future(iasio, 0x23111961, ctypes.byref(fo))
    print('future(kAsioSetIoFormat=PCM) -> 0x%08X' % (r & 0xFFFFFFFF), flush=True)
    ci = A.ASIOChannelInfo()
    for ch in (0, 1):
        ctypes.memset(ctypes.byref(ci), 0, ctypes.sizeof(ci))
        ci.channel = ch
        getChannelInfo(iasio, ctypes.byref(ci))
        print('getChannelInfo(%d) type=%d (expect 18=Int32LSB or 16=Int16LSB)' % (ch, ci.type), flush=True)
    type16 = (ci.type == 16)
    print('encoding: %s' % ('Int16' if type16 else 'Int32'), flush=True)
    state = {'pos': 0, 'calls': 0, 'infos': None}
    def vals_for(ch, k0, n):
        out = []; segs_per = RATE * SEGLEN
        tw = 2.0 * math.pi * FREQS[ch] / RATE
        for i in range(n):
            k = k0 + i
            seg = int(k / segs_per)
            if seg > len(SEGS) - 1: seg = len(SEGS) - 1
            a = SEGS[seg][ch]
            out.append(int(AMP * a * math.sin(tw * k) * 2147483647.0) if a else 0)
        if type16:
            return struct.pack('<%dh' % n, *[v >> 16 for v in out])
        return struct.pack('<%di' % n, *out)
    def cb_switch(idx, direct):
        infos = state['infos']
        if infos is None: return
        k0 = state['pos']
        for ch in (0, 1):
            dst = infos[ch].buffers[idx]
            if dst:
                ctypes.memmove(dst, vals_for(ch, k0, BUFSIZE), BUFSIZE * (2 if type16 else 4))
        state['pos'] += BUFSIZE
        state['calls'] += 1
    cbs = A.ASIOCallbacks()
    cbs.bufferSwitch = A.BUF_SWITCH(cb_switch)
    cbs.sampleRateDidChange = A.SR_CHANGE(lambda r: None)
    cbs.asioMessage = A.MSG_FN(lambda s, v, m, o: 0)
    cbs.bufferSwitchTimeInfo = A.BSTI_FN()
    infos = (A.ASIOBufferInfo * 2)()
    infos[0].isInput = 0; infos[0].channelNum = 0
    infos[1].isInput = 0; infos[1].channelNum = 1
    state['infos'] = infos
    r = createBuffers(iasio, infos, 2, BUFSIZE, ctypes.byref(cbs))
    print('createBuffers -> %d' % r, flush=True)
    if r != 0: return 3
    r = start(iasio)
    print('start -> %d  SEGMENTS: [1] L-only  [2] R-only  [3] both  [4] silence (2s each)' % r, flush=True)
    t0 = time.time()
    while time.time() - t0 < TOTAL:
        time.sleep(2.0)
    stop(iasio)
    disposeBuffers(iasio)
    print('DONE PCM calls=%d' % state['calls'], flush=True)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())

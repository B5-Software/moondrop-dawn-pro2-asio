# asio_host_test.py - minimal ASIO host for dawn_pro2_asio.dll (libusbK transport)
# Plays a DSF file (L = tone, R = silence) for N seconds through the driver.
# usage: asio_host_test.py [dsf_path] [seconds]
import ctypes, struct, sys, time, os
from ctypes import c_long, c_void_p, c_double, c_char_p, c_ubyte, c_ulong, c_char

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DLL = os.path.join(BASE_DIR, 'dawn_pro2_asio.dll')
DSF = sys.argv[1] if len(sys.argv) > 1 else os.path.join(BASE_DIR, 'test', 'sine1k_dsd256_L.dsf')
DUR = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0

ASE_OK = 0
ASE_SUCCESS = 0x3F4847A0
SUCCESS = (ASE_OK, ASE_SUCCESS)
kAsioSetIoFormat = 0x23111961
kASIODSDFormat = 1

def bitrev8(b):
    r = 0
    for i in range(8):
        r |= ((b >> i) & 1) << (7 - i)
    return r
REV = [bitrev8(i) for i in range(256)]

def le32(b): return struct.unpack('<I', b)[0]
def le64(b): return struct.unpack('<Q', b)[0]

def load_dsf(path):
    data = open(path, 'rb').read()
    assert data[:4] == b'DSD ', 'not a DSF'
    assert data[28:32] == b'fmt ', 'no fmt chunk'
    ver = le32(data[40:44]); fmtid = le32(data[44:48])
    chtype = le32(data[48:52]); chnum = le32(data[52:56]); rate = le32(data[56:60])
    bps = le32(data[60:64]); scount = le64(data[64:72]); block = le32(data[72:76])
    assert data[80:84] == b'data', 'no data chunk'
    payload = data[92:]
    nbytes = scount // 8
    L = bytearray(); R = bytearray()
    nblocks = (nbytes + block - 1) // block
    for b in range(nblocks):
        off = b * 2 * block
        L += payload[off:off + block]
        R += payload[off + block:off + 2 * block]
    L = bytes(L[:nbytes]); R = bytes(R[:nbytes])
    if bps == 1:
        L = bytes(REV[x] for x in L)
        R = bytes(REV[x] for x in R)
    print('DSF: rate=%d ch=%d bits=%d samples/ch=%d  (%d bytes/ch)' % (rate, chnum, bps, scount, nbytes))
    return rate, L, R

class GUID(ctypes.Structure):
    _fields_ = [('d1', c_ulong), ('d2', ctypes.c_ushort), ('d3', ctypes.c_ushort), ('d4', c_ubyte * 8)]

class ASIOBufferInfo(ctypes.Structure):
    _fields_ = [('isInput', c_long), ('channelNum', c_long), ('buffers', c_void_p * 2)]

class ASIOChannelInfo(ctypes.Structure):
    _fields_ = [('channel', c_long), ('isInput', c_long), ('isActive', c_long),
                ('channelGroup', c_long), ('type', c_long), ('name', c_char * 32)]

class ASIOIoFormat(ctypes.Structure):
    _fields_ = [('FormatType', c_long), ('future', c_char * 508)]

class ASIOSamples(ctypes.Structure):
    _fields_ = [('lo', c_ulong), ('hi', c_ulong)]

BUF_SWITCH = ctypes.WINFUNCTYPE(None, c_long, c_long)
SR_CHANGE = ctypes.WINFUNCTYPE(None, c_double)
MSG_FN = ctypes.WINFUNCTYPE(c_long, c_long, c_long, c_void_p, ctypes.POINTER(c_double))
BSTI_FN = ctypes.WINFUNCTYPE(c_void_p, c_void_p, c_long, c_long)

class ASIOCallbacks(ctypes.Structure):
    _fields_ = [('bufferSwitch', BUF_SWITCH), ('sampleRateDidChange', SR_CHANGE),
                ('asioMessage', MSG_FN), ('bufferSwitchTimeInfo', BSTI_FN)]

def vfn(inst, idx, restype, *argtypes):
    vt = ctypes.cast(inst, ctypes.POINTER(c_void_p))[0]
    addr = ctypes.cast(vt, ctypes.POINTER(c_void_p))[idx]
    proto = ctypes.WINFUNCTYPE(restype, c_void_p, *argtypes)
    return proto(addr)

def main():
    rate, L, R = load_dsf(DSF)
    n = len(L)

    dll = ctypes.WinDLL(DLL)
    dll.DllGetClassObject.restype = c_long
    dll.DllGetClassObject.argtypes = [c_void_p, c_void_p, ctypes.POINTER(c_void_p)]

    clsid = GUID(0xB9B1E0A1, 0x35D8, 0x011D, (c_ubyte * 8)(0x80, 0, 0, 0, 0, 0, 0, 1))
    iunk = GUID(0, 0, 0, (c_ubyte * 8)(0xC0, 0, 0, 0, 0, 0, 0, 0x46))
    iasio_iid = GUID(0x254CD580, 0x08A0, 0x11D1, (c_ubyte * 8)(0xA1, 0x20, 0x00, 0x20, 0xAF, 0xD7, 0x72, 0x42))

    factory = c_void_p()
    hr = dll.DllGetClassObject(ctypes.byref(clsid), ctypes.byref(iunk), ctypes.byref(factory))
    print('DllGetClassObject hr=0x%08X' % (hr & 0xFFFFFFFF))
    if hr != 0 or not factory.value:
        return 2
    create = vfn(factory, 3, c_long, c_void_p, c_void_p, ctypes.POINTER(c_void_p))
    iasio = c_void_p()
    hr = create(factory, None, ctypes.byref(iasio_iid), ctypes.byref(iasio))
    print('CreateInstance hr=0x%08X ptr=%s' % (hr & 0xFFFFFFFF, hex(iasio.value or 0)))
    if hr != 0 or not iasio.value:
        return 2

    init = vfn(iasio, 3, c_long, c_void_p)
    getDriverName = vfn(iasio, 4, None, c_char_p)
    getDriverVersion = vfn(iasio, 5, c_long)
    getChannels = vfn(iasio, 9, c_long, ctypes.POINTER(c_long), ctypes.POINTER(c_long))
    getBufferSize = vfn(iasio, 11, c_long, ctypes.POINTER(c_long), ctypes.POINTER(c_long), ctypes.POINTER(c_long), ctypes.POINTER(c_long))
    canSampleRate = vfn(iasio, 12, c_long, c_double)
    setSampleRate = vfn(iasio, 14, c_long, c_double)
    getSamplePosition = vfn(iasio, 17, c_long, ctypes.POINTER(ASIOSamples), c_void_p)
    getChannelInfo = vfn(iasio, 18, c_long, ctypes.POINTER(ASIOChannelInfo))
    createBuffers = vfn(iasio, 19, c_long, ctypes.POINTER(ASIOBufferInfo), c_long, c_long, ctypes.POINTER(ASIOCallbacks))
    disposeBuffers = vfn(iasio, 20, c_long)
    start = vfn(iasio, 7, c_long)
    stop = vfn(iasio, 8, c_long)
    future = vfn(iasio, 22, c_long, c_long, c_void_p)

    r = init(iasio, None)
    print('init -> %d' % r)
    if r != 1:
        return 2
    name = ctypes.create_string_buffer(64)
    getDriverName(iasio, name)
    print('driver: %s  version: %d' % (name.value.decode(), getDriverVersion(iasio)))

    nin = c_long(); nout = c_long()
    getChannels(iasio, ctypes.byref(nin), ctypes.byref(nout))
    print('channels: %d in / %d out' % (nin.value, nout.value))

    for sr in (44100.0, 384000.0, 2822400.0, 5644800.0, 11289600.0, 999.0):
        print('  canSampleRate(%9.0f) -> %d' % (sr, canSampleRate(iasio, sr)))

    r = setSampleRate(iasio, float(rate))
    print('setSampleRate(%d) -> %d' % (rate, r))

    fo = ASIOIoFormat(); fo.FormatType = kASIODSDFormat
    r = future(iasio, kAsioSetIoFormat, ctypes.byref(fo))
    print('future(kAsioSetIoFormat=DSD) -> 0x%08X fmt=%d' % (r & 0xFFFFFFFF, fo.FormatType))

    ci = ASIOChannelInfo()
    for ch in (0, 1):
        ctypes.memset(ctypes.byref(ci), 0, ctypes.sizeof(ci))
        ci.channel = ch
        r = getChannelInfo(iasio, ctypes.byref(ci))
        print('getChannelInfo(%d) -> %d type=%d name=%s' % (ch, r, ci.type, ci.name.decode()))

    mi = c_long(); ma = c_long(); pr = c_long(); gr = c_long()
    getBufferSize(iasio, ctypes.byref(mi), ctypes.byref(ma), ctypes.byref(pr), ctypes.byref(gr))
    print('bufferSize: min=%d max=%d preferred=%d gran=%d' % (mi.value, ma.value, pr.value, gr.value))

    BUFSIZE = 32768
    per_ch = BUFSIZE // 8

    state = {'pos': 0, 'calls': 0, 'infos': None, 'stop': False}

    def cb_switch(idx, direct):
        infos = state['infos']
        if infos is None:
            return
        pos = state['pos']
        for ch, arr in ((0, L), (1, R)):
            dst = infos[ch].buffers[idx]
            if not dst:
                continue
            end = pos + per_ch
            if end <= n:
                ctypes.memmove(dst, arr[pos:end], per_ch)
            else:
                first = n - pos
                ctypes.memmove(dst, arr[pos:], first)
                ctypes.memmove(dst + first, arr[0:per_ch - first], per_ch - first)
        state['pos'] = (pos + per_ch) % n
        state['calls'] += 1

    def cb_rate(r):
        print('  [callback] sampleRateDidChange %.0f' % r)

    def cb_msg(sel, val, msg, opt):
        return 0

    cbs = ASIOCallbacks()
    cbs.bufferSwitch = BUF_SWITCH(cb_switch)
    cbs.sampleRateDidChange = SR_CHANGE(cb_rate)
    cbs.asioMessage = MSG_FN(cb_msg)
    cbs.bufferSwitchTimeInfo = BSTI_FN()

    infos = (ASIOBufferInfo * 2)()
    infos[0].isInput = 0; infos[0].channelNum = 0
    infos[1].isInput = 0; infos[1].channelNum = 1
    state['infos'] = infos

    r = createBuffers(iasio, infos, 2, BUFSIZE, ctypes.byref(cbs))
    print('createBuffers(%d) -> %d  bufs: ch0=%s ch1=%s' % (BUFSIZE, r,
          hex(infos[0].buffers[0] or 0), hex(infos[1].buffers[0] or 0)))
    if r != 0:
        return 3

    r = start(iasio)
    print('start -> %d   PLAYING %.1fs (L = 1 kHz tone, R = silence)...' % (r, DUR))
    if r != 0:
        return 3

    t0 = time.time()
    while time.time() - t0 < DUR:
        time.sleep(1.0)
        sp = ASIOSamples()
        getSamplePosition(iasio, ctypes.byref(sp), None)
        pos64 = sp.lo | (sp.hi << 32)
        print('  t=%.1fs bufferSwitch calls=%d samplePos=%d (%.2fs of DSD)' % (
            time.time() - t0, state['calls'], pos64, pos64 / float(rate)))

    r = stop(iasio)
    print('stop -> %d  (total bufferSwitch calls: %d)' % (r, state['calls']))
    disposeBuffers(iasio)
    print('DONE')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())

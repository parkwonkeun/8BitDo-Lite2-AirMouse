"""Bounded Windows HID transfers; importing this module performs no I/O."""
import ctypes as C
from ctypes import wintypes as W
import inspect_lite2_readonly as r

r.api(r.k,'WriteFile',W.BOOL,[W.HANDLE,r.P,W.DWORD,r.P,r.P])

def transfer(handle,data=None,length=64,timeout=3000):
    event=r.k.CreateEventW(None,True,False,None)
    ov=r.OV();ov.hEvent=event;n=W.DWORD()
    buf=C.create_string_buffer(data,len(data)) if data is not None else C.create_string_buffer(length)
    try:
        ok=(r.k.WriteFile if data is not None else r.k.ReadFile)(handle,buf,len(buf),C.byref(n),C.byref(ov))
        if not ok:
            if C.get_last_error()!=997:raise C.WinError(C.get_last_error())
            if r.k.WaitForSingleObject(event,timeout)!=0:
                r.k.CancelIoEx(handle,C.byref(ov));r.k.GetOverlappedResult(handle,C.byref(ov),C.byref(n),True)
                return None
            if not r.k.GetOverlappedResult(handle,C.byref(ov),C.byref(n),False):raise C.WinError(C.get_last_error())
        return n.value if data is not None else buf.raw[:n.value]
    finally:r.k.CloseHandle(event)

def open_unique(vid,pid):
    paths=r.paths(f'vid_{vid:04x}&pid_{pid:04x}')
    if len(paths)>1 and (vid,pid) in ((0x2dc8,0x5112),(0x057e,0x2009)):
        # The custom image exposes a separate vendor collection. Windows owns
        # the mouse collection; do not attempt to open it for raw HID reads.
        vendor=[]
        for path in paths:
            probe=r.k.CreateFileW(path,0,3,None,3,0,None);pp=r.P()
            if probe==r.P(-1).value:continue
            try:
                if r.h.HidD_GetPreparsedData(probe,C.byref(pp)):
                    caps=r.CAPS()
                    if r.h.HidP_GetCaps(pp,C.byref(caps))==0x110000 and caps.UsagePage==0xff00:
                        vendor.append(path)
            finally:
                if pp:r.h.HidD_FreePreparsedData(pp)
                r.k.CloseHandle(probe)
        paths=vendor
    if len(paths)!=1:raise RuntimeError(f'Expected one {vid:04x}:{pid:04x} HID collection, found {len(paths)}')
    handle=r.k.CreateFileW(paths[0],0xc0000000,3,None,3,0x40000000,None)
    if handle==r.P(-1).value:raise C.WinError(C.get_last_error())
    attr=r.ATTR();attr.Size=C.sizeof(attr)
    if not r.h.HidD_GetAttributes(handle,C.byref(attr)) or (attr.VendorID,attr.ProductID)!=(vid,pid):
        r.k.CloseHandle(handle);raise RuntimeError('Device identity mismatch')
    return handle

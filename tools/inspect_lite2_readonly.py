"""Read only Windows HID inspection. Opens only VID 2DC8 / PID 5112.
No WriteFile, SetFeature, output reports, vendor commands, or bootloader commands.
"""
import ctypes as C
from ctypes import wintypes as W
import json, struct, time, collections
from pathlib import Path

k=C.WinDLL('kernel32',use_last_error=True)
h=C.WinDLL('hid',use_last_error=True)
s=C.WinDLL('setupapi',use_last_error=True)
P=C.c_void_p
class GUID(C.Structure):
    _fields_=[('d1',W.DWORD),('d2',W.WORD),('d3',W.WORD),('d4',C.c_ubyte*8)]
class IFACE(C.Structure):
    _fields_=[('cbSize',W.DWORD),('guid',GUID),('flags',W.DWORD),('reserved',C.c_size_t)]
class ATTR(C.Structure):
    _fields_=[('Size',W.ULONG),('VendorID',W.USHORT),('ProductID',W.USHORT),('VersionNumber',W.USHORT)]
class CAPS(C.Structure):
    _fields_=[(n,W.USHORT) for n in ['Usage','UsagePage','InputReportByteLength','OutputReportByteLength','FeatureReportByteLength']]+[('Reserved',W.USHORT*17)]+[(n,W.USHORT) for n in ['NumberLinkCollectionNodes','NumberInputButtonCaps','NumberInputValueCaps','NumberInputDataIndices','NumberOutputButtonCaps','NumberOutputValueCaps','NumberOutputDataIndices','NumberFeatureButtonCaps','NumberFeatureValueCaps','NumberFeatureDataIndices']]
class OV(C.Structure):
    _fields_=[('Internal',C.c_size_t),('InternalHigh',C.c_size_t),('Offset',W.DWORD),('OffsetHigh',W.DWORD),('hEvent',W.HANDLE)]
def api(lib,name,rest,args):
    f=getattr(lib,name);f.restype=rest;f.argtypes=args;return f
api(h,'HidD_GetHidGuid',None,[P])
api(s,'SetupDiGetClassDevsW',P,[P,W.LPCWSTR,W.HWND,W.DWORD])
api(s,'SetupDiEnumDeviceInterfaces',W.BOOL,[P,P,P,W.DWORD,P])
api(s,'SetupDiGetDeviceInterfaceDetailW',W.BOOL,[P,P,P,W.DWORD,P,P])
api(s,'SetupDiDestroyDeviceInfoList',W.BOOL,[P])
api(k,'CreateFileW',W.HANDLE,[W.LPCWSTR,W.DWORD,W.DWORD,P,W.DWORD,W.DWORD,W.HANDLE])
api(k,'CloseHandle',W.BOOL,[W.HANDLE])
for n in ['HidD_GetAttributes','HidD_GetPreparsedData']:
    api(h,n,C.c_ubyte,[W.HANDLE,P])
api(h,'HidD_FreePreparsedData',C.c_ubyte,[P])
api(h,'HidP_GetCaps',W.LONG,[P,P])
for n in ['HidP_GetButtonCaps','HidP_GetValueCaps']:
    api(h,n,W.LONG,[C.c_int,P,P,P])
for n in ['HidD_GetProductString','HidD_GetManufacturerString']:
    api(h,n,C.c_ubyte,[W.HANDLE,P,W.ULONG])
api(k,'CreateEventW',W.HANDLE,[P,W.BOOL,W.BOOL,W.LPCWSTR])
api(k,'ResetEvent',W.BOOL,[W.HANDLE])
api(k,'ReadFile',W.BOOL,[W.HANDLE,P,W.DWORD,P,P])
api(k,'WaitForSingleObject',W.DWORD,[W.HANDLE,W.DWORD])
api(k,'GetOverlappedResult',W.BOOL,[W.HANDLE,P,P,W.BOOL])
api(k,'CancelIoEx',W.BOOL,[W.HANDLE,P])

def paths(match='vid_2dc8&pid_5112'):
    g=GUID();h.HidD_GetHidGuid(C.byref(g));devs=s.SetupDiGetClassDevsW(C.byref(g),None,None,0x12)
    if devs==P(-1).value:raise C.WinError(C.get_last_error())
    result=[]
    try:
        for i in range(1024):
            it=IFACE();it.cbSize=C.sizeof(it)
            if not s.SetupDiEnumDeviceInterfaces(devs,None,C.byref(g),i,C.byref(it)):
                if C.get_last_error()==259:break
                raise C.WinError(C.get_last_error())
            n=W.DWORD();s.SetupDiGetDeviceInterfaceDetailW(devs,C.byref(it),None,0,C.byref(n),None)
            buf=C.create_string_buffer(n.value);C.cast(buf,C.POINTER(W.DWORD))[0]=8 if C.sizeof(P)==8 else 6
            if not s.SetupDiGetDeviceInterfaceDetailW(devs,C.byref(it),buf,n.value,None,None):raise C.WinError(C.get_last_error())
            p=C.wstring_at(C.addressof(buf)+4)
            if match in p.lower():result.append(p)
    finally:s.SetupDiDestroyDeviceInfoList(devs)
    return result

def get_caps(pp,typ,count,isvalue):
    if not count:return []
    buf=C.create_string_buffer(72*count);n=W.USHORT(count)
    f=h.HidP_GetValueCaps if isvalue else h.HidP_GetButtonCaps
    status=f(typ,buf,C.byref(n),pp)
    if status!=0x110000:raise RuntimeError(hex(status&0xffffffff))
    result=[]
    for i in range(n.value):
        b=buf.raw[i*72:(i+1)*72]
        x={'usage_page':struct.unpack_from('<H',b)[0],'report_id':b[2],'is_range':bool(b[12]),'is_absolute':bool(b[15])}
        if x['is_range']:x['usage_min'],x['usage_max']=struct.unpack_from('<HH',b,56)
        else:x['usage']=struct.unpack_from('<H',b,56)[0]
        if isvalue:
            x['bit_size'],x['report_count']=struct.unpack_from('<HH',b,18)
            x['logical_min'],x['logical_max']=struct.unpack_from('<ii',b,40)
        result.append(x)
    return result

def inspect(path):
    handle=k.CreateFileW(path,0x80000000,3,None,3,0x40000000,None)
    if handle==P(-1).value:raise C.WinError(C.get_last_error())
    pp=P();event=None
    try:
        a=ATTR();a.Size=C.sizeof(a)
        if not h.HidD_GetAttributes(handle,C.byref(a)):raise C.WinError(C.get_last_error())
        assert (a.VendorID,a.ProductID)==(0x2dc8,0x5112)
        result={'vid':f'{a.VendorID:04x}','pid':f'{a.ProductID:04x}','device_release_bcd':f'{a.VersionNumber:04x}'}
        for key,fn in [('product',h.HidD_GetProductString),('manufacturer',h.HidD_GetManufacturerString)]:
            v=C.create_unicode_buffer(128)
            if fn(handle,v,C.sizeof(v)):result[key]=v.value
        if not h.HidD_GetPreparsedData(handle,C.byref(pp)):raise C.WinError(C.get_last_error())
        caps=CAPS()
        if h.HidP_GetCaps(pp,C.byref(caps))!=0x110000:raise RuntimeError('HidP_GetCaps failed')
        result['caps']={n:getattr(caps,n) for n,t in caps._fields_ if n!='Reserved'}
        result['input_buttons']=get_caps(pp,0,caps.NumberInputButtonCaps,False)
        result['input_values']=get_caps(pp,0,caps.NumberInputValueCaps,True)
        result['output_values']=get_caps(pp,1,caps.NumberOutputValueCaps,True)
        result['feature_values']=get_caps(pp,2,caps.NumberFeatureValueCaps,True)
        print(json.dumps(result,ensure_ascii=False,indent=2),flush=True)
        event=k.CreateEventW(None,True,False,None)
        if not event:raise C.WinError(C.get_last_error())
        samples=[];counts=collections.Counter();start=time.monotonic();first=None;last=None
        print('Passive input capture: 15 seconds; no reports sent.',flush=True)
        while time.monotonic()-start<15:
            k.ResetEvent(event);ov=OV();ov.hEvent=event
            buf=C.create_string_buffer(caps.InputReportByteLength);n=W.DWORD()
            ok=k.ReadFile(handle,buf,len(buf),C.byref(n),C.byref(ov))
            if not ok:
                err=C.get_last_error()
                if err!=997:result['read_error']=err;break
                wait=k.WaitForSingleObject(event,250)
                if wait!=0:
                    k.CancelIoEx(handle,C.byref(ov));k.GetOverlappedResult(handle,C.byref(ov),C.byref(n),True)
                    continue
                if not k.GetOverlappedResult(handle,C.byref(ov),C.byref(n),False):result['read_error']=C.get_last_error();break
            data=buf.raw[:n.value];t=time.monotonic()-start
            if data:
                if first is None:first=t
                last=t;counts[data.hex()]+=1
                if len(samples)<5000:samples.append({'t_seconds':round(t,6),'hex':data.hex()})
        result['capture']={'duration_seconds':round(time.monotonic()-start,3),'count':sum(counts.values()),'unique_reports':len(counts),'first_t':first,'last_t':last,'histogram':dict(counts),'samples':samples}
        return result
    finally:
        if event:k.CloseHandle(event)
        if pp:h.HidD_FreePreparsedData(pp)
        k.CloseHandle(handle)

if __name__=='__main__':
    found=paths()
    if not found:raise SystemExit('No matching Lite 2 HID collection present')
    results=[inspect(p) for p in found]
    output=Path('outputs/Lite2-connected-readonly-2026-11-22.json')
    output.write_text(json.dumps({'mode':'read-only; no output/feature/vendor commands sent','devices':results},ensure_ascii=False,indent=2),encoding='utf-8')
    for r in results:print('CAPTURE',json.dumps({k:v for k,v in r['capture'].items() if k not in ['samples','histogram']}))
    print('Saved',output)

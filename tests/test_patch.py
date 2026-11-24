from test_arm import Machine, ROOT, SP, STOP
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import *
import struct
import sys
import unittest

sys.path.insert(0,str(ROOT/'tools'))
from package_patch import MOUSE, BT_MOUSE, KEYBOARD, CONSUMER, USB_FINE_DESCRIPTOR, thumb_bl

PATCH_IMAGE = ROOT / 'build/Lite2AirMouse-v106-s-mode-r50_1.bin'

def parse_hid(data):
    i=0;size=count=0;report_id=0;depth=0;globals_stack=[];bits={};relative=[];usages=[]
    while i<len(data):
        prefix=data[i];i+=1
        n=(0,1,2,4)[prefix&3];tag=prefix>>4;kind=(prefix>>2)&3
        if prefix==0xfe or i+n>len(data): raise ValueError('Invalid HID item')
        value=int.from_bytes(data[i:i+n],'little');i+=n
        if kind==1:
            if tag==10:globals_stack.append((size,count,report_id))
            elif tag==11:size,count,report_id=globals_stack.pop()
            elif tag==7:size=value
            elif tag==8:report_id=value
            elif tag==9:count=value
        if kind==2 and tag==0: usages.append(value)
        if kind==0:
            if tag==10:depth+=1
            elif tag==12:depth-=1
            elif tag in (8,9,11):
                bits[(tag,report_id)]=bits.get((tag,report_id),0)+size*count
                if tag==8 and value&4:relative.append((size,count,usages[:]))
            usages=[]
            if depth<0:raise ValueError('Unmatched HID collection')
    if depth:raise ValueError('Unclosed HID collection')
    return bits,relative

class PatchTests(unittest.TestCase):
    def test_descriptor_transport_lengths(self):
        bits,rel=parse_hid(MOUSE)
        self.assertEqual(bits,{(8,1):128})
        self.assertEqual([(r[0],r[1]) for r in rel],[(16,2),(8,1),(16,5)])
        bt,relative=parse_hid(BT_MOUSE)
        self.assertEqual(bt,{(8,1):136,(11,6):8})
        self.assertEqual(relative[-1],(8,1,[0x38]))
        data=PATCH_IMAGE.read_bytes()
        original=(ROOT/'outputs/Lite2-v1.06-decrypted-analysis-only.bin').read_bytes()
        pointer,=struct.unpack_from('<I',data,0xc220)
        start=pointer-0x1018000
        length,=struct.unpack_from('<H',data,0x122b2)
        self.assertEqual(length,len(USB_FINE_DESCRIPTOR))
        self.assertEqual(data[start:start+length],USB_FINE_DESCRIPTOR)
        usb,_=parse_hid(data[start:start+length])
        self.assertEqual(usb,{(8,1):128,(8,2):504,(8,3):504,(8,4):64,(9,4):8,(8,5):8,(9,0x81):504,(11,6):8})
        self.assertTrue(all(length % 8 == 0 for length in usb.values()),
                        "Windows requires byte-aligned USB HID reports")
        self.assertEqual(data[0x122eb:0x12372],original[0x122eb:0x12372])
        self.assertEqual(data[0x12ee2:0x130ba],original[0x12ee2:0x130ba])
        self.assertEqual(data[0x5bae:0x5d40],original[0x5bae:0x5d40])

    def test_s_mouse_radio_policy_only_after_registration(self):
        machine=Machine()
        image=PATCH_IMAGE.read_bytes()
        machine.u.mem_write(0x1018000,image)
        self.assertEqual(image[0x217c:0x2180],thumb_bl(0x0101a17c,machine.symbols['lite2_hid_connected']))
        # Reproduce startup's stock data-table initialization from this image.
        machine.symbols['init_data']=0x10187fe
        machine.call('init_data',0x102ba58,0x20000,0x668)
        calls=[]
        def record(uc,address,size,unused):
            r0,r1=uc.reg_read(UC_ARM_REG_R0),uc.reg_read(UC_ARM_REG_R1)
            if address==0x101a4f0:
                calls.append(('features',bytes(uc.mem_read(r0,r1))))
            if address==0x101a958:
                calls.append(('sniff',r0,r1))
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        for address in (0x102141c,0x101a4f0,0x101a958):
            machine.u.hook_add(UC_HOOK_CODE,record,begin=address,end=address)
        machine.u.hook_add(UC_HOOK_CODE,lambda uc,a,n,d:uc.reg_write(UC_ARM_REG_PC,STOP|1),
                           begin=0x1021f94,end=0x1021f94)
        machine.symbols['after_s_registration']=0x1021ec6
        machine.call('after_s_registration',0)
        self.assertEqual(calls,[])
        machine.call('after_s_registration',1)
        r48_policy=[('features',bytes.fromhex('7f fd 89 fa 98 1d 59 87')),('sniff',0,0)]
        self.assertEqual(calls,r48_policy)
        # The unmodified shared path for stock mode 3 still advertises Role Switch.
        calls.clear()
        machine.symbols['other_mode_features']=0x1021f5c
        machine.call('other_mode_features')
        self.assertEqual(calls,r48_policy)
        stock=(ROOT/'outputs/Lite2-v1.06-decrypted-analysis-only.bin').read_bytes()
        self.assertEqual(image[0x9eda:0x9f22],stock[0x9eda:0x9f22])

    def test_thumb_branch_encoder(self):
        for source,target in [(0x101cfe8,0x102c0a1),(0x102c0a0,0x101cfe9)]:
            a,b=struct.unpack('<HH',thumb_bl(source,target))
            delta=((a&0x7ff)<<12)|((b&0x7ff)<<1)
            if delta&(1<<22):delta-=1<<23
            self.assertEqual(source+4+delta,target&~1)

    def test_pair_button_blinks_four_lights_on_the_native_timer(self):
        m=Machine()
        u=m.u
        u.mem_write(0x1018000,PATCH_IMAGE.read_bytes())
        callback,=struct.unpack('<I',u.mem_read(0x10211ac,4))
        self.assertEqual(callback,m.symbols['lite2_pairing_lights']|1)
        gpio=set()
        writes=[]
        scheduled=[]
        delegated=[]
        power,usb=1,0
        def io(uc,addr,size,data):
            a=uc.reg_read(UC_ARM_REG_R0)
            b=uc.reg_read(UC_ARM_REG_R1)
            if addr==0x101cf18 or addr==0x101ce4c:
                writes.append((b,addr==0x101cf18))
                if addr==0x101cf18:gpio.add(b)
                else:gpio.discard(b)
            elif addr==0x101d51e:uc.reg_write(UC_ARM_REG_R0,int(a in gpio))
            elif addr==0x102644c:scheduled.append((a,b))
            elif addr==0x1026104:scheduled.append((a,None))
            elif addr==0x10210e0:delegated.append(a)
            else:uc.reg_write(UC_ARM_REG_R0,power if addr==0x1020a34 else usb)
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        for addr in (0x101cf18,0x101ce4c,0x101d51e,0x102644c,0x1026104,0x10210e0,0x1020a34,0x1020a80):
            u.hook_add(UC_HOOK_CODE,io,begin=addr,end=addr)
        for mode in (0,1):
            u.mem_write(0x20184,bytes([mode]))
            u.mem_write(0x20152,bytes([0]))
            u.mem_write(0x2fff8,struct.pack('<I',0x20221031))
            for on in (True,False):
                writes.clear()
                m.call('lite2_pairing_lights',0x21870)
                self.assertEqual(writes,[(pin,on) for pin in (0x100,0x80,0x8000,0x4000)])
                self.assertEqual(scheduled[-2:],[(0x21870,250),(0x21870,None)])
        self.assertFalse(delegated)
        writes.clear()
        # Connected, ordinary reconnect, USB and power-off use the stock callback.
        for reason,link,power,usb in ((0x20221031,2,1,0),(0,0,1,0),
                                     (0x20221031,0,1,1),(0x20221031,0,0,0)):
            u.mem_write(0x20152,bytes([link]))
            u.mem_write(0x2fff8,struct.pack('<I',reason))
            m.call('lite2_pairing_lights',0x21870)
        self.assertEqual(delegated,[0x21870]*4)
        self.assertFalse(writes)

    def test_wireless_dispatch_and_buttons(self):
        m=Machine();u=m.u
        image=PATCH_IMAGE.read_bytes()
        u.mem_write(0x1018000,image)
        packets=[];usb_packets=[];original_calls=[]
        sensor_reads=[]
        def sensor(uc,address,size,data):
            sensor_reads.append(address)
            uc.mem_write(uc.reg_read(UC_ARM_REG_R0),struct.pack('<6h',0,0,4096,0,0,-2048 if data[0] else 0))
            uc.reg_write(UC_ARM_REG_R0,1)
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        moving=[False]
        u.hook_add(UC_HOOK_CODE,sensor,moving,begin=0x101d60c,end=0x101d60c)
        def idle(uc,address,size,data):uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        u.hook_add(UC_HOOK_CODE,idle,begin=0x10221a4,end=0x10221a4)
        leds=[]
        def led_write(uc,address,size,data):
            leds.append((uc.reg_read(UC_ARM_REG_R1),int(address==0x101cf18)))
            idle(uc,address,size,data)
        for address in (0x101cf18,0x101ce4c):
            u.hook_add(UC_HOOK_CODE,led_write,begin=address,end=address)
        physical=[0]
        def raw_buttons(uc,address,size,data):
            uc.reg_write(UC_ARM_REG_R0,physical[0])
            idle(uc,address,size,data)
        u.hook_add(UC_HOOK_CODE,raw_buttons,begin=0x1021b64,end=0x1021b64)
        def usb_state(uc,address,size,data):
            uc.reg_write(UC_ARM_REG_R0,0);idle(uc,address,size,data)
        u.hook_add(UC_HOOK_CODE,usb_state,begin=0x1020a80,end=0x1020a80)
        rejected=[False]
        reject_mouse=[False]
        u.hook_add(UC_HOOK_CODE,idle,begin=0x1018ee8,end=0x1018ee8)
        def bt_send(uc,address,size,data):
            request=uc.reg_read(UC_ARM_REG_R0)
            _,cmd,length,pointer=struct.unpack('<BBHI',uc.mem_read(request,8))
            assert cmd == 0xb0
            packet=bytes(uc.mem_read(pointer,length))
            if reject_mouse[0] and packet[1]==1:
                reject_mouse[0]=False
                uc.reg_write(UC_ARM_REG_R0,0)
            elif rejected[0] and packet[1]==5:
                rejected[0]=False
                uc.reg_write(UC_ARM_REG_R0,0)
            else:
                packets.append(packet)
                uc.reg_write(UC_ARM_REG_R0,1)
            idle(uc,address,size,data)
        u.hook_add(UC_HOOK_CODE,bt_send,begin=0x10223fe,end=0x10223fe)
        delays=[]
        def timer(uc,address,size,data):
            delays.append(uc.reg_read(UC_ARM_REG_R1))
            idle(uc,address,size,data)
        u.hook_add(UC_HOOK_CODE,timer,begin=0x102644c,end=0x102644c)
        def original(uc,address,size,data):
            original_calls.append(tuple(uc.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2)))
            idle(uc,address,size,data)
        u.hook_add(UC_HOOK_CODE,original,begin=0x1020d38,end=0x1020d38)
        u.mem_write(0x2022b,b'\x0a')
        u.hook_add(UC_HOOK_CODE,idle,begin=0x1023958,end=0x1023958)
        m.call('lite2_scan')
        u.mem_write(0x20152,b'\x02')
        # Exercise the wireless timer fallback with a stopped SysTick.
        for tick in range(101):m.call('lite2_dispatch',0,0,0)
        self.assertEqual(u.mem_read(0x2fe20,1),b'\x01')
        moving[0]=True
        for button,expected in [(0x20,1),(0x10,2),(0x4000,0),(0x8000,0),(0x400|0x800|0xc000,0),(0,0)]:
            m.call('lite2_dispatch',button,0,0)
            if packets[-1][1] != 1:
                m.call('lite2_dispatch',button,0,0)
            self.assertEqual(packets[-1][1],1)
            self.assertEqual(len(packets[-1]),19)
            mouse=struct.unpack_from('<BhhbhhhHH',packets[-1],2)
            self.assertEqual(mouse[0],expected)
            self.assertEqual((mouse[4],mouse[5],mouse[6]&2047),(2048,0,1408))
            if expected:
                self.assertEqual(mouse[1:3],(0,0))
            else:
                self.assertIn(mouse[1:3],((180,0),(360,0)))
        # Execute the actual interval-event entry for S and D. Stock passes
        # the radio interval unchanged for D; S programs the supported 5ms tick.
        applied=[]
        def period_set(uc,address,size,data):
            applied.append(uc.reg_read(UC_ARM_REG_R0));idle(uc,address,size,data)
        u.hook_add(UC_HOOK_CODE,period_set,begin=0x101d874,end=0x101d874)
        u.hook_add(UC_HOOK_CODE,lambda uc,a,n,d:uc.reg_write(UC_ARM_REG_PC,STOP|1),begin=0x101a10a,end=0x101a10a)
        m.symbols['interval_event']=0x101a0bc
        u.mem_write(0x20524,struct.pack('<H',30))
        for mode,expected_period in ((0,5),(1,30)):
            u.mem_write(0x20184,bytes([mode]))
            m.call('interval_event')
            self.assertEqual(applied[-1],expected_period)
        u.mem_write(0x20184,b'\x00')
        def reports_for(buttons):
            start=len(packets)
            for _ in range(5): m.call('lite2_dispatch',buttons,0,0)
            return packets[start:]

        for mask in [0]+[1<<bit for bit in range(18)]+[0x3ffff,0x40000,0x20000000,0]:
            physical[0]=mask
            mouse=next(frame for frame in reversed(reports_for(0)) if frame[1]==1)
            lo,hi=struct.unpack_from('<HH',mouse,14)
            self.assertEqual((((hi-1)&511)<<9)|((lo-1)&511), mask&0x3ffff)
        rejected[0]=True
        physical[0]=0x10000
        frames=reports_for(0x2000|0x200)
        physical[0]=0
        self.assertIn(bytes.fromhex('a1 04 00 00 52 00 00 00 00 00'),frames)
        self.assertIn(bytes.fromhex('a1 05 03'),frames)
        frames=reports_for(0x1000|0x40)
        self.assertIn(bytes.fromhex('a1 04 00 00 28 4f 00 00 00 00'),frames)
        self.assertIn(bytes.fromhex('a1 05 00'),frames)
        self.assertIn(bytes.fromhex('a1 04 00 00 00 00 00 00 00 00'),reports_for(0))
        for button,usage in ((0x200,0x52),(0x100,0x51),(0x80,0x50),(0x40,0x4f)):
            self.assertIn(bytes([0xa1,4,0,0,usage,0,0,0,0,0]),reports_for(button))
        self.assertIn(bytes.fromhex('a1 05 01'),reports_for(0x2000))
        self.assertIn(bytes.fromhex('a1 05 00'),reports_for(0x20000))
        for physical[0],expected in [(1<<10,64),(1<<11,128),(3<<10,0),(0,0)]:
            frames=[f for f in reports_for(0x20) if f[1]==1]
            self.assertTrue(frames)
            self.assertTrue(all(f[2]==(expected|1) for f in frames))
        physical[0]=0x20000
        self.assertNotIn(bytes.fromhex('a1 05 02'),reports_for(0))
        physical[0]=0
        power_pulse=reports_for(0)
        self.assertIn(bytes.fromhex('a1 05 02'),power_pulse)
        self.assertIn(bytes.fromhex('a1 05 00'),power_pulse)
        physical[0]=0x20000
        for _ in range(16): reports_for(0)
        physical[0]=0
        self.assertNotIn(bytes.fromhex('a1 05 02'),reports_for(0))
        self.assertEqual(leds[-1],(0x100,1))
        for led in (0x80,0x8000,0x100):
            m.call('lite2_dispatch',2,0,0)
            self.assertEqual(leds[-1],(led,1))
            before=len(sensor_reads)
            for _ in range(3): m.call('lite2_dispatch',0,0,0)
            self.assertEqual(len(sensor_reads)-before,0 if led==0x80 else 3)
            if led==0x80:
                for _ in range(3):m.call('lite2_dispatch',0,0x80807790,0)
                frame=next(f for f in reversed(packets) if f[1]==1)
                x,y,tag,low,high=struct.unpack_from('<hhHHH',frame,8)
                self.assertEqual((x,y,tag&2047),(16,-9,1664))
                self.assertEqual(((low-1)//512-1,(high-1)//512-1,tag>>11),(18,14,16))
                center=m.symbols['lite2_stick_centers']
                self.assertEqual(bytes(u.mem_read(center,8)),bytes([128])*8)
                # Synthetic widened neutral zone for this emulated test only.
                u.mem_write(center,bytes([120]*4+[136]*4))
                start=len(packets)
                resting=[0x88888888,0x80808080,0x7c84847c,0x78787878]
                for i in range(400):m.call('lite2_dispatch',0,resting[i%len(resting)],0)
                rest=[f for f in packets[start:] if f[1]==1]
                self.assertGreater(len(rest),350)
                for frame in rest[12:]:
                    self.assertEqual(struct.unpack_from('<hh',frame,3),(0,0))
                    self.assertEqual(struct.unpack_from('<hh',frame,8),(0,0))
                for x,y in [(0,255),(255,0),(75,200),(200,75)]:
                    for _ in range(4):m.call('lite2_dispatch',0,0x80800000|(y<<8)|x,0)
                    frame=next(f for f in reversed(packets) if f[1]==1)
                    def normalized(v):
                        return int((v-120)*128/120) if v<120 else int(max(0,v-136)*127/(255-136))
                    self.assertEqual(struct.unpack_from('<hh',frame,8),(normalized(x),normalized(y)))
                u.mem_write(center,bytes([128])*8)

        m.symbols['stock_led_entry']=0x1022428
        m.call('stock_led_entry',0x4000,1)
        self.assertEqual(leds[-1],(0x100,1))
        # A moving gyro must request the same short timer as the stick.
        moving[0]=True
        m.call('lite2_dispatch',0,0x80808080,0)
        m.call('lite2_scan_delay',0x20000,10)
        self.assertEqual(delays[-1],4)
        for button in (0x20,0x10):
            for _ in range(3):
                m.call('lite2_dispatch',button,0x80808080,0)
                m.call('lite2_scan_delay',0x20000,19)
                self.assertEqual(delays[-1],4)
                if packets[-1][1]==1:self.assertEqual(packets[-1][3:7],bytes(4))
            m.call('lite2_dispatch',0,0x80808080,0)
        moving[0]=False
        m.call('lite2_dispatch',0,0x80808080,0)
        m.call('lite2_scan_delay',0x20000,9)
        self.assertEqual(delays[-1],4)
        for _ in range(8):m.call('lite2_dispatch',0,0x808080ff,0)
        m.call('lite2_scan_delay',0x20000,9)
        self.assertEqual(delays[-1],4)
        m.call('lite2_dispatch',0,0x80808080,0)
        m.call('lite2_scan_delay',0x20000,10)
        self.assertEqual(delays[-1],4)
        # The new tail carries fine steps even across a busy radio queue and
        # competing keyboard refreshes. The cached descriptor sees byte 7.
        for axis,sign in [(0x01808080,1),(0xff808080,-1)]:
            m.call('lite2_dispatch',0,axis,0)
            start=len(packets)
            for sample in range(100):
                if sample==20:reject_mouse[0]=True
                m.call('lite2_dispatch',0,axis,0)
            for _ in range(3):m.call('lite2_dispatch',0,0x80808080,0)
            frames=[f for f in packets[start:] if f[1]==1]
            def signed(f,index): return int.from_bytes(f[index:index+1],'little',signed=True)
            self.assertIn(sum(signed(f,18)*sign for f in frames),(1800,1920))
            self.assertIn(sum(signed(f,7)*sign for f in frames),(15,16))
            self.assertEqual(frames[-1][18],0)
        m.call('lite2_dispatch',0,0x01808080,0)
        m.call('lite2_scan_delay',0x20000,10)
        self.assertEqual(delays[-1],4)
        led_count=len(leds)
        count=len(packets)
        u.mem_write(0x20184,b'\x01')
        m.call('lite2_dispatch',0x1234,0x5678,0x9abc)
        self.assertEqual(original_calls,[(0x1234,0x5678,0x9abc)])
        self.assertEqual(len(packets),count)
        self.assertEqual(len(leds),led_count)

    def test_usb_wheel_multiplier_control_requests(self):
        m=Machine()
        u=m.u
        image=PATCH_IMAGE.read_bytes()
        u.mem_write(0x1018000,image)
        m.symbols['usb_setup']=0x1023f60
        response=[]
        def control_send(uc,address,size,data):
            ptr=uc.reg_read(UC_ARM_REG_R1)
            length=uc.reg_read(UC_ARM_REG_R2)
            response.append(bytes(uc.mem_read(ptr,length)))
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        u.hook_add(UC_HOOK_CODE,control_send,begin=0x1023a86,end=0x1023a86)
        u.mem_write(0x22000,bytes.fromhex('a1 01 06 03 00 00 02 00'))
        m.call('usb_setup',0x22100,0x22000)
        self.assertEqual(response,[b'\x06\x01'])
        u.mem_write(0x22000,bytes.fromhex('21 09 06 03 00 00 02 00'))
        m.call('usb_setup',0x22100,0x22000)
        self.assertEqual(bytes(u.mem_read(0x20120,4)),bytes([1,3,6,2]))
        u.hook_add(UC_HOOK_CODE,lambda uc,a,n,d:uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR)),
                   begin=0x101f4fc,end=0x101f4fc)
        u.mem_write(0x22000,bytes.fromhex('81 06 00 22 00 00 ff 00'))
        m.call('usb_setup',0x22100,0x22000)
        self.assertEqual(response[-1],USB_FINE_DESCRIPTOR)

    def test_usb_frame_service(self):
        m=Machine()
        u=m.u
        image=PATCH_IMAGE.read_bytes()
        original=(ROOT/'outputs/Lite2-v1.06-decrypted-analysis-only.bin').read_bytes()
        # Check all USB configuration descriptors, including the S OUT endpoint.
        config=bytearray(original[0x1214c:0x122eb])
        config[0x122ba-0x1214c]=1
        struct.pack_into("<H",config,0x122b2-0x1214c,len(USB_FINE_DESCRIPTOR))
        self.assertEqual(image[0x1214c:0x122eb],config)
        u.mem_write(0x1018000,image)
        u.mem_map(0xe000e000,0x1000)
        u.mem_write(0xe000e014,struct.pack('<I',99999))
        u.mem_write(0x2022b,b'\x0a')
        reports=[]
        adc=[]
        connected=[True]
        def stub(uc,address,size,data):
            result=0
            if address==0x1020a80: result=connected[0]
            elif address==0x1020ac4:
                adc.append(1)
                result=0x6c8080ff
            elif address==0x101c668:
                ptr=uc.reg_read(UC_ARM_REG_R0)
                reports.append(bytes(uc.mem_read(ptr,uc.reg_read(UC_ARM_REG_R1))))
            uc.reg_write(UC_ARM_REG_R0,int(result))
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        for address in (0x1020a80,0x1020ac4,0x1021b64,0x101d60c,0x101c668,
                        0x101cf18,0x101ce4c,0x10221a4):
            u.hook_add(UC_HOOK_CODE,stub,begin=address,end=address)
        for ms in range(1000):
            u.mem_write(0x20034,struct.pack('<I',ms//10))
            u.mem_write(0xe000e018,struct.pack('<I',99999-10000*(ms%10)))
            if ms%10==0:m.call('lite2_dispatch',0,0x6c8080ff,0)
            m.call('lite2_usb_poll')
            m.call('lite2_usb_poll')
        self.assertEqual(len(reports),1000)
        # A barely tilted right stick emits +1 in the wheel field directly.
        self.assertEqual(len([r for r in reports if r[0]==1 and r[6]==1]),15)
        self.assertEqual(len(adc),900)
        total=sum(struct.unpack_from('<h',r,2)[0] for r in reports if r[0]==1)
        self.assertAlmostEqual(total,999*1250/1024,delta=2)
        connected[0]=False
        m.call('lite2_usb_poll')
        self.assertEqual(len(reports),1000)

    def test_scan_hook_preserves_original_and_modes(self):
        m=Machine();u=m.u;calls=[]
        delays=[]
        def timer(uc,address,size,data):
            delays.append(uc.reg_read(UC_ARM_REG_R1))
            idle(uc,address,size,data)
        u.hook_add(UC_HOOK_CODE,timer,begin=0x102644c,end=0x102644c)
        def original(uc,address,size,data):
            calls.append(address);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        u.hook_add(UC_HOOK_CODE,original,begin=0x1023958,end=0x1023958)
        for mode,special,expected in [(0,0,99),(1,0,100),(1,1,100),(2,0,100)]:
            u.mem_write(0x20184,bytes([mode]));u.mem_write(0x20187,bytes([special]))
            u.mem_write(0x201ea,struct.pack('<H',100))
            m.call('lite2_scan')
            self.assertEqual(struct.unpack('<H',u.mem_read(0x201ea,2))[0],expected)
        self.assertEqual(len(calls),4)

    def test_bluetooth_class_is_specific_to_s(self):
        m=Machine(); calls=[]
        stock=bytes.fromhex('08 25 00')
        m.u.mem_write(0x204fa,stock)
        def set_class(uc,address,size,data):
            ptr=uc.reg_read(UC_ARM_REG_R0)
            calls.append((ptr,bytes(uc.mem_read(ptr,3))))
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        m.u.hook_add(UC_HOOK_CODE,set_class,begin=0x101a9c0,end=0x101a9c0)
        m.call('lite2_set_device_class',0x0204fa)
        m.u.mem_write(0x20184,b'\x01')
        m.call('lite2_set_device_class',0x0204fa)
        self.assertEqual(calls[0][1],bytes.fromhex('c0 25 00'))
        self.assertEqual(calls[1],(0x204fa,stock))

    def test_sdp_and_distinct_name(self):
        m=Machine();calls=[]
        def set_sdp(uc,address,size,data):
            ptr=uc.reg_read(UC_ARM_REG_R0);length=uc.reg_read(UC_ARM_REG_R1)
            calls.append(bytes(uc.mem_read(ptr,length)))
            uc.reg_write(UC_ARM_REG_R0,1);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        m.u.hook_add(UC_HOOK_CODE,set_sdp,begin=0x101a6fc,end=0x101a6fc)
        m.u.mem_write(0x1018000,PATCH_IMAGE.read_bytes())
        m.call('lite2_set_sdp',0x20290,553)
        desc=BT_MOUSE+KEYBOARD+CONSUMER
        self.assertIn(desc,calls[0])
        bits,_=parse_hid(desc)
        self.assertEqual(bits,{(8,1):136,(11,6):8,(8,4):64,(9,4):8,(8,5):8})
        self.assertLessEqual(len(calls[0]),553)
        self.assertEqual(len(calls[0]),472+len(desc)-83-27-3)
        stock=(ROOT/'outputs/Lite2-v1.06-decrypted-analysis-only.bin').read_bytes()
        self.assertEqual(calls[0][-120:],stock[0x13042:0x130ba])
        self.assertEqual(bytes(m.u.mem_read(m.symbols['lite2_mouse_name'],21)),b'Lite2 Air Mouse Plus\x00')

if __name__=='__main__':unittest.main(verbosity=2)

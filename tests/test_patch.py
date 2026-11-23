from test_arm import Machine, ROOT, SP, STOP
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import *
import struct
import sys
import unittest

sys.path.insert(0,str(ROOT/'tools'))
from package_patch import MOUSE, BT_MOUSE, KEYBOARD, CONSUMER, thumb_bl

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
        data=(ROOT/'build/Lite2AirMouse-v106-s-mode-r31.bin').read_bytes()
        original=(ROOT/'outputs/Lite2-v1.06-decrypted-analysis-only.bin').read_bytes()
        usb,_=parse_hid(data[0x123ea:0x124b5])
        self.assertEqual(usb,{(8,1):128,(8,2):504,(8,3):504,(8,4):64,(8,5):8,(9,0x81):504})
        self.assertEqual(data[0x122eb:0x12372],original[0x122eb:0x12372])
        self.assertEqual(data[0x12ee2:0x130ba],original[0x12ee2:0x130ba])
        self.assertEqual(data[0x5bae:0x5d40],original[0x5bae:0x5d40])

    def test_s_mouse_radio_policy_only_after_registration(self):
        machine=Machine()
        image=(ROOT/'build/Lite2AirMouse-v106-s-mode-r31.bin').read_bytes()
        machine.u.mem_write(0x1018000,image)
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
        self.assertEqual(calls,[('features',bytes.fromhex('7f fd 89 fa 98 1d 59 87')),('sniff',0,0)])
        features=calls[0][1]
        self.assertEqual((features[0]&0x80,features[5]&2),(0,0))
        # The unmodified shared path for stock mode 3 still advertises Role Switch.
        calls.clear()
        machine.symbols['other_mode_features']=0x1021f5c
        machine.call('other_mode_features')
        self.assertEqual(calls,[('features',bytes.fromhex('7f fd 89 fa 98 1d 59 87')),('sniff',0,0)])
        stock=(ROOT/'outputs/Lite2-v1.06-decrypted-analysis-only.bin').read_bytes()
        self.assertEqual(image[0x9eda:0x9f22],stock[0x9eda:0x9f22])

    def test_thumb_branch_encoder(self):
        for source,target in [(0x101cfe8,0x102c0a1),(0x102c0a0,0x101cfe9)]:
            a,b=struct.unpack('<HH',thumb_bl(source,target))
            delta=((a&0x7ff)<<12)|((b&0x7ff)<<1)
            if delta&(1<<22):delta-=1<<23
            self.assertEqual(source+4+delta,target&~1)

    def test_wireless_dispatch_and_buttons(self):
        m=Machine();u=m.u
        image=(ROOT/'build/Lite2AirMouse-v106-s-mode-r31.bin').read_bytes()
        u.mem_write(0x1018000,image)
        packets=[];usb_packets=[];original_calls=[]
        sensor_reads=[]
        def sensor(uc,address,size,data):
            sensor_reads.append(address)
            uc.mem_write(uc.reg_read(UC_ARM_REG_R0),struct.pack('<6h',0,0,4096,0,1024 if data[0] else 0,-2048 if data[0] else 0))
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
            self.assertEqual((mouse[4],mouse[5],mouse[6]&2047),(2048,1024,1408))
            if expected:
                self.assertEqual(mouse[1:3],(0,0))
            else:
                self.assertIn(mouse[1:3],((180,40),(360,80)))
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

        u.mem_write(0x2fe38,struct.pack('<I',1))  # Adapter.active is set by lite2_scan.
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
        self.assertEqual(delays[-1],9)
        for _ in range(8):m.call('lite2_dispatch',0,0x808080ff,0)
        m.call('lite2_scan_delay',0x20000,9)
        self.assertEqual(delays[-1],4)
        m.call('lite2_dispatch',0,0x80808080,0)
        m.call('lite2_scan_delay',0x20000,10)
        self.assertEqual(delays[-1],10)
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
            self.assertIn(sum(signed(f,18)*sign for f in frames),(900,901))
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
        def set_class(uc,address,size,data):
            calls.append(uc.reg_read(UC_ARM_REG_R0))
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        m.u.hook_add(UC_HOOK_CODE,set_class,begin=0x101a9c0,end=0x101a9c0)
        m.call('lite2_set_device_class',0x0204fa)
        m.u.mem_write(0x20184,b'\x01')
        m.call('lite2_set_device_class',0x0204fa)
        self.assertEqual(calls,[0x05c0,0x0204fa])

    def test_sdp_and_distinct_name(self):
        m=Machine();calls=[]
        def set_sdp(uc,address,size,data):
            ptr=uc.reg_read(UC_ARM_REG_R0);length=uc.reg_read(UC_ARM_REG_R1)
            calls.append(bytes(uc.mem_read(ptr,length)))
            uc.reg_write(UC_ARM_REG_R0,1);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        m.u.hook_add(UC_HOOK_CODE,set_sdp,begin=0x101a6fc,end=0x101a6fc)
        m.u.mem_write(0x1018000,(ROOT/'build/Lite2AirMouse-v106-s-mode-r31.bin').read_bytes())
        m.call('lite2_set_sdp',0x20290,553)
        desc=BT_MOUSE+KEYBOARD+CONSUMER
        self.assertIn(desc,calls[0])
        self.assertLessEqual(len(calls[0]),553)
        self.assertEqual(len(calls[0]),472+len(desc)-83-26)
        stock=(ROOT/'outputs/Lite2-v1.06-decrypted-analysis-only.bin').read_bytes()
        self.assertEqual(calls[0][-120:],stock[0x13042:0x130ba])
        self.assertEqual(bytes(m.u.mem_read(m.symbols['lite2_mouse_name'],21)),b'Lite2 Air Mouse Plus\x00')

if __name__=='__main__':unittest.main(verbosity=2)

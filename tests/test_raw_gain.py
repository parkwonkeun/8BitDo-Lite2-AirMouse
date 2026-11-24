import struct
import unittest

from test_arm import Machine, IMU, OUTPUT, UC_HOOK_CODE, UC_ARM_REG_R0, UC_ARM_REG_PC, UC_ARM_REG_LR


class RawGainTest(unittest.TestCase):
    def setUp(self):
        self.m = Machine()
        self.m.u.mem_write(IMU, struct.pack('<6h', 0, 0, 4096, 0, 0, -2048))

    def step(self, buttons=0, axes=0x80808080):
        self.m.call('pointer_step', IMU, buttons, 10, OUTPUT, axes)
        return struct.unpack_from('<h', self.m.u.mem_read(OUTPUT, 9), 1)[0]

    def cold_start(self):
        self.m.u.mem_write(0x2fe00, bytes(256))

    def records(self):
        records=[]
        for word, in struct.iter_unpack('<I',self.m.nv[0x200:]):
            if word==0xffffffff:break
            tag,a,b,check=word.to_bytes(4,'little')
            if 0xd0<=tag<0xe0 and tag^a^b^check==255:
                records.append(((tag>>1)&3,((tag&8)<<14)|((tag&1)<<16)|(b<<8)|a))
        return records

    def test_slow_fast_press_hold_and_release(self):
        self.step(1<<15);self.step()
        self.assertEqual(self.records(),[(0,10100)])
        for _ in range(18): self.step()
        self.step(1<<15)
        self.assertEqual(self.records()[-1],(0,10605))
        for _ in range(39): self.step(1<<15)
        self.assertEqual(len(self.records()),2)
        self.step(1<<15)
        self.assertEqual(self.records()[-1],(0,11135))
        for _ in range(12):self.step(1<<15)
        self.assertEqual(self.records()[-1],(0,11692))
        for _ in range(30):self.step()
        self.step(1<<15)
        self.assertEqual(self.records()[-1],(0,11809))
        self.step(0,0x808080ff)
        self.step(1<<14)
        self.assertEqual(self.records()[-1],(1,9900))
        self.step()
        self.cold_start()
        self.assertAlmostEqual(self.step(),180*1.1809,delta=1)

    def test_r39_stick_records_cross_tag_boundary_and_restore_after_reboot(self):
        for value in (98113,100000,131071,199999):
            with self.subTest(value=value):
                # The first two seeds use exactly the r39 four-byte layout.
                tag=0xd2+((value>>16)&1)+((value>>17)<<3)
                lo,hi=value&255,(value>>8)&255
                self.m.nv[0x200:]=bytes((255,))*(4096-0x200)
                self.m.nv[0x200:0x204]=bytes((tag,lo,hi,255^tag^lo^hi))
                self.cold_start()
                self.step(0,0x808080ff)
                self.step(1<<15,0x808080ff)
                expected=min(200000,value+(value+50)//100)
                self.assertEqual(self.records()[-1],(1,expected))
                self.cold_start()
                counts=sum(self.step(0,0x808080ff) for _ in range(100))
                self.assertAlmostEqual(counts,(1250*expected//10000)*1000/1024,delta=1)

    def test_wheel_priority_persistence_and_capped_output(self):
        # Tilt with the gyro active and adjust using LT. Four-axis byte order is LX, LY, RX, RY.
        tilted=0xff808080
        self.step(1<<14,tilted)
        first=bytes(self.m.nv[0x200:0x204])
        self.assertEqual(first[0],0xe0)
        self.assertEqual(int.from_bytes(first[1:3],'little'),9900)
        self.step(0,tilted)
        # On the horizontal right stick, RT also targets the wheel, even with LX at full tilt.
        self.step(1<<15,0x80ff80ff)
        second=bytes(self.m.nv[0x204:0x208])
        self.assertEqual(second[0],0xe0)
        self.assertEqual(int.from_bytes(second[1:3],'little'),9999)
        for _ in range(45):
            self.step(0,tilted)
            self.step(1<<15,tilted)
        words=self.m.nv[0x200:]
        latest=next(words[i:i+4] for i in range(len(words)-4,-1,-4) if words[i]==0xe0)
        self.assertEqual(int.from_bytes(latest[1:3],"little"),30000)
        self.cold_start()
        self.assertEqual(self.step(),180)
        self.step(0,tilted)
        fine=0
        for _ in range(100):
            self.step(0,tilted)
            fine+=int.from_bytes(self.m.u.mem_read(OUTPUT+6,2),'little',signed=True)
        self.assertEqual(fine,-45*120)

    def test_separate_wheel_records_survive_native_settings_save(self):
        def wheel_record(tag,value):
            lo,hi=value&255,value>>8
            return bytes((tag,lo,hi,255^tag^lo^hi))
        pair=wheel_record(0xe0,5000)+wheel_record(0xe1,2500)
        self.m.nv[0x200:0x208]=pair
        self.m.call('lite2_config_erase',0x107d000,4096)
        self.m.call('lite2_config_write',0x107d000,0x22400,33)
        self.assertEqual(self.m.nv[0x200:0x208],pair)
        self.cold_start()
        total=0
        for _ in range(100):
            self.step(0,0x01808080)
            total+=self.m.u.mem_read(OUTPUT+6,1)[0]
        self.assertEqual(total,960)

    def test_torn_record_factory_save_and_caps(self):
        for _ in range(125):
            self.step(1<<14);self.step()
        self.assertEqual(self.records()[-1],(0,500))
        pos=0x200+4*len(self.records())
        self.m.nv[pos:pos+2]=bytes((0xd0,28))
        self.cold_start()
        self.assertEqual(self.step(),9)
        for _ in range(150):
            self.step(1<<15);self.step()
        self.assertEqual(self.records()[-1],(0,200000))
        self.cold_start()
        self.m.u.mem_write(0x22400,bytes(range(132)))
        self.m.call('lite2_config_erase',0x107d000,4096)
        self.m.call('lite2_config_write',0x107d000,0x22400,33)
        self.assertEqual(self.m.nv[:132],bytes(range(132)))
        self.cold_start()
        self.assertEqual(self.step(),3600)

    def test_independent_usb_and_wireless_legacy_migration(self):
        # Start at the previous maximum using a legacy gain record.
        self.m.nv[0x200:0x204] = bytes((100, 100, 155, 155))
        u = self.m.u
        def stub(uc, address, size, result):
            uc.reg_write(UC_ARM_REG_R0, result)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        for address in (0x1020a80, 0x101d60c, 0x1021b64, 0x101c668,
                        0x101cf18, 0x101ce4c, 0x10221a4):
            u.hook_add(UC_HOOK_CODE, stub, int(address == 0x1020a80),
                       begin=address, end=address)
        u.mem_map(0xe000e000, 0x1000)
        def plug_usb():
            self.m.call('lite2_dispatch', 0, 0x80808080, 0)
        def increase(axes):
            for _ in range(105):
                self.step(1<<15, axes)
                self.step(0, axes)
        plug_usb()
        self.assertEqual(self.step(), 900)
        increase(0x80808080)
        self.assertEqual(self.step(), 3600)
        self.assertEqual(self.step(1<<14), 3564)
        self.step()
        increase(0x80808080)
        self.step(0,0x808080ff)
        increase(0x808080ff)
        self.assertAlmostEqual(sum(self.step(0,0x808080ff) for _ in range(100)),
                               1250*20*1000/1024, delta=1)
        self.m.call('lite2_config_erase',0x107d000,4096)
        self.m.call('lite2_config_write',0x107d000,0x22400,33)
        self.cold_start()
        self.assertEqual(self.step(),900)
        self.assertEqual(self.step(1<<15),909)
        self.assertAlmostEqual(sum(self.step(0,0x808080ff) for _ in range(100)),
                               1250*5*1000/1024, delta=1)
        self.cold_start()
        plug_usb()
        self.assertEqual(self.step(),3600)
        self.assertAlmostEqual(sum(self.step(0,0x808080ff) for _ in range(100)),
                               1250*20*1000/1024, delta=1)

    def test_slot_lease_preserves_raw_settings_and_releases(self):
        addr=0x22500
        for transport,prefix in ((1,b""),(2,bytes((0xa2,)))):
            self.m.u.mem_write(addr,prefix+bytes((4,16)))
            self.m.call("lite2_slot_output",addr,len(prefix)+2,transport)
            self.step(1<<14)
            self.step()
            self.step(1<<15,0x808080ff)
            self.step()
            self.assertEqual(self.m.nv,bytes((255,))*4096)
            self.m.u.mem_write(addr,prefix+bytes((4,0)))
            self.m.call("lite2_slot_output",addr,len(prefix)+2,transport)
        self.cold_start()
        self.m.u.mem_write(addr,bytes((4,16)))
        self.m.call("lite2_slot_output",addr,2,1)
        for _ in range(201):self.step()
        self.assertEqual(self.step(1<<14),178)

    def test_full_log_and_orderly_poweroff(self):
        record=bytes((20,20,235,235))
        self.m.nv[0x200:]=record*896
        self.assertEqual(self.step(1<<14),178)
        self.assertEqual(self.m.u.mem_read(0x201ba,1),bytes((1,)))
        self.m.call('lite2_config_erase',0x107d000,4096)
        self.m.call('lite2_config_write',0x107d000,0x22400,33)
        self.cold_start()
        self.assertEqual(self.step(),178)

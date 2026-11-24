import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
from build_ble_discovery import BASE, OFF, build, profile, XY, BUTTON_XY, motion_image
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2


class PairingProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = build()

    def test_services(self):
        table = profile()["hid_attributes"]
        attrs = []
        pos = 0
        while table[pos:pos+2] != bytes(2):
            handle, width, uuid, size = struct.unpack_from("<HBHB",table,pos)
            self.assertEqual((handle,width),(len(attrs)+1,2))
            attrs.append((uuid,table[pos+6:pos+6+size]))
            pos += 6+size
        self.assertEqual(set(table[pos:]),{0})
        self.assertIn((0x2800,b"\x12\x18"),attrs)
        self.assertIn((0x2a4b,XY),attrs)
        self.assertEqual(attrs[-4:],[(0x2803,b"\x12"),(0x2a4d,bytes(4)),(0x2902,bytes(2)),(0x2908,b"\x01\x01")])

    def test_mode_gating_and_resubscription(self):
        regs=(UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2)
        for mode, reason, discover, expected in ((1,0x20221031,1,(1,1,0)),(0,0,1,(1,1,0)),
                (0,0x20221031,0,(0,0,0)),(0,0x20221031,1,(0,0,1))):
            u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
            u.mem_map(BASE,0x20000)
            u.mem_write(BASE,self.image)
            u.mem_map(0x20000,0x10000)
            u.mem_map(0xc4000,0x1000)
            u.mem_write(0x20184,bytes([mode]))
            u.mem_write(0x2fff8,struct.pack("<I",reason))
            u.mem_write(0xc4000,b"\xbb"*4096)
            radio=bytearray(u.mem_read(0xc4000,4096))
            for iteration in range(2):
                for reg,val in zip(regs,(discover,discover,0)):
                    u.reg_write(reg,val)
                u.reg_write(UC_ARM_REG_SP,0x2f000)
                u.reg_write(UC_ARM_REG_LR,0x1018101)
                u.emu_start((BASE+OFF)|1,0x101ab8c,count=4000)
                self.assertEqual(tuple(u.reg_read(r) for r in regs),expected)
                if expected[-1] and iteration==0:
                    for dest,k in ((0x975,'hid_attributes'),(0x341,'advertisement'),
                                   (0x361,'scan_response'),(0x381,'gap_name')):
                        data=profile()[k]
                        radio[dest:dest+len(data)]=data
                self.assertEqual(u.mem_read(0xc4000,4096),radio)
                # A host-written configuration byte must survive a repeat call.
                cccd=0x975+profile()["hid_attributes"].index(bytes.fromhex("13 00 02 02 29 02"))+6
                radio[cccd]=1
                u.mem_write(0xc4000+cccd,b"\x01")

    def test_stick_packet_and_ack_timeout(self):
        self.check_packet(False)

    def test_cursor_packet_and_ack_timeout(self):
        self.assertIn(BUTTON_XY,profile(True)["hid_attributes"])
        self.check_packet(True)

    def check_packet(self,mouse):
        u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        u.mem_map(BASE,0x20000)
        u.mem_write(BASE,bytes(motion_image(build(mouse),mouse)))
        u.mem_map(0x20000,0x10000)
        u.mem_map(0xc4000,0x1000)
        u.mem_write(0xc4382,b"L")
        keys=0
        sent=[]
        lights=[]
        def stub(uc,addr,size,user):
            result=0
            if addr==0x0101cf18: lights.append(uc.reg_read(UC_ARM_REG_R1))
            if addr==0x01021b64: result=keys
            if addr==0x01020ac4: result=0x808000ff
            if addr==0x01023480: result=1
            if addr==0x010223b8:
                req=uc.mem_read(uc.reg_read(UC_ARM_REG_R0),7)
                self.assertEqual(req[:3],bytes([0,9,7 if mouse else 6]))
                sent.append(bytes(uc.mem_read(int.from_bytes(req[3:],"little"),req[2])))
                result=1
            uc.reg_write(UC_ARM_REG_R0,result)
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        for addr in (0x0102d4cc,0x01020a80,0x01021b64,0x01020ac4,0x01023480,0x01018f0a,0x01018ee8,0x010223b8,0x0101cf18,0x0101ce4c):
            u.hook_add(UC_HOOK_CODE,stub,begin=addr,end=addr)
        for keys in (0,0x20,0x20,0x20):
            u.reg_write(UC_ARM_REG_SP,0x2f000)
            u.reg_write(UC_ARM_REG_LR,0x1018101)
            u.emu_start(BASE+len(self.image)+1,0x1018100,count=2000)
            self.assertEqual(u.reg_read(UC_ARM_REG_PC),0x1018100)
        self.assertEqual(sent,[struct.pack("<HBhh",18,0,15,-16) if mouse else struct.pack("<Hhh",18,15,-16)])
        if mouse: self.assertEqual(lights,[0x100,0x80,0x4000])

    def test_app_free_pointer_sources(self):
        from build_ble_discovery import decrypt
        u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        u.mem_map(BASE, 0x20000)
        u.mem_write(BASE, decrypt((ROOT/'build/Lite2AirMouse-r50-ble-pointer.dat').read_bytes()))
        u.mem_map(0x20000, 0x10000)
        u.mem_map(0xc4000, 0x1000)
        table = profile(True, True)['hid_attributes']
        u.mem_write(0xc4975, table)
        status = 0xc4975 + table.index(b'000000')
        reports = []
        gyro = 0
        def hardware(uc, addr, size, user):
            a = uc.reg_read(UC_ARM_REG_R0)
            b = uc.reg_read(UC_ARM_REG_R1)
            value = 0
            if addr == 0x01020ac4:
                value = 0xffffffff  # The stock scanner reports "unchanged".
            elif addr == 0x01023480:
                value = int(b == 10)
            elif addr == 0x0101d60c:
                uc.mem_write(a, struct.pack('<6h', 0, 0, 4096, 0, 0, gyro))
                value = 1
            elif addr == 0x01018f0a:
                value = 1
            elif addr == 0x010223b8:
                req = uc.mem_read(a, 7)
                self.assertEqual(req[:3], bytes([0, 9, 7]))
                report = struct.unpack('<HBhh', uc.mem_read(int.from_bytes(req[3:], 'little'), 7))
                self.assertEqual(report[:2], (17, 0))
                reports.append(report[2:])
                value = 1
            uc.reg_write(UC_ARM_REG_R0, value)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
        for addr in (0x0102d4cc, 0x01020a80, 0x01023480, 0x01021b64, 0x01020ac4,
                     0x0101d60c, 0x01018f0a, 0x01018ee8, 0x010223b8):
            u.hook_add(UC_HOOK_CODE, hardware, begin=addr, end=addr)
        def tick(axes):
            u.mem_write(0x201a0, struct.pack('<I', axes))
            u.reg_write(UC_ARM_REG_SP, 0x2f000)
            u.reg_write(UC_ARM_REG_LR, 0x1018101)
            u.emu_start(BASE+len(self.image)+1, 0x1018100, count=100000)
            self.assertEqual(u.reg_read(UC_ARM_REG_PC), 0x1018100)
        for _ in range(120):
            tick(0x80808080)
        self.assertEqual(reports, [])
        gyro = -2048
        for _ in range(5):
            tick(0x80808080)
        self.assertTrue(reports and all(x > 0 and y == 0 for x, y in reports), reports)
        reports.clear()
        gyro = 0
        for _ in range(6):
            tick(0x808080ff)
        self.assertEqual(len(reports), 6)
        self.assertTrue(all(x > 0 and y == 0 for x, y in reports), reports)
        self.assertEqual(bytes(u.mem_read(status+2, 2)), b'3F')

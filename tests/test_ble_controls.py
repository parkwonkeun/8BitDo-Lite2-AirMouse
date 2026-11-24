import struct
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'work/python-libs')]
from build_ble_discovery import BASE, decrypt
from build_ble_controls import database
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2

class BleControls(unittest.TestCase):
    image_name="Lite2AirMouse-r51-ble-controls.dat"
    def setUp(self):
        self.u=u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        u.mem_map(BASE,0x20000)
        u.mem_write(BASE,decrypt((ROOT/'build'/self.image_name).read_bytes()))
        u.mem_map(0x20000,0x10000)
        u.mem_map(0xc4000,0x1000)
        u.mem_write(0xc4000, bytes(range(256))*16)
        u.mem_write(0xc4440+0x4e,b'\x75\x49')
        u.mem_write(0xc4547,b'\x83\x46')
        u.mem_write(0x2fff8,struct.pack('<I',0x20221031))
        self.before=bytes(u.mem_read(0xc4000,4096))
        self.now=self.buttons=self.gyro=0
        self.axes=0x80808080
        self.accept=True
        self.packets=[]
        self.leds=[]
        self.hw_hooks={}
        for a in (0x0101ab8c,0x0102d4cc,0x01020a80,0x01027bac,0x01020ac4,
                  0x01021b64,0x0101d60c,0x010223b8,0x010276a0,0x0102c288):
            self.hw_hooks[a]=u.hook_add(UC_HOOK_CODE,self.io,begin=a,end=a)
    def io(self,u,addr,size,user):
        a=u.reg_read(UC_ARM_REG_R0)
        val=0
        if addr==0x01027bac: val=self.now
        elif addr==0x01020ac4: val=0xffffffff
        elif addr==0x01021b64: val=self.buttons
        elif addr==0x0101d60c:
            u.mem_write(a,struct.pack('<6h',0,0,4096,0,0,self.gyro))
            val=1
        elif addr==0x010276a0:
            u.mem_write(u.reg_read(UC_ARM_REG_R1),b'\xff'*u.reg_read(UC_ARM_REG_R2))
        elif addr==0x010223b8:
            req=u.mem_read(a,7)
            self.assertEqual(req[:2],bytes((0,9)))
            if self.accept:
                self.packets.append(bytes(u.mem_read(int.from_bytes(req[3:],'little'),req[2])))
                val=1
        elif addr==0x0102c288:self.leds.append(a)
        u.reg_write(UC_ARM_REG_R0,val)
        u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
    def call(self,site,*args):
        u=self.u
        u.reg_write(UC_ARM_REG_SP,0x2f900)
        for reg,arg in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2),args):
            u.reg_write(reg,arg)
        u.emu_start(site|1,site+4,count=120000)
        self.assertEqual(u.reg_read(UC_ARM_REG_PC),site+4)
    def install(self):
        self.call(0x0101c662,1,1,0)
        table,self.handles=database()
        self.assertEqual(self.u.mem_read(0xc4683,len(table)),table)
    def tick(self,ms=8):
        self.now+=ms
        self.u.mem_write(0x201a0,struct.pack('<I',self.axes))
        self.call(0x01026270)
    def test_restore_radio_configuration(self):
        self.install()
        self.call(0x0102667e,1,1,0)
        self.u.mem_write(0x20184,b'\1')
        self.call(0x0102667e,1,1,0)
        self.assertEqual(self.u.mem_read(0xc4000,4096),self.before)
    def test_controls_and_release_while_moving(self):
        self.install()
        m,k,c=self.handles
        for _ in range(140):self.tick()
        self.assertEqual(self.packets,[])
        self.gyro=-2048
        for _ in range(8):self.tick()
        self.assertTrue(any(p[0]==m and int.from_bytes(p[3:5],'little',signed=True)>0 for p in self.packets))
        self.axes=0xff8080ff
        self.gyro=0
        self.buttons=0x10|0x1000|0x200|0x2000|0x400
        for _ in range(80):self.tick()
        self.assertIn(bytes([k,0,17]),self.packets)
        self.assertIn(bytes([c,0,5]),self.packets)
        self.assertTrue(any(p[0]==m and p[2]==2 and p[7]!=0 for p in self.packets))
        self.buttons=0x20|0x800|0x10000
        self.tick()
        self.assertIn(bytes([k,0,0]),self.packets)
        self.assertIn(bytes([c,0,10]),self.packets)
        self.assertTrue(any(p[0]==m and p[2]==1 for p in self.packets))
        self.buttons=0
        self.tick()
        self.assertIn(bytes([c,0,0]),self.packets)
    def test_backpressure_and_sample_interval(self):
        self.install()
        self.axes=0x808080ff
        self.tick()
        self.packets.clear()
        self.tick(3)
        self.assertFalse(self.packets)
        self.tick(5)
        self.assertEqual(len(self.packets),1)
        one=int.from_bytes(self.packets[0][3:5],'little')
        self.assertGreater(one,0)
        self.accept=False
        self.tick()
        self.accept=True
        self.tick()
        total=int.from_bytes(self.packets[-1][3:5],'little')
        self.assertAlmostEqual(total,2*one,delta=2)

class BleReconnect(BleControls):
    image_name="Lite2AirMouse-r52-ble-reconnect.dat"

    def test_home_power_on_with_saved_classic_peer(self):
        # Execute the stock power-on branch and its real visibility wrapper.
        self.u.mem_write(0x2fff8,bytes(4))
        advertised=[]
        self.u.hook_del(self.hw_hooks[0x0101ab8c])
        def visibility(u,addr,size,user):
            advertised.append(tuple(u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2)))
            self.io(u,addr,size,user)
        self.u.hook_add(UC_HOOK_CODE,visibility,begin=0x0101ab8c,end=0x0101ab8c)
        self.u.reg_write(UC_ARM_REG_SP,0x2f900)
        self.u.emu_start(0x0101b7f7,0x0101b812,count=120000)
        table,_=database()
        self.assertEqual(self.u.mem_read(0xc4683,len(table)),table)
        self.assertEqual(advertised,[(0,0,0),(0,0,1)])
        self.call(0x0102667e,0,0,0)
        self.assertEqual(advertised[-1],(0,0,0))
        self.call(0x0102667e,1,1,0)
        self.assertEqual(advertised[-1],(0,0,1))

    def test_d_mode_uses_existing_host(self):
        self.u.mem_write(0x20184,bytes([1]))
        called=[]
        def saved(u,addr,size,user):
            called.append(addr)
            u.reg_write(UC_ARM_REG_R0,1)
            u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
        self.u.hook_add(UC_HOOK_CODE,saved,begin=0x01021cb4,end=0x01021cb4)
        for at in (0x0101b7f6,0x01019f8a):
            self.call(at)
            self.assertEqual(self.u.reg_read(UC_ARM_REG_R0),1)
        self.assertEqual(len(called),2)

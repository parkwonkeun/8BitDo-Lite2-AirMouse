import struct
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'work/python-libs')]
from build_ble_mode_lights import BASE, TARGET, START, END, EVENT_START, EVENT_END, HOOKS, EVENTS, decrypt
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1

class ReconnectIndicators(unittest.TestCase):
    def setUp(self):
        self.u=u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        self.image=decrypt(TARGET.read_bytes())
        u.mem_map(BASE,0x20000);u.mem_write(BASE,self.image)
        u.mem_map(0x20000,0x10000);u.mem_map(0xc4000,0x1000)
        self.seen=[];self.usb=0
        def native(u,addr,size,user):
            if addr==0x1020a80:u.reg_write(UC_ARM_REG_R0,self.usb)
            else:self.seen.append((addr,u.reg_read(UC_ARM_REG_R0),u.reg_read(UC_ARM_REG_R1)))
            u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
        for a in (0x102da70,0x1020a80,0x102c288,0x102ca3c,0x102ca8c,0x102d9cc):
            u.hook_add(UC_HOOK_CODE,native,begin=a,end=a)
        u.mem_write(0xc448e,b'\x83\x46');self.mode(0)
    def mode(self,number):self.u.mem_write(0x2fe00+34,bytes([number]))
    def ptr(self,at):return struct.unpack_from('<I',self.image,at-BASE)[0]
    def call(self,entry,r0=0,r1=0,end=BASE):
        self.seen.clear()
        self.u.reg_write(UC_ARM_REG_SP,0x2f900);self.u.reg_write(UC_ARM_REG_LR,end|1)
        self.u.reg_write(UC_ARM_REG_R0,r0);self.u.reg_write(UC_ARM_REG_R1,r1)
        self.u.emu_start(entry|1,end,count=1000)
        self.assertEqual(self.u.reg_read(UC_ARM_REG_PC),end)
        return self.seen[-1]
    def test_only_led_paths_changed(self):
        r52=decrypt((ROOT/'build/Lite2AirMouse-r52-ble-reconnect.dat').read_bytes())
        allowed=set(range(START-BASE,END-BASE))|set(range(EVENT_START-BASE,EVENT_END-BASE))
        for a in (*HOOKS,*EVENTS):allowed.update(range(a-BASE,a-BASE+4))
        self.assertEqual(len(self.image),len(r52))
        self.assertTrue(all(i in allowed for i,(a,b) in enumerate(zip(self.image,r52)) if a!=b))
    def test_d_to_s_and_late_blinks(self):
        self.u.mem_write(0x20184,b'\x01')
        self.assertEqual(self.call(self.ptr(0x10211ac),27)[:2],(0x102ca8c,27))
        self.u.mem_write(0x20184,b'\x00')
        for mode in (0,1,2):
            self.mode(mode)
            self.assertEqual(self.call(self.ptr(0x10211ac))[:2],(0x102c288,mode))
            for on in (0,1,0):
                self.assertEqual(self.call(0x1022428,0xc180,on)[:2],(0x102c288,mode))
    def test_pairing_and_rearm_after_connect_or_encryption(self):
        self.u.mem_write(0x2fff8,struct.pack('<I',0x20221031))
        for site in EVENTS:
            self.call(self.ptr(0x101820c))
            self.assertEqual(self.call(self.ptr(0x10211ac),17)[:2],(0x102ca8c,17))
            self.assertEqual(self.call(0x1022428,0xc180,1),(0x102ca3c,0xc180,1))
            self.call(site,0x1019cc4,end=site+4)
            for _ in range(3):
                self.assertEqual(self.call(self.ptr(0x10211ac))[:2],(0x102c288,0))
                self.assertEqual(self.call(0x1022428,0xc180,0)[:2],(0x102c288,0))
        self.usb=1
        self.assertEqual(self.call(0x1022428,128,0),(0x102ca3c,128,0))

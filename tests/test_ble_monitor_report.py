import struct
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools'),str(ROOT/'work/python-libs')]
import test_ble_controls as base
from build_ble_controls import database
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_R0

class BleMonitorReport(unittest.TestCase):
    sticks=False
    def setUp(self):
        self.pad=base.BleControls()
        self.pad.image_name='Lite2AirMouse-r54-ble-sticks.dat' if self.sticks else 'Lite2AirMouse-r53-ble-monitor.dat'
        self.pad.setUp()
        self.pad.u.mem_write(0x2fff8,bytes(4))
        self.pad.call(0x0101c662,1,1,0)
        table,handles=database(True,self.sticks)
        self.mouse=handles[0]
        self.assertEqual(self.pad.u.mem_read(0xc4683,len(table)),table)
        def save(u,addr,size,user):
            u.reg_write(UC_ARM_REG_R0,1)
            u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
        for addr in (0x1027704,0x101c6d2,0x101c6ae):
            self.pad.u.hook_add(UC_HOOK_CODE,save,begin=addr,end=addr)
    def frame(self,mask):
        self.pad.packets.clear()
        self.pad.buttons=mask
        self.pad.tick()
        frames=[p for p in self.pad.packets if p[0]==self.mouse]
        self.assertEqual(len(frames),1)
        handle,clicks,x,y,wheel,stamp=struct.unpack('<HBhhbI',frames[0])
        self.assertEqual(stamp-1&0x3ffff,mask)
        self.assertIn((stamp-1)>>18,(1,2,3))
        return clicks
    def test_each_switch_and_chords_emit_even_without_motion(self):
        self.frame(0)
        for bit in range(18):
            clicks=self.frame(1<<bit)
            if bit in (4,5):self.assertEqual(clicks,2 if bit==4 else 1)
            self.frame(0)
        for combo in ((1<<10)|(1<<11),9,(1<<14)|(1<<15)):
            self.frame(combo)
            self.frame(0)
        self.pad.packets.clear()
        for _ in range(80):self.pad.tick()
        self.assertFalse(self.pad.packets)
    def test_retry_and_motion(self):
        self.frame(0)
        self.pad.accept=False;self.pad.buttons=9;self.pad.tick()
        self.pad.accept=True;self.frame(9)
        self.pad.axes=0x808080ff
        self.frame(0)
        self.assertGreater(struct.unpack_from('<h',self.pad.packets[-1],3)[0],0)
    def test_restore_configuration(self):
        self.pad.u.mem_write(0x20184,b'\1')
        self.pad.call(0x0102667e,1,1,0)
        self.assertEqual(self.pad.u.mem_read(0xc4000,4096),self.pad.before)

class BleMonitorSensors(BleMonitorReport):
    sticks=True
    def test_sensor_reads_do_not_add_mouse_reports(self):
        table,_=database(True,True)
        address=0xc4683+len(table)-22
        self.pad.axes=0x8080ff80
        self.pad.gyro=170
        self.pad.tick()
        tick,axes,*imu=struct.unpack('<II6h',self.pad.u.mem_read(address,20))
        self.assertEqual(tick,self.pad.now)
        self.assertEqual(axes,0x8080ff80)
        self.assertEqual(imu,[0,0,4096,0,0,170])
        self.pad.axes=0x80808080
        self.pad.gyro=0
        self.pad.tick()
        self.assertEqual(struct.unpack('<II6h',self.pad.u.mem_read(address,20))[1:],(0x80808080,0,0,4096,0,0,0))

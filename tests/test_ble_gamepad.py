import unittest
import test_ble_controls as controls
from build_ble_controls import database
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_R0


class BleGamepad(unittest.TestCase):
    def setUp(self):
        self.pad=controls.BleControls()
        self.pad.image_name='Lite2AirMouse-r55-ble-gamepad.dat'
        self.pad.setUp()
        self.mode(1)
        self.table,self.handles=database(gamepad=True)

    def mode(self,value):
        self.pad.u.mem_write(0x20184,bytes([value]))
        self.pad.call(0x0101c662,1,1,0)

    def frame(self,buttons):
        pad=self.pad
        pad.buttons=buttons
        pad.packets.clear()
        pad.tick()
        self.assertEqual(len(pad.packets),1)
        packet=pad.packets[0]
        self.assertEqual(packet[:2],bytes([self.handles[0],0]))
        self.assertEqual(len(packet),11)
        return packet[2:]

    def test_profiles_and_advertising(self):
        p=self.pad
        for mode,table,name,appearance in ((1,self.table,b'Lite2 Pad',0xc4),(0,database(True,True)[0],b'Lite2 BLE',0xc2),(1,self.table,b'Lite2 Pad',0xc4)):
            self.mode(mode)
            self.assertEqual(p.u.mem_read(0xc4683,len(table)),table)
            for address in (0xc434f,0xc4364,0xc4382):
                self.assertEqual(p.u.mem_read(address,9),name)
            self.assertEqual(p.u.mem_read(0xc4347,1),bytes([appearance]))
        self.mode(2)
        self.assertEqual(p.u.mem_read(0xc4000,4096),p.before)

    def test_every_button_and_hat(self):
        self.assertEqual(self.frame(0),bytes([0,0,128,128,128,128,128,0,0]))
        for source,destination in ((12,0),(13,1),(5,3),(4,4),(10,6),(11,7),(14,8),(15,9),(3,10),(0,11),(17,12),(1,13),(2,14),(16,15)):
            report=self.frame(1<<source)
            self.assertEqual(int.from_bytes(report[:2],'little'),1<<destination)
            self.assertEqual(report[7:],bytes([255*(source==15),255*(source==14)]))
        for direction,buttons in enumerate((512,576,64,320,256,384,128,640,0)):
            self.assertEqual(self.frame(buttons)[2],direction<<4)

    def test_sticks_retry_and_periodic_resync(self):
        self.frame(0)
        self.pad.axes=0xff00ff00
        self.pad.accept=False
        self.pad.tick()
        self.pad.accept=True
        self.assertEqual(self.frame(0)[3:7],bytes([0,255,0,255]))
        self.pad.packets.clear()
        self.pad.tick(249)
        self.assertFalse(self.pad.packets)
        self.pad.tick(8)
        self.assertEqual(len(self.pad.packets),1)

    def test_d_usb_suppresses_ble(self):
        p=self.pad
        p.u.hook_del(p.hw_hooks[0x01020a80])
        def usb(u,addr,size,data):
            u.reg_write(UC_ARM_REG_R0,1)
            u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
        p.u.hook_add(UC_HOOK_CODE,usb,begin=0x01020a80,end=0x01020a80)
        p.buttons=0x4001
        p.tick()
        self.assertFalse(p.packets)

    def test_s_reports_after_leaving_gamepad(self):
        self.frame(0)
        self.mode(0)
        p=self.pad
        p.buttons=(1<<5)|(1<<16)
        p.axes=0x808080ff
        p.gyro=190
        p.packets.clear()
        p.tick()
        table,handles=database(True,True)
        mouse=[x for x in p.packets if x[0]==handles[0]]
        self.assertEqual(len(mouse),1)
        self.assertEqual(mouse[0][2],1)
        self.assertEqual((int.from_bytes(mouse[0][8:],'little')-1)&0x3ffff,p.buttons)
        self.assertIn(bytes([handles[2],0,2]),p.packets)
        self.assertEqual(int.from_bytes(p.u.mem_read(0xc4683+len(table)-4,2),'little'),190)
        self.mode(1)
        self.assertEqual(self.frame(0)[1],0)

class DualBle(unittest.TestCase):
    image_name="Lite2AirMouse-r56-dual-ble.dat"
    def setUp(self):
        self.pad=controls.BleControls()
        self.pad.image_name=self.image_name
        self.pad.setUp()

    def test_profiles_and_transition_gate(self):
        p=self.pad
        for mode,name in ((1,b'Lite2 GamePAD'),(0,b'Lite2 Air Mouse'),(1,b'Lite2 GamePAD')):
            p.u.mem_write(0x20184,bytes([mode]))
            p.call(0x0101c662,1,1,0)
            table,handles=database(not mode,not mode,gamepad=bool(mode),dual=True)
            self.assertEqual(p.u.mem_read(0xc4683,len(table)),table)
            for addr in (0xc434f,0xc4364,0xc4382):
                self.assertEqual(p.u.mem_read(addr,len(name)),name)
            self.assertEqual(p.u.mem_read(0xc434d,2),bytes([len(name)+1,9]))
            self.assertEqual(p.u.mem_read(0xc4381,1),bytes([len(name)]))
            p.packets.clear()
            p.buttons=(mode<<29)|16
            p.tick()
            self.assertFalse(p.packets)
            p.buttons=((1-mode)<<29)|16
            p.tick()
            self.assertTrue(p.packets)
        p.u.mem_write(0x20184,bytes([2]))
        p.call(0x0101c662,1,1,0)
        self.assertEqual(p.u.mem_read(0xc4000,4096),p.before)

    def test_mode_radio_addresses(self):
        from unicorn.arm_const import UC_ARM_REG_SP
        p=self.pad
        for switch,mask in ((0,128),(1,64)):
            sp=0x2f800
            p.buttons=switch<<29
            p.u.reg_write(UC_ARM_REG_SP,sp)
            p.u.mem_write(sp,bytes([0xa5])*64)
            p.u.mem_write(sp+4,bytes([250,33,40,11,4,240]))
            p.u.mem_write(sp+12,bytes.fromhex('000000d817e4'))
            p.u.emu_start(0x0101adf5,0x0101ae2a,count=150)
            self.assertEqual(p.u.reg_read(UC_ARM_REG_PC),0x0101ae2a)
            self.assertEqual(p.u.mem_read(sp+12,6),bytes([5^mask,37,24,0xd8,0x17,0xe4]))
            self.assertEqual(p.u.mem_read(sp+34,1),bytes(1))

    def test_host_disconnect_rearms_only_selected_role(self):
        p=self.pad
        events=[]
        def intercept(u,addr,size,data):
            events.append(addr)
            u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))
        for a in (0x0102141c,0x01018fb4,0x0101f664):
            p.u.hook_add(UC_HOOK_CODE,intercept,begin=a,end=a)
        p.u.mem_write(0x20184,bytes([1]))
        p.call(0x0101c662,1,1,0)
        vis=[]
        p.u.hook_del(p.hw_hooks[0x0101ab8c])
        def advertise(u,addr,size,data):
            vis.append(tuple(u.reg_read(r) for r in (controls.UC_ARM_REG_R0,controls.UC_ARM_REG_R1,controls.UC_ARM_REG_R2)))
            intercept(u,addr,size,data)
        p.u.hook_add(UC_HOOK_CODE,advertise,begin=0x0101ab8c,end=0x0101ab8c)
        p.call(0x0101a3ae)
        p.tick()
        self.assertEqual(vis,[(0,0,1)])
        p.call(0x0101db90)
        self.assertIn(0x01018fb4,events)
        self.assertEqual(vis[-1],(0,0,0))
        p.call(0x0101a3ae)
        p.packets.clear()
        p.tick(400)
        self.assertFalse(p.packets)
        self.assertEqual(vis[-1],(0,0,0))

    def test_boot_advertises_the_selected_role(self):
        from unicorn.arm_const import UC_ARM_REG_SP
        for mode in (0,1):
            p=controls.BleControls()
            p.image_name=self.image_name
            p.setUp()
            p.u.mem_write(0x20184,bytes([mode]))
            p.u.mem_write(0x2fff8,bytes(4))
            p.u.reg_write(UC_ARM_REG_SP,0x2f900)
            p.u.emu_start(0x0101b7f7,0x0101b812,count=120000)
            tbl,_=database(not mode,not mode,gamepad=bool(mode),dual=True)
            self.assertEqual(p.u.mem_read(0xc4683,len(tbl)),tbl)

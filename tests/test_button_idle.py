"""Exercise the actual r58 Thumb scan callback and link events in both BLE modes."""
import hashlib
import struct

from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0
import test_ble_gamepad as gamepad
from test_ble_controls import ROOT, decrypt
from release_inputs import R58_SHA


class ButtonIdle(gamepad.DualBle):
    image_name = 'Lite2AirMouse-r58-button-idle-public.dat'

    def setUp(self):
        super().setUp()
        self.shutdown = []
        self.stock_checks = []
        self.usb = False
        p = self.pad
        p.u.hook_del(p.hw_hooks[0x01020a80])

        def native(u, address, size, data):
            if address == 0x01026958:
                self.shutdown.append(u.reg_read(UC_ARM_REG_R0))
            elif address == 0x0101ef38:
                self.stock_checks.append(1)
            elif address == 0x01020a80:
                u.reg_write(UC_ARM_REG_R0, int(self.usb))
            u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))
        for addr in (0x01026958, 0x0101ef38, 0x01020a80, 0x0102141c):
            p.u.hook_add(UC_HOOK_CODE, native, begin=addr, end=addr)

    def clock(self, ticks, period=10):
        self.pad.u.mem_write(0x20034, struct.pack('<I', ticks & 0xffffffff))
        self.pad.u.mem_write(0x2022b, bytes([period]))

    def connect(self, mode, start=0):
        self.clock(start)
        self.pad.u.mem_write(0x20184, bytes([mode]))
        self.pad.call(0x01019c8e)

    def scan(self, ticks, buttons, period=10):
        self.clock(ticks, period)
        self.pad.buttons = buttons
        self.pad.call(0x0101deac)
        self.assertEqual(self.pad.u.reg_read(UC_ARM_REG_R0), buttons)

    def test_motion_and_periodic_reports_do_not_extend_either_mode(self):
        for mode in (0, 1):
            with self.subTest(mode=mode):
                self.shutdown.clear()
                self.connect(mode)
                self.pad.call(0x0101c662, 1, 1, 0)
                for t in (1, 1200, 30000, 89999):
                    self.pad.gyro = 2000
                    self.pad.axes = 0xff0080ff
                    self.scan(t, (1-mode) << 29)
                    self.pad.tick()
                    self.pad.call(0x0101cffe)
                self.assertFalse(self.shutdown)
                self.pad.call(0x01019c96)  # A duplicate link event cannot extend life.
                self.scan(90000, (1-mode) << 29)
                self.scan(90001, (1-mode) << 29)
                self.assertEqual(self.shutdown, [0])
                self.assertFalse(self.stock_checks)
                self.pad.call(0x0101a3ae)

    def test_every_button_hold_and_release_restarts_timer(self):
        self.connect(0)
        for bit in range(19):
            t = (bit+1)*200000
            self.scan(t, 1 << bit)
            self.scan(t+90001, 1 << bit)
            self.scan(t+90002, 0)
            self.scan(t+180001, 0)
        self.assertFalse(self.shutdown)
        self.scan(t+180002, 0)
        self.assertEqual(self.shutdown, [0])

    def test_wrap_period_change_usb_and_disconnect(self):
        base = 0xffffff00
        self.connect(1, base)
        self.scan(base+50000, 0, 5)
        self.scan(base+129999, 0, 5)
        self.assertFalse(self.shutdown)
        self.usb = True
        self.scan(base+130000, 0)
        self.pad.call(0x0101cffe)
        self.usb = False
        self.scan(base+219999, 0)
        self.assertFalse(self.shutdown)
        self.pad.call(0x0101a3ae)
        self.scan(base+220000, 0)
        self.pad.call(0x0101cffe)
        self.assertEqual(len(self.stock_checks), 2)
        self.assertFalse(self.shutdown)
        self.connect(0, base+220000)
        self.scan(base+310000, 0)
        self.assertEqual(self.shutdown, [0])

    def test_public_image(self):
        data = (ROOT/'build'/self.image_name).read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), R58_SHA)
        image = decrypt(data)
        self.assertEqual(image[0x15714:0x1571c], bytes([128])*8)
        self.assertEqual(image[-8:], bytes([255])*8)
        self.assertEqual(struct.unpack_from('<I', image, 0x7130)[0], 2000)

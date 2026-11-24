"""Execute the actual linked ARM Thumb instructions using Unicorn.

This is not a hardware/ROM/radio emulator. External adapter calls are mocked.
"""
from pathlib import Path
import json
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'work/python-libs'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import *
from elftools.elf.elffile import ELFFile

STATE, IMU, OUTPUT, SP, STOP = 0x22000, 0x22100, 0x22210, 0x2f000, 0x1000100

class Machine:
    def __init__(self):
        self.u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.u.mem_map(0x1000000, 0x100000)
        self.u.mem_write(0x1018000, (ROOT/'outputs/Lite2-v1.06-decrypted-analysis-only.bin').read_bytes())
        self.u.mem_map(0x20000, 0x10000)
        with (ROOT / 'build/mouse.elf').open('rb') as f:
            elf = ELFFile(f)
            for section in ('.gravity', '.settings', '.text'):
                part=elf.get_section_by_name(section)
                self.u.mem_write(part['sh_addr'],part.data())
            self.symbols = {s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        self.nv = bytearray(b'\xff'*4096)
        def nv_io(uc, address, size, data):
            off=uc.reg_read(UC_ARM_REG_R0)-0x107d000
            buf=uc.reg_read(UC_ARM_REG_R1)
            n=uc.reg_read(UC_ARM_REG_R2)
            if address==0x101c6ae:
                assert off==0 and buf==4096
                self.nv[:]=b'\xff'*4096
            else:
                if address==0x101c6d2: n*=4
                assert 0<=off and off+n<=4096
                if address==0x10276a0: uc.mem_write(buf,bytes(self.nv[off:off+n]))
                else:
                    for i,v in enumerate(uc.mem_read(buf,n)): self.nv[off+i]&=v
            uc.reg_write(UC_ARM_REG_R0,1)
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        for addr in (0x10276a0,0x1027704,0x101c6ae,0x101c6d2):
            self.u.hook_add(UC_HOOK_CODE,nv_io,begin=addr,end=addr)
        self.u.hook_add(UC_HOOK_CODE, self.stop, begin=STOP, end=STOP)
        self.u.mem_write(STATE-16, b'\xa5'*(16+96+16))
        self.call('mouse_reset', STATE)
        assert self.u.mem_read(STATE-16,16) == b'\xa5'*16
        assert self.u.mem_read(STATE+96,16) == b'\xa5'*16

    def stop(self, uc, address, size, data): uc.emu_stop()

    def call(self, name, *args):
        u = self.u
        for r,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args[:4]):
            u.reg_write(r,v)
        u.reg_write(UC_ARM_REG_SP, SP)
        for i,v in enumerate(args[4:]): u.mem_write(SP+4*i,struct.pack('<I',v))
        u.reg_write(UC_ARM_REG_LR, STOP|1)
        preserved = (UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
                     UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11)
        for i,r in enumerate(preserved): u.reg_write(r,0x12340000+i)
        u.emu_start(self.symbols[name]|1, STOP+2, count=100000)
        assert u.reg_read(UC_ARM_REG_PC) == STOP, 'instruction budget exhausted'
        assert u.reg_read(UC_ARM_REG_SP) == SP, 'stack not restored'
        for i,r in enumerate(preserved): assert u.reg_read(r)==0x12340000+i, 'ABI violation'

    def step(self, imu=(0,0,4096,0,0,0), buttons=0, dt=10, axes=0x80808080):
        self.u.mem_write(IMU,struct.pack('<6h',*imu))
        self.u.mem_write(OUTPUT-16,b'\xa5'*41)
        self.call('mouse_step',STATE,IMU,buttons,dt,OUTPUT,axes if axes>0xffff else axes|0x80800000)
        assert self.u.mem_read(OUTPUT-16,16)==b'\xa5'*16
        assert self.u.mem_read(OUTPUT+9,16)==b'\xa5'*16
        assert self.u.mem_read(STATE+96,16)==b'\xa5'*16
        return struct.unpack('<Bhhb3s', self.u.mem_read(OUTPUT,9))

    def calibrate(self, sample=(0,0,4096,0,0,0)):
        for _ in range(100): self.step(sample)
        assert self.u.mem_read(STATE+32,1)==b'\x01'

class CoreTests(unittest.TestCase):
    def setUp(self): self.m=Machine()

    def test_stock_division_abi(self):
        for a in (0, 1, -37, 12345678, -2147483648):
            for b in (3, -7, 192, 2500):
                self.m.call('__aeabi_idivmod', a, b)
                quotient = abs(a) // abs(b) * (-1 if (a < 0) != (b < 0) else 1)
                self.assertEqual(self.m.u.reg_read(UC_ARM_REG_R0), quotient & 0xffffffff)
                self.assertEqual(self.m.u.reg_read(UC_ARM_REG_R1), (a - b * quotient) & 0xffffffff)

    def test_click_hold_release_drag_and_stick(self):
        self.m.calibrate()
        imu=(0,0,4096,0,0,-2048)
        for button in (0x20,0x10):
            self.assertEqual(self.m.step(imu,button)[0:3],(1 if button==0x20 else 2,0,0))
            self.assertEqual(self.m.step(imu,0)[1:3],(180,0))
        for _ in range(20):
            self.assertEqual(self.m.step(imu,0x20)[0:3],(1,0,0))
        self.assertEqual(self.m.step(imu,0x20)[0:3],(1,180,0))
        self.assertEqual(self.m.step(imu,0)[0:3],(0,180,0))
        result=self.m.step(imu,0x20,axes=0x808080ff)
        self.assertEqual(result[0],1)
        self.assertGreater(result[1],0)

    def test_real_stationary_capture(self):
        log=json.loads((ROOT/'tests/data/stationary-scan.json').read_text())
        last=0; motion=0
        for packet in log['reports']:
            now=round(packet['t_seconds']*1000)
            result=self.m.step(packet['samples'][0],dt=now-last)
            motion+=abs(result[1])+abs(result[2]);last=now
        self.assertEqual(motion,0)
        self.assertEqual(self.m.u.mem_read(STATE+32,1),b'\x01')

    def test_tracking_during_grip_change_and_linear_acceleration(self):
        import math
        self.m.calibrate()
        # Roll around body X from flat to vertical in one second. The pad
        # measures +Y gravity when this right-handed rotation reaches 90°.
        for n in range(1,101):
            angle=n*math.pi/200
            frame=self.m.step((0,round(4096*math.sin(angle)),round(4096*math.cos(angle)),1282,0,0))
            self.assertEqual(frame[1:3],(0,0))
        for _ in range(12):
            # Translation biases measured acceleration while yaw remains constant.
            frame=self.m.step((4096,4096,0,0,-1024,0))
            self.assertAlmostEqual(frame[1],90,delta=1)
            self.assertLessEqual(abs(frame[2]),1)
        for _ in range(30):
            self.assertEqual(self.m.step((0,4096,0,0,0,0))[1:3],(0,0))

    def test_accelerometer_corrects_a_tilt_prediction_error(self):
        self.m.calibrate()
        # Simulate missed roll motion. Gravity now points 30 degrees away
        # from the current prediction, but the gyro has returned to rest.
        level=(0,2048,3547,0,0,0)
        self.m.step(level)
        first=struct.unpack('<3h',self.m.u.mem_read(STATE+52,6))
        self.assertGreater(first[1],0)
        self.assertLess(first[1],2048)
        for _ in range(350):
            self.assertEqual(self.m.step(level)[1:3],(0,0))
        settled=struct.unpack('<3h',self.m.u.mem_read(STATE+52,6))
        self.assertAlmostEqual(settled[1],2048,delta=30)
        self.assertAlmostEqual(settled[2],3547,delta=30)

    def test_integrated_motion_and_sign(self):
        self.m.calibrate()
        for sign in (1,-1):
            self.m=Machine();self.m.calibrate()
            results=[self.m.step((0,0,4096,0,0,-2048*sign)) for _ in range(100)]
            self.assertEqual(sum(r[1] for r in results),18000*sign)
            self.assertEqual(sum(r[2] for r in results),0)
            results=[self.m.step((0,0,4096,0,1024*sign,0)) for _ in range(100)]
            self.assertEqual(sum(r[2] for r in results),9000*sign)

    def test_fine_rotation_in_rest_recording(self):
        log=json.loads((ROOT/'tests/data/stationary-scan.json').read_text())
        for rate in (4,-4,8,-8):
            m=Machine()
            last=x_total=y_total=0
            for packet in log['reports']:
                now=round(packet['t_seconds']*1000)
                sample=packet['samples'][0].copy()
                rotating=5000<=now<9000
                if rotating:
                    sample[4]+=rate
                    sample[5]-=rate
                _,x,y,*_=m.step(sample,dt=now-last)
                if rotating:
                    x_total+=x
                    y_total+=y
                last=now
            self.assertGreater(x_total*rate,0)
            self.assertGreater(y_total*rate,0)

    def test_standalone_drift_and_slow_turns(self):
        recording = (ROOT / 'tests/data/r19-stationary.csv').read_text().splitlines()[1:]
        for rate in (0, 4, -4):
            machine = Machine()
            elapsed = total_x = total_y = 0
            for line in recording:
                dt, x, y = map(int, line.split(','))
                elapsed += dt
                turning = 2500 <= elapsed < 4300
                turn = rate if turning else 0
                _, dx, dy, *_ = machine.step((0, 0, 4096, 0, y+turn, -x-turn), dt=dt)
                if turning:
                    total_x += dx
                    total_y += dy
                if rate == 0 or elapsed < 2500 or elapsed > 4800:
                    self.assertEqual((dx, dy), (0, 0))
            if rate:
                self.assertGreater(total_x*rate, 0)
                self.assertGreater(total_y*rate, 0)

    def test_slow_turn_after_prolonged_weak_input(self):
        for sign in (-1,1):
            m=Machine()
            m.calibrate()
            for _ in range(1500):
                m.step((0,0,4096,0,2*sign,-2*sign))
            outputs=[m.step((0,0,4096,0,3*sign,-3*sign))[1:3] for _ in range(200)]
            for axis,max_delay in [(0,50),(1,100)]:
                first=next((i+1)*10 for i,frame in enumerate(outputs) if frame[axis]*sign>0)
                self.assertLessEqual(first,max_delay)
            self.assertGreaterEqual(sum(x*sign for x,_ in outputs),50)
            self.assertGreaterEqual(sum(y*sign for _,y in outputs),22)
            for _ in range(200):
                self.assertEqual(m.step()[1:3],(0,0))

    def test_startup_rest_window_tolerates_isolated_bad_samples(self):
        for i in range(80):
            sample=(0,0,0,0,0,0) if i%10==9 else (0,0,4096,0,2,-8)
            self.assertEqual(self.m.step(sample)[1:3],(0,0))
        self.assertEqual(self.m.u.mem_read(STATE+32,1),bytes([1]))
        moving=Machine()
        for _ in range(100):
            self.assertGreater(moving.step((0,0,4096,0,0,-48))[1],0)
        self.assertEqual(moving.u.mem_read(STATE+32,1),bytes([0]))

    def test_first_slow_turn_from_rest(self):
        for rate,limit_ms in [(1,600),(2,150),(3,130),(8,60),(16,40)]:
            for sign in (-1,1):
                m=Machine()
                m.calibrate()
                seen=[False,False]
                for _ in range(limit_ms//10):
                    frame=m.step((0,0,4096,0,rate*sign,-rate*sign))
                    for axis in range(2):
                        seen[axis] |= sign*frame[axis+1]>0
                self.assertEqual(seen,[True,True],(rate,sign))

    def test_fractional_motion(self):
        self.m.calibrate()
        result=[self.m.step((0,0,4096,0,0,-32)) for _ in range(100)]
        self.assertEqual(sum(r[1] for r in result),281)

    def test_buttons_and_recalibration(self):
        self.m.calibrate()
        r=self.m.step((0,0,4096,0,2048,-2048),0x20|0x10)
        self.assertEqual(r[:4],(3,0,0,0))
        self.assertEqual(self.m.step(buttons=0x2000|0x1000)[0],0)
        self.assertEqual(self.m.step(buttons=0x400)[0],0)
        self.assertEqual(self.m.step(buttons=0x800)[0],0)
        self.assertEqual(self.m.step(buttons=0x20)[0],1)
        self.assertEqual(self.m.step(buttons=0x10)[0],2)
        self.assertEqual(self.m.u.mem_read(STATE+32,1),b'\x01')
        self.m.step(buttons=4)
        self.assertEqual(self.m.u.mem_read(STATE+32,1),b'\x00')
        self.m.calibrate()

    def test_small_left_stick_tilt_moves_in_stick_mode(self):
        self.m.step(buttons=2)
        self.assertEqual(self.m.step(axes=0x8080808d)[1],0)
        slow=sum(self.m.step(axes=0x80808098)[1] for _ in range(10))
        fast=sum(self.m.step(axes=0x808080c0)[1] for _ in range(10))
        self.assertEqual(slow,0)  # Less than one count over the first 100 ms.
        m=Machine();m.step(buttons=2)
        self.assertGreater(sum(m.step(axes=0x80808094)[1] for _ in range(100)),0)
        self.assertEqual(sum(m.step(axes=0x80808093)[1] for _ in range(100)),0)
        self.assertGreater(fast,slow)

    def test_right_stick_scrolls_by_tilt(self):
        slow = [self.m.step(axes=0x60808080)[3] for _ in range(100)]
        fast = [self.m.step(axes=0x01808080)[3] for _ in range(100)]
        self.assertEqual(slow[0],0)
        self.assertGreater(sum(fast),sum(slow))
        self.assertEqual(sum(self.m.step(axes=0xff808080)[3] for _ in range(100)),-14)
        self.assertEqual(self.m.step(axes=0x80ff8080)[3],0)
        self.assertEqual(self.m.step(buttons=0xc000)[3],0)

    def test_fine_wheel_preserves_speed_and_stops_at_center(self):
        for axes,sign in [(0x01808080,1),(0xff808080,-1)]:
            self.m.step(axes=axes,dt=5)
            reports=[self.m.step(axes=axes,dt=5) for _ in range(200)]
            fine=[struct.unpack('b',r[4][:1])[0]*sign for r in reports]
            self.assertEqual(set(fine),{0,120})
            self.assertEqual(sum(fine),1800)
            self.assertEqual(sum(r[3] for r in reports),15*sign)
            for _ in range(10):
                r=self.m.step(dt=5)
                self.assertEqual((r[3],r[4][0]),(0,0))

    def test_auto_half_tilt_and_gyro_takeover(self):
        self.m.calibrate()
        self.assertEqual(self.m.step(axes=0x80bf)[1:3],(0,0))
        self.assertGreater(self.m.step(axes=0x80c0)[1],0)
        for gyro in ((0,0,4096,0,0,-2048), (0,0,4096,0,2048,0)):
            for _ in range(220):
                self.assertEqual(self.m.step(gyro,axes=0x80c0)[2],0)
            for _ in range(200):
                self.assertEqual(self.m.step(gyro)[1:3],(0,0))
            self.assertEqual(self.m.step(gyro)[1:3],(0,0))
            takeover=self.m.step(gyro)
            self.assertEqual(takeover[1:3],(180,0) if gyro[5] else (0,180))
            self.assertGreater(self.m.step(axes=0x80c0)[1],0)

    def test_l3_cycles_modes_and_debounces(self):
        self.m.calibrate()
        gyro=(0,0,4096,0,0,-2048)
        self.assertEqual(self.m.step(gyro,buttons=4)[1:3],(0,0))
        self.assertEqual(self.m.u.mem_read(STATE+34,1),b'\x00')
        self.m.calibrate()
        self.assertGreater(self.m.step(gyro,buttons=2,axes=0x80ff)[1],8)
        self.assertEqual(self.m.step(gyro,buttons=2)[1:3],(0,0))
        self.m.step(gyro)
        self.assertEqual(self.m.step(gyro,buttons=2)[1:3],(0,0))
        for _ in range(3): self.m.step()
        self.assertEqual(self.m.step(gyro,buttons=2,axes=0x80ff)[1:3],(180,0))
        for _ in range(3):
            self.assertEqual(self.m.step(gyro,axes=0x80ff)[1:3],(180,0))
        self.assertEqual(self.m.step(gyro,buttons=2)[1:3],(180,0))
        self.m.step()
        self.assertGreater(self.m.step(axes=0x80ff)[1],8)

    def test_stick_only_pauses_calibration(self):
        self.m.step(buttons=2,axes=0x80ff)
        for _ in range(100): self.m.step(axes=0x80ff)
        self.assertEqual(self.m.u.mem_read(STATE+32,1),b'\x00')
        self.assertEqual(self.m.step((0,0,4096,0,1024,-2048))[1:3],(0,0))
        self.m.step(buttons=2)
        self.m.calibrate()
        self.assertEqual(self.m.step((0,0,4096,0,0,-128))[1],11)

    def test_extreme_input_and_gap(self):
        self.m.calibrate()
        self.assertEqual(self.m.step((0,0,4096,0,0,-32768),dt=100)[1:3],(28800,0))
        self.assertEqual(self.m.step((0,0,4096,0,-32768,0),dt=100)[2],-28800)
        self.assertEqual(self.m.step((0,0,4096,-32768,0,-32768),dt=101)[1:3],(0,0))
        self.assertEqual(self.m.step(dt=0)[1:3],(0,0))

    def test_movement_or_missing_data_rejects_calibration(self):
        for sample in [(0,0,0,0,0,0),(0,0,4096,400,0,0)]:
            for _ in range(150): self.m.step(sample)
            self.assertEqual(self.m.u.mem_read(STATE+32,1),b'\x00')

if __name__=='__main__': unittest.main(verbosity=2)


import struct
import unittest

from test_arm import Machine, ROOT, STOP
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_PC, UC_ARM_REG_LR

def wireless_scan(enable_trace_chord=True, rest=(0,0,4096,0,0,0)):
    """Execute the released dispatch and stock enqueue code with a simulated radio reader."""
    image = (ROOT / "build/Lite2AirMouse-v106-s-mode-r33.bin").read_bytes()
    machine = Machine()
    u = machine.u
    u.mem_write(0x1018000, image)
    u.mem_map(0xc0000, 0x10000)
    u.mem_write(0x2002c, struct.pack("<I", 0x23000))
    start, end = 0x800, 0xbff
    u.mem_write(0x23000, struct.pack("<HHHH", start, end, start, start))
    u.mem_write(0x20152, bytes([2]))
    u.mem_write(0x2022b, bytes([10]))
    tick = 0
    sample = rest
    seen = []
    packets = []
    physical = 0

    def hook(uc, address, size, data):
        if address == 0x10223fe:
            req = uc.reg_read(UC_ARM_REG_R0)
            _, cmd, length, ptr = struct.unpack("<BBHI", uc.mem_read(req, 8))
            assert cmd == 0xb0
            packet = uc.mem_read(ptr, length)
            packets.append(bytes(packet))
            seen.append((packet[1], struct.unpack_from("<hh", packet, 3) if packet[1] == 1 else None))
            return
        if address == 0x101d60c:
            uc.mem_write(uc.reg_read(UC_ARM_REG_R0), struct.pack("<6h", *sample))
            uc.reg_write(UC_ARM_REG_R0, 1)
        if address == 0x1021b64:
            uc.reg_write(UC_ARM_REG_R0, physical)
        if address == 0x1020a80:
            uc.reg_write(UC_ARM_REG_R0, 0)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    for addr in (0x10223fe, 0x101d60c, 0x1021b64, 0x1020a80, 0x10221a4, 0x101cf18, 0x101ce4c, 0x1018ee8):
        u.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)

    machine.symbols['scan_gate'] = 0x101df82
    normal, suspended, sample_input = 0x101df9a, 0x101df18, 0x101e06e
    routes = []
    def exit_gate(uc, address, size, data):
        routes.append(address)
        uc.reg_write(UC_ARM_REG_PC, STOP | 1)
    for address in (normal, suspended, sample_input):
        u.hook_add(UC_HOOK_CODE, exit_gate, begin=address, end=address)

    def gate(reserve, mode=0, connected=2):
        write, = struct.unpack('<H', u.mem_read(0x23006, 2))
        read = start + (write - start + reserve) % (end - start + 1)
        u.mem_write(0x23004, struct.pack('<H', read))
        u.mem_write(0x20184, bytes([mode]))
        u.mem_write(0x20152, bytes([connected]))
        machine.call('scan_gate')
        return routes[-1]

    def step(full=False, buttons=0, gyro=(0, -1024), ticks=1, axes=0x80808080, reserve=None, imu=None):
        nonlocal tick, sample, physical
        physical=0 if buttons==9 and not enable_trace_chord else buttons
        sample = imu if imu is not None else (0,0,4096,0,*gyro)
        tick += ticks
        write, = struct.unpack("<H", u.mem_read(0x23006, 2))
        read = start + ((write + 1 - start) % (end - start + 1)) if full else write
        u.mem_write(0x23004, struct.pack("<H", read))
        u.mem_write(0x204f0, struct.pack("<I", tick))
        if reserve is not None:
            route = gate(reserve)
            assert route == (sample_input if reserve < 700 else normal)
        u.mem_write(0xc8040,bytes([tick&255]))
        machine.call("lite2_dispatch", buttons, axes, 0)
        after, = struct.unpack("<H", u.mem_read(0x23006, 2))
        return seen[-1] if after != write else (None, None)

    for _ in range(110):
        step(imu=rest)
    step.packets = packets
    step.gate = gate
    return step


class WirelessQueueTests(unittest.TestCase):
    def test_world_yaw_in_raw_and_slot_reports(self):
        from math import sqrt
        grips=[(0,0,1),(1,0,0),(0,1,0),(0,-1,0),(1/sqrt(2),0,1/sqrt(2)),
               (1/sqrt(3),1/sqrt(3),1/sqrt(3)),(0,0,-1)]
        for up in grips:
            accel=tuple(round(4096*n) for n in up)
            bias=(7,-4,9)
            step=wireless_scan(rest=accel+bias)
            for sign in (-1,1):
                imu=accel+tuple(b+round(-sign*1024*n) for b,n in zip(bias,up))
                start=len(step.packets)
                for _ in range(100):step(imu=imu)
                reports=[p for p in step.packets[start:] if p[1]==1]
                self.assertGreater(len(reports),90)
                for p in reports:
                    sx,sy=struct.unpack_from('<hh',p,8)
                    self.assertAlmostEqual(sx,sign*1024,delta=2)
                    self.assertLessEqual(abs(sy),2)
                raw=[struct.unpack_from('<hh',p,3) for p in reports]
                self.assertAlmostEqual(sum(x for x,y in raw),sign*9000,delta=190)
                self.assertLessEqual(sum(abs(y) for x,y in raw),8)

    def test_trace_keeps_pointer_and_counts_only_accepted_mouse_reports(self):
        original,candidate=wireless_scan(enable_trace_chord=False),wireless_scan()
        last=None
        for i in range(135):
            args=dict(buttons=9 if i==0 else 0x800 if i==8 else 0,
                      full=i in (15,16),axes=0x808080ff if i>=75 else 0x80808080)
            if i==40:args['reserve']=699
            if i==41:args['reserve']=1024
            self.assertEqual(original(**args),candidate(**args))
            a,b=original.packets[-1],candidate.packets[-1]
            self.assertEqual((a[:12],a[18:]),(b[:12],b[18:]))
            if b[1]!=1 or i in (15,16,40):continue
            tag,lo,hi=struct.unpack_from('<HHH',b,12)
            self.assertTrue(lo&hi&0x8000)
            self.assertEqual(((hi&511)<<9)|(lo&511),args['buttons'])
            self.assertEqual((tag>>11,tag&255,(hi>>9)&63),(10,(111+i)&255,(111+i)&63))
            seq=(lo>>9)&15
            if last is not None:self.assertEqual(seq,(last+1)&15)
            last=seq
            self.assertEqual((lo>>13)&3,2 if i==17 else 1 if i==41 else 0)

    def test_gate_boundaries_and_modes(self):
        step = wireless_scan()
        for free in (1, 23, 699, 700, 1024):
            for mode in (0, 1, 2, 3, 4):
                for connection in (0, 2):
                    expected = 0x101df9a
                    if connection == 2 and free < 700:
                        expected = 0x101e06e if mode == 0 else 0x101df18
                    self.assertEqual(step.gate(free, mode, connection), expected)

    def test_turn_and_stop_or_reverse_during_low_space(self):
        for stick in (False, True):
            for end in (0, -1):
                totals = []
                for congested in (False, True):
                    step = wireless_scan()
                    total = 0
                    for i, direction in enumerate([1,1,1,end,end]+[0]*12):
                        free = 699 if congested and i < 5 else 1024
                        inputs = (dict(axes=0x80808000 | (128 + 126 * direction))
                                  if stick else dict(gyro=(0,-400*direction)))
                        report, xy = step(reserve=free, **inputs)
                        if free < 700:
                            self.assertIsNone(report)
                        if report == 1:
                            total += xy[0]
                    totals.append(total)
                self.assertGreater(totals[0], 0)
                self.assertEqual(totals[0], totals[1])

    def test_four_blocked_reports_and_key_refresh(self):
        for buttons in (0, 0x1000, 0x2000):
            with self.subTest(buttons=buttons):
                step = wireless_scan()
                for _ in range(4):
                    self.assertEqual(step(full=True), (None, None))
                if buttons:
                    self.assertEqual(step(buttons=buttons)[0], 4 if buttons == 0x1000 else 5)
                remaining = 4 + bool(buttons)
                for _ in range(remaining):
                    self.assertEqual(step(buttons=buttons), (1, (180, 0)))
                self.assertEqual(step(buttons=buttons), (1, (90, 0)))
                self.assertTrue(all(step(buttons=buttons) == (1, (90, 0)) for _ in range(120)))

    def test_long_stall_keeps_only_eight_recent_samples(self):
        step = wireless_scan()
        for i in range(40):
            step(full=True, gyro=(0, 1024 if i < 32 else -1024))
        for _ in range(8):
            self.assertEqual(step(), (1, (180, 0)))
        self.assertEqual(step(), (1, (90, 0)))

    def test_click_drops_pending_gyro(self):
        step = wireless_scan()
        for _ in range(4):
            step(full=True)
        self.assertEqual(step(buttons=0x20), (1, (0, 0)))
        self.assertEqual(step(), (1, (90, 0)))

    def test_saturated_report_retains_unsent_remainder(self):
        step = wireless_scan()
        for z, sign in ((-32768, 1), (32767, -1)):
            step(full=True, gyro=(0, z), ticks=10)
            frames = [step(gyro=(0, z), ticks=10)[1][0] for _ in range(8)]
            self.assertEqual(frames[0], sign * 32767)
            baseline = wireless_scan()
            expected = sum(baseline(gyro=(0, z), ticks=10)[1][0] for _ in range(9))
            self.assertLessEqual(abs(sum(frames) - expected), 1)

    def test_stick_and_key_interleaving_conserve_both_axes(self):
        def scan_trace(blocked):
            step = wireless_scan()
            total = [0, 0]
            for i in range(120):
                report, xy = step(full=blocked and 10 <= i < 14,
                                  buttons=0x1000 if 14 <= i < 30 else 0,
                                  axes=0x8080ffff)
                if report == 1:
                    for n in (0, 1):
                        total[n] += xy[n]
            return total
        reference = scan_trace(False)
        self.assertTrue(all(value > 0 for value in reference))
        self.assertEqual(scan_trace(True), reference)

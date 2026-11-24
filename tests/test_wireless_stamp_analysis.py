import importlib.util
from pathlib import Path
import tempfile
import unittest


path = Path(__file__).resolve().parents[1] / 'tools' / 'analyze_wireless_stamps.py'
spec = importlib.util.spec_from_file_location('wireless_stamps', path)
stamps = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stamps)


def frame(i, ms=None, phase=0, occupancy=0, flags=0, motion=False):
    return ((10*i if ms is None else ms)/1000, i%16, i%64, 10,
            (phase << 6) | occupancy, flags, 5, motion)


class StampAnalysisTests(unittest.TestCase):
    def test_truncated_and_known_queue_codes_at_each_end_of_long_gap(self):
        reports = [frame(0,0,occupancy=0),frame(1,10,occupancy=3),
                   frame(2,60,occupancy=63,flags=2),frame(3,100,occupancy=62,flags=1),
                   frame(7,180),frame(8,390)]
        result = stamps.analyze_r50(reports)
        self.assertFalse(result['acknowledged_comparison']['sufficient_coverage_and_comparability'])
        m = result['phases']['before_request']
        self.assertEqual(m['long_host_gaps'],4)
        self.assertEqual(m['long_gaps_with_comparable_enqueue'],2)
        self.assertEqual(m['repeated_gap_clusters'],1)
        self.assertEqual(m['queue_on_long_gap_start_report']['four_byte_units']['max'],3)
        self.assertEqual(m['queue_on_long_gap_end_report']['four_byte_units']['max'],62)
        self.assertEqual(m['queue_on_long_gap_end_report']['overflow_or_unknown'],1)
        self.assertEqual(m['long_host_gaps_enqueue_at_most_20ms'],2)
        self.assertEqual(m['long_host_gaps_pressure'],1)
        self.assertEqual(m['long_host_gaps_rejection'],1)
        self.assertEqual(m['covered_s'],.39)
        self.assertEqual(m['long_gaps_per_minute'],round(4*60/.39,3))

    def test_two_second_quarantine_on_both_sides_and_comparable_phases(self):
        reports = [frame(i,phase=0 if i<1500 else 1) for i in range(3000)]
        result = stamps.analyze_r50(reports)
        a,b=[result['phases'][k] for k in ('before_request','after_ack')]
        self.assertLess(a['segments'][-1]['end_s'],12.99)
        self.assertGreater(b['segments'][0]['start_s'],17)
        self.assertTrue(result['acknowledged_comparison']['sufficient_coverage_and_comparability'])
        self.assertFalse(result['acknowledged_comparison']['baseline_repeated_clusters_present'])
        self.assertEqual(a['long_host_gaps'],0)
        self.assertGreaterEqual(result['excluded_reports']['transition_margin'],401)

    def test_failed_ack_and_unmarked_intervals_do_not_masquerade_as_treatment(self):
        reports = [frame(i,phase=0 if i<1500 else 2) for i in range(3000)]
        reports[100:600] = [None]*500
        result = stamps.analyze_r50(reports)
        self.assertFalse(result['acknowledged_comparison']['sufficient_coverage_and_comparability'])
        self.assertEqual(result['phases']['after_ack']['marked_reports'],0)
        self.assertLess(result['phases']['before_request']['coverage_fraction'],.8)
        self.assertEqual(len(result['phases']['before_request']['segments']),2)
        self.assertGreater(result['phases']['after_failed_request']['observed_arrival_pairs'],1000)

    def test_movement_imbalance_is_reported_and_reverse_clock_rejected(self):
        rows = [frame(i, phase=int(i>=1500),motion=i>=1500) for i in range(3000)]
        reasons = stamps.analyze_r50(rows)['acknowledged_comparison']['reasons']
        self.assertTrue(any('Moving-frame' in reason for reason in reasons))
        with self.assertRaisesRegex(ValueError,'Out-of-order'):
            stamps.analyze_r50(rows[::-1])

    def test_report_decoding_preserves_legacy_clock_and_signals_optional_motion(self):
        lines = []
        for i, stamp in enumerate([61, 65, 127]):
            for code, value in [(0,65535 if i else 0),(2,0x8000 | (i<<9)),
                                (7,0x8000 | (i<<9)),(5,(10<<11) | (5<<8) | stamp)]:
                lines.append(f'[{i*.01:.5f}] 0002 {code:04x} {value:08x}')
            lines.append(f'[{i*.01:.5f}] 0000 0000 00000000')
        with tempfile.TemporaryDirectory() as folder:
            trace = Path(folder) / 'input.txt'
            trace.write_text('\n'.join(lines),encoding='utf-16')
            standard = stamps.read_reports(trace)
            extended = stamps.read_reports(trace,include_motion=True)
        self.assertEqual([r[:7] for r in extended],standard)
        self.assertEqual([r[7] for r in extended],[False,True,True])
        self.assertEqual(stamps.analyze(standard)['secondary_clock_distinct'],3)
        self.assertEqual(stamps.analyze_r50(extended)['phase_markers'],{'before_request':1,'after_ack':2})


if __name__ == '__main__':
    unittest.main()

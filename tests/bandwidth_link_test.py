"""Verify the bottleneck model independently of process and network timing."""
import pathlib
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'benchmarks'))
from bandwidth import BandwidthLink, memory_summary, verify_cycles, rss, cpu_seconds


class LinkTest(unittest.TestCase):
    def test_serialization_and_tail_drop(self):
        link = BandwidthLink([(10, 800)], 120)
        packet = bytes(72)  # Exactly 100 bytes with IPv4 and UDP headers.
        self.assertTrue(link.enqueue(packet))
        self.assertEqual(link.advance(0.4), [])
        self.assertAlmostEqual(link.queued, 60)
        self.assertFalse(link.enqueue(packet))
        self.assertEqual(link.dropped, 1)
        self.assertEqual(link.advance(1), [packet])
        self.assertEqual(link.delivered_bytes, 100)
        self.assertEqual(link.queued, 0)

    def test_no_idle_credit(self):
        link = BandwidthLink([(20, 800)], 1000)
        self.assertEqual(link.advance(10), [])
        link.enqueue(bytes(72))
        self.assertEqual(link.advance(10.5), [])
        self.assertEqual(link.advance(11), [bytes(72)])

    def test_capacity_change_mid_packet(self):
        link = BandwidthLink([(1, 800), (2, 1600)], 1000)
        link.enqueue(bytes(172))
        self.assertEqual(link.advance(0.5), [])
        self.assertEqual(link.advance(1.25), [])
        self.assertEqual(link.advance(1.5), [bytes(172)])
        self.assertEqual(link.delivered_bytes, 200)

    def test_fifo_across_multiple_phases(self):
        link = BandwidthLink([(1, 800), (2, 400), (3, 800)], 1000)
        packets = [bytes([n]) * 72 for n in range(3)]
        for packet in packets:
            link.enqueue(packet)
        self.assertEqual(link.advance(2), packets[:1])
        self.assertEqual(link.advance(2.5), packets[1:2])
        self.assertEqual(link.advance(3.5), packets[2:])
        self.assertEqual(link.maximum_queue, 300)
        with self.assertRaises(RuntimeError):
            link.advance(3)


class SettlingTest(unittest.TestCase):
    def samples(self):
        rows = [dict(seconds=n, target_kbps=660, delivered_kbps=700, drops=0, decoded=n * 30)
                for n in range(1, 17)]
        for second, rate in [(7, 817), (8, 881), (9, 660), (13, 800), (14, 864), (15, 928)]:
            rows[second - 1]['target_kbps'] = rate
        return rows

    def test_transient_probe_peak_is_not_sustained_overshoot(self):
        verify_cycles(self.samples(), 1)

    def test_sustained_overshoot_fails(self):
        rows = self.samples()
        for row in rows[6:9]:
            row['target_kbps'] = 1000
        with self.assertRaisesRegex(RuntimeError, 'target did not settle'):
            verify_cycles(rows, 1)

    def test_wire_budget_and_loss_still_fail(self):
        rows = self.samples()
        rows[7]['delivered_kbps'] = 1000
        with self.assertRaisesRegex(RuntimeError, 'capacity budget'):
            verify_cycles(rows, 1)
        rows = self.samples()
        rows[7]['drops'] = 4
        with self.assertRaisesRegex(RuntimeError, 'continued drops'):
            verify_cycles(rows, 1)


class MemoryTest(unittest.TestCase):
    def test_exit_during_proc_read(self):
        with patch('pathlib.Path.read_text', side_effect=ProcessLookupError):
            self.assertIsNone(rss(123))
            self.assertIsNone(cpu_seconds(123))

    def test_warmup_and_exited_processes(self):
        samples = [dict(seconds=1, host_rss_kib=9999, client_rss_kib=9999)]
        samples += [dict(seconds=n, host_rss_kib=100 + n, client_rss_kib=200)
                    for n in range(5, 11)]
        samples.append(dict(seconds=11, host_rss_kib=None, client_rss_kib=200))
        result = memory_summary(samples)
        self.assertEqual(result['host']['samples'], 6)
        self.assertEqual(result['host']['growth_kib'], 5)
        self.assertEqual(result['host']['max_kib'], 110)
        self.assertEqual(result['client']['growth_kib'], 0)
        self.assertEqual(result['client']['samples'], 7)

    def test_missing_samples_fail(self):
        with self.assertRaises(RuntimeError):
            memory_summary([dict(seconds=6, host_rss_kib=None, client_rss_kib=100)])


if __name__ == '__main__':
    unittest.main()

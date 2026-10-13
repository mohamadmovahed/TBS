"""Tests for the tbsdvb Python module (run by ctest with PYTHONPATH set)."""
import os
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

import tbsdvb


def ts_packet(pid, cc, payload=b"", pusi=False):
    body = payload + b"\xff" * (184 - len(payload))
    return bytes([0x47, (0x40 if pusi else 0) | ((pid >> 8) & 0x1F), pid & 0xFF, 0x10 | (cc & 15)]) + body


def section(table_id, ext, body):
    length = 5 + len(body) + 4
    s = bytes([table_id, 0xB0 | (length >> 8), length & 0xFF, ext >> 8, ext & 0xFF, 0xC1, 0, 0]) + body
    return s + struct.pack(">I", tbsdvb.crc32_mpeg(s))


class Module(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.capture = os.path.join(cls.tmp, "gse.ts")
        subprocess.check_call([os.environ["TBS_MAKE_TEST_STREAM"], "--gse", "--out", cls.capture],
                              stdout=subprocess.DEVNULL)

    def test_version_and_enums(self):
        self.assertTrue(tbsdvb.__version__)
        self.assertEqual(tbsdvb.AlarmType.no_lock.name, "no_lock")
        cfg = tbsdvb.ReceiverConfig()
        cfg.tuning.frequency_mhz = 11494
        cfg.tuning.polarization = tbsdvb.Polarization.Horizontal
        self.assertIn("11494", cfg.tuning.describe())
        self.assertEqual(cfg.tuning.intermediate_frequency_khz(), 1744000)

    def test_network_url(self):
        o = tbsdvb.parse_network_url("udp://239.1.1.1:1234?program=101&pids=0x12")
        self.assertEqual((o.host, o.port, o.program_number, o.pids), ("239.1.1.1", 1234, 101, [0x12]))
        self.assertEqual(tbsdvb.TsNetworkOutput("srt://:9000?mode=listener").srt_mode, tbsdvb.SrtMode.Listener)
        with self.assertRaises(ValueError):
            tbsdvb.parse_network_url("http://x:1")

    def test_analyze_capture(self):
        fmt, snap, xmltv = tbsdvb.analyze_capture(self.capture)
        self.assertIn("pseudo-TS", fmt)
        st = snap["streams"][0]
        self.assertEqual(st["content"], "GSE (IP)")
        self.assertGreater(st["gse"]["pdus"], 0)
        self.assertGreater(st["ip"]["ipv4"], 0)
        self.assertTrue(xmltv.startswith("<?xml"))

    def test_read_capture_and_processor(self):
        frames = []
        fmt = tbsdvb.read_capture(self.capture, on_bbframe=lambda f, h: frames.append((f, h)))
        self.assertGreater(len(frames), 10)
        frame, header = frames[0]
        self.assertEqual(header.stream_type, "GSE-HEM" if header.mode == "HEM" else header.stream_type)
        self.assertEqual(tbsdvb.BbHeader.parse(frame[:10]).dfl, header.dfl)
        with self.assertRaises(ValueError):
            tbsdvb.BbHeader.parse(b"\0" * 9 + b"\x02")  # CRC-8 0 (NM) or 1 (HEM) would be valid
        proc = tbsdvb.Processor()
        proc.open()
        for f, h in frames:
            proc.push_bbframe(f, h)
        snap = proc.snapshot()
        self.assertGreater(snap["streams"][0]["gse"]["pdus"], 0)
        self.assertIn("GSE", proc.report())

    def test_ts_filter(self):
        pat = section(0x00, 1, struct.pack(">HH", 7, 0xE100))
        pmt = section(0x02, 7, struct.pack(">HH", 0xE101, 0xF000) + bytes([0x1B]) + struct.pack(">HH", 0xE101, 0xF000))
        packets = ts_packet(0, 0, b"\x00" + pat, True) + ts_packet(0x100, 0, b"\x00" + pmt, True)
        packets += b"".join(ts_packet(0x101, i) + ts_packet(0x200, i) + ts_packet(0x1FFF, i) for i in range(10))
        f = tbsdvb.TsFilter(program=7)
        out = f.push(packets)
        pids = {((out[i + 1] & 0x1F) << 8) | out[i + 2] for i in range(0, len(out), 188)}
        self.assertEqual(pids, {0, 0x100, 0x101})
        self.assertTrue(f.program_found)
        self.assertEqual(f.service_pids, [0x100, 0x101])
        self.assertEqual(len(tbsdvb.TsFilter().push(packets)), len(packets) - 10 * 188)  # nulls dropped

    def test_receiver_replay(self):
        cfg = tbsdvb.ReceiverConfig()
        cfg.device.kind = tbsdvb.DeviceKind.FileReplay
        cfg.device.replay_file = self.capture
        cfg.device.replay_loop = False
        cfg.device.replay_rate_mbps = 40
        cfg.capture = tbsdvb.CaptureMode.BBFrame
        rx = tbsdvb.Receiver()
        proc = tbsdvb.Processor()
        proc.open()
        mon = tbsdvb.Monitor()
        rx.attach_processor(proc)
        rx.attach_monitor(mon)
        frames, states, logs = [], [], []
        stopped = threading.Event()
        rx.on_bbframe = lambda f, h: frames.append(len(f))
        rx.on_log = logs.append

        def on_state(state, error):
            states.append(state)
            if state == tbsdvb.ReceiverState.Stopped:
                stopped.set()
        rx.on_state = on_state
        rx.set_status_interval(100)
        self.assertTrue(rx.start(cfg))
        self.assertTrue(stopped.wait(20))
        rx.stop()
        self.assertEqual(rx.last_error(), "")
        self.assertIn(tbsdvb.ReceiverState.Running, states)
        self.assertGreater(len(frames), 10)
        self.assertGreater(rx.stats().bb_frames, 0)
        self.assertTrue(any("Replaying" in l for l in logs))
        snap = proc.snapshot()
        self.assertEqual(snap["streams"][0]["content"], "GSE (IP)")   # fed in C++
        self.assertGreater(len(mon.history()), 0)
        self.assertEqual(len(mon.alarms()), 9)

    def test_callback_exception_does_not_stop_capture(self):
        cfg = tbsdvb.ReceiverConfig()
        cfg.device.kind = tbsdvb.DeviceKind.FileReplay
        cfg.device.replay_file = self.capture
        cfg.device.replay_loop = False
        cfg.capture = tbsdvb.CaptureMode.BBFrame
        rx = tbsdvb.Receiver()
        count = [0]

        def bad(frame, header):
            count[0] += 1
            raise RuntimeError("boom")
        rx.on_bbframe = bad
        old = sys.unraisablehook
        sys.unraisablehook = lambda u: None
        try:
            rx.start(cfg)
            for _ in range(200):
                if not rx.is_running() and count[0]:
                    break
                time.sleep(0.05)
        finally:
            rx.stop()
            sys.unraisablehook = old
        self.assertGreater(count[0], 10)

    def test_monitor_alarm_callback(self):
        mon = tbsdvb.Monitor()
        events = []
        mon.set_alarm_callback(events.append)
        rx = tbsdvb.Receiver()
        # A stopped receiver's status / stats are valid defaults
        for i in range(5):
            mon.update(tbsdvb.ReceiverState.Running, rx.status(), rx.stats(), 1000000 + i * 1000)
        self.assertTrue(any(e.type == tbsdvb.AlarmType.no_lock and e.raised for e in events))
        path = os.path.join(self.tmp, "h.csv")
        mon.write_csv(path)
        with open(path) as f:
            self.assertTrue(f.readline().startswith("time,unix_ms"))

    def test_scan_needs_neumo_driver(self):
        dev = tbsdvb.DeviceSelection()
        dev.kind = tbsdvb.DeviceKind.FileReplay
        caps = tbsdvb.scan_capabilities(dev)
        self.assertFalse(caps["probed"])
        self.assertTrue(caps["note"])
        with self.assertRaises(RuntimeError):
            tbsdvb.spectrum(dev, 10700, 12750)
        with self.assertRaises(RuntimeError):
            tbsdvb.blind_scan(dev, 10700, 12750)

    def test_network_scan_and_constellation(self):
        capture = os.path.join(self.tmp, "si.ts")
        subprocess.check_call([os.environ["TBS_MAKE_TEST_STREAM"], "--add-si", "--out", capture],
                              stdout=subprocess.DEVNULL)
        cfg = tbsdvb.ReceiverConfig()
        cfg.device.kind = tbsdvb.DeviceKind.FileReplay
        cfg.device.replay_file = capture
        cfg.device.replay_rate_mbps = 10
        cfg.device.replay_loop = True
        cfg.capture = tbsdvb.CaptureMode.BBFrame
        cfg.tuning.frequency_mhz = 11494
        rx = tbsdvb.Receiver()
        proc = tbsdvb.Processor()
        rx.attach_processor(proc)
        rx.set_status_interval(100)
        rx.set_constellation_samples(128)
        self.assertTrue(rx.start(cfg))
        try:
            progress = []
            r = tbsdvb.network_scan(rx, proc, [cfg.tuning], table_timeout_ms=1000,
                                    progress=lambda i, n, msg: progress.append((i, n, msg)))
            # The NIT lists this transport and three more at 13.0E
            self.assertEqual(len(r["transponders"]), 4)
            self.assertTrue(all(t["locked"] for t in r["transponders"]))
            freqs = sorted(round(t["tuning"].frequency_mhz) for t in r["transponders"])
            self.assertEqual(freqs, [11053, 11494, 11817, 12187])
            self.assertEqual(r["network_name"], "TBS Demo Network")
            self.assertIn("4 transponders", r["text"])
            self.assertTrue(progress)
            # NIT transports carry the parsed satellite delivery descriptor
            nets = [s for s in proc.snapshot()["streams"] if s["ts"]["networks"]]
            sat = nets[0]["ts"]["networks"][0]["transports"][0]["satellite"]
            self.assertEqual(sat["polarization"], "H")
            self.assertAlmostEqual(sat["orbital_position"], 13.0)
            # File replay has no IQ samples
            for _ in range(50):
                if rx.constellation() is None:
                    break
                time.sleep(0.05)
            self.assertIsNone(rx.constellation())
        finally:
            rx.stop()


if __name__ == "__main__":
    unittest.main(verbosity=1)

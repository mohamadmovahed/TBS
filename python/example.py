#!/usr/bin/env python3
"""Capture with a TBS 6903x (or replay a recording), print the status once a second,
then list the services and save the EPG.

    python3 example.py --replay capture.ts          # recording (TS, pseudo-TS or BBFrames)
    python3 example.py --adapter 0 --freq 11494 --sr 27500 --pol h --seconds 30
"""
import argparse
import time

import tbsdvb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--replay", help="recording to replay instead of a card")
    ap.add_argument("--adapter", type=int, default=0)
    ap.add_argument("--freq", type=float, default=11494)
    ap.add_argument("--sr", type=float, default=27500)
    ap.add_argument("--pol", choices=["h", "v"], default="h")
    ap.add_argument("--bbframe", action="store_true", help="capture BBFrames (stid135 bbframe=1)")
    ap.add_argument("--seconds", type=float, default=10)
    ap.add_argument("--xmltv", default="epg.xml")
    a = ap.parse_args()

    cfg = tbsdvb.ReceiverConfig()
    if a.replay:
        cfg.device.kind = tbsdvb.DeviceKind.FileReplay
        cfg.device.replay_file = a.replay
        cfg.device.replay_rate_mbps = 20
    else:
        cfg.device.kind = tbsdvb.DeviceKind.LinuxDvb
        cfg.device.adapter = a.adapter
    cfg.tuning.frequency_mhz = a.freq
    cfg.tuning.symbol_rate_ksps = a.sr
    cfg.tuning.polarization = tbsdvb.Polarization.Horizontal if a.pol == "h" else tbsdvb.Polarization.Vertical
    cfg.capture = tbsdvb.CaptureMode.BBFrame if a.bbframe else tbsdvb.CaptureMode.TransportStream

    rx = tbsdvb.Receiver()
    proc = tbsdvb.Processor()
    proc.open()
    rx.attach_processor(proc)          # tables, EPG and IP analysis run in C++
    mon = tbsdvb.Monitor()
    mon.set_alarm_callback(lambda e: print("ALARM" if e.raised else "cleared", e.type.name, e.message))
    rx.attach_monitor(mon)
    rx.on_log = lambda m: print("log:", m)
    if not rx.start(cfg):
        raise SystemExit(rx.last_error())

    end = time.time() + a.seconds
    while time.time() < end and rx.is_running():
        time.sleep(1)
        st, s = rx.status(), rx.stats()
        print(f"{'LOCK' if st.locked else '----'}  C/N {st.cnr_db}  {s.input_mbps:.2f} Mbit/s  "
              f"TS {s.ts_packets}  BBFrames {s.bb_frames}  CC errors {s.cc_errors}")
    rx.stop()

    for stream in proc.snapshot()["streams"]:
        print(f"ISI {stream['isi']}: {stream['content']}")
        for svc in stream["ts"]["services"]:
            now = svc["now"]["name"] if svc["now"] else ""
            print(f"  {svc['service_id']:5}  {svc['name']:<30} {now}")
    with open(a.xmltv, "w", encoding="utf-8") as f:
        f.write(proc.xmltv())
    print("EPG written to", a.xmltv)


if __name__ == "__main__":
    main()

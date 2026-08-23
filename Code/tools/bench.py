#!/usr/bin/env python3
"""
bench.py — talk to the Bench_Tune sketch over Serial.

    python bench.py COM5 status get               # one or more commands
    python bench.py COM5 -f session.txt           # commands from a file, one per line
    python bench.py COM5 -i                       # interactive
    python bench.py COM5 --log run1.log rep 50 U U'

Every line the Teensy sends is echoed to stdout (and to --log if given). A
command is complete when the sketch answers `ok` or `err <n> <text>`; an
`err` stops a file/argument run unless --keep-going is set.

`mv` records are also parsed into a CSV (--csv) with one row per move and
per-motor columns, for pandas or a spreadsheet:
    tok,code,aligned,homed,ms,U_before,U_after,U_res,U_idx,U_err,R_before,...

The protocol is documented at the top of Code/Test Code/Bench_Tune/Bench_Tune.ino.
"""
import argparse
import csv
import sys
import time

import serial  # pyserial

MOTORS = ["U", "R", "F", "D", "L", "B"]
BAUD = 115200


class Bench:
    def __init__(self, port, log=None, csvfile=None, timeout=120.0):
        self.ser = serial.Serial(port, BAUD, timeout=0.1)
        self.log = open(log, "a", encoding="utf-8") if log else None
        self.csv = None
        if csvfile:
            new = True
            try:
                new = open(csvfile, "r", encoding="utf-8").read(1) == ""
            except OSError:
                pass
            self.csv = csv.writer(open(csvfile, "a", newline="", encoding="utf-8"))
            if new:
                cols = ["time", "tok", "code", "aligned", "homed", "ms"]
                for m in MOTORS:
                    cols += [f"{m}_before", f"{m}_after", f"{m}_res", f"{m}_idx", f"{m}_err"]
                self.csv.writerow(cols)
        self.timeout = timeout

    def _emit(self, line):
        print(line, flush=True)
        if self.log:
            self.log.write(line + "\n")
            self.log.flush()
        if self.csv and line.startswith("mv "):
            self._csv_row(line)

    def _csv_row(self, line):
        parts = line.split()
        row = {"time": f"{time.time():.3f}", "tok": parts[1]}
        for p in parts[2:]:
            k, _, v = p.partition("=")
            if k in MOTORS:
                f = v.split(",")
                for name, val in zip(["before", "after", "res", "idx", "err"], f):
                    row[f"{k}_{name}"] = val
            else:
                row[k] = v
        cols = ["time", "tok", "code", "aligned", "homed", "ms"]
        for m in MOTORS:
            cols += [f"{m}_before", f"{m}_after", f"{m}_res", f"{m}_idx", f"{m}_err"]
        self.csv.writerow([row.get(c, "") for c in cols])

    def wait_ready(self, seconds=8.0):
        """Drain the boot banner until `ready` or the time is up."""
        end = time.time() + seconds
        while time.time() < end:
            raw = self.ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").rstrip()
            self._emit(line)
            if line == "ready":
                return True
        return False

    def cmd(self, text):
        """Send one command; return (ok, lines) once the terminator arrives."""
        self.ser.write((text.strip() + "\n").encode("utf-8"))
        self._emit("> " + text.strip())
        lines = []
        end = time.time() + self.timeout
        while time.time() < end:
            raw = self.ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").rstrip()
            self._emit(line)
            lines.append(line)
            if line == "ok":
                return True, lines
            if line.startswith("err "):
                return False, lines
        self._emit("# timeout waiting for terminator")
        return False, lines

    def stop(self):
        # Any byte stops a running rep at the next move boundary; then the
        # explicit stop drops torque.
        self.ser.write(b"\n")
        time.sleep(0.5)
        return self.cmd("stop")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("commands", nargs="*", help="commands to send, each as one argument (quote them)")
    ap.add_argument("-f", "--file", help="file of commands, one per line; # comments allowed")
    ap.add_argument("-i", "--interactive", action="store_true")
    ap.add_argument("--log", help="append every line to this file")
    ap.add_argument("--csv", help="append parsed mv records to this CSV")
    ap.add_argument("--keep-going", action="store_true", help="do not stop at the first err")
    ap.add_argument("--no-wait", action="store_true", help="do not wait for the boot banner")
    # intermixed: options may follow the command list (`COM5 --log x status get`)
    args = ap.parse_intermixed_args()

    b = Bench(args.port, log=args.log, csvfile=args.csv)
    if not args.no_wait:
        b.wait_ready()

    cmds = list(args.commands)
    if args.file:
        for line in open(args.file, encoding="utf-8"):
            line = line.split("#", 1)[0].strip()
            if line:
                cmds.append(line)

    rc = 0
    try:
        for c in cmds:
            ok, _ = b.cmd(c)
            if not ok and not args.keep_going:
                rc = 1
                break
        if args.interactive:
            print("# interactive — type commands; Ctrl-C stops a run and drops torque", flush=True)
            while True:
                try:
                    c = input()
                except EOFError:
                    break
                if c.strip():
                    b.cmd(c)
    except KeyboardInterrupt:
        b.stop()
        rc = 130
    sys.exit(rc)


if __name__ == "__main__":
    main()

"""Read-only ATGM336H NMEA positioning test; requires pyserial."""

import argparse
from collections import Counter
from datetime import datetime
import json
import math
from pathlib import Path
import re
import sys
import time

import serial
from serial.tools import list_ports


def fields_from_line(line):
    """Reject incomplete, non-ASCII and checksum-invalid sentences."""
    match = re.fullmatch(r"\$([A-Z]{5},[ -~]*)\*([0-9A-Fa-f]{2})", line.strip())
    if not match:
        raise ValueError("invalid NMEA framing")
    body, checksum = match.groups()
    actual = 0
    for char in body:
        actual ^= ord(char)
    if actual != int(checksum, 16):
        raise ValueError("checksum mismatch")
    return body.split(",")


def coordinate(raw, hemisphere, latitude):
    width, limit, allowed = (2, 90, "NS") if latitude else (3, 180, "EW")
    if hemisphere not in tuple(allowed) or not re.fullmatch(
        rf"\d{{{width + 2}}}(?:\.\d+)?", raw
    ):
        raise ValueError("missing/invalid coordinate")
    degrees, minutes = int(raw[:width]), float(raw[width:])
    if minutes >= 60 or degrees > limit or (degrees == limit and minutes != 0):
        raise ValueError("coordinate out of range")
    value = degrees + minutes / 60
    return -value if hemisphere in "SW" else value


def utc_seconds(raw):
    if not re.fullmatch(r"\d{6}(?:\.\d+)?", raw):
        raise ValueError("missing/invalid UTC")
    hours, minutes, seconds = int(raw[:2]), int(raw[2:4]), float(raw[4:])
    if hours >= 24 or minutes >= 60 or seconds >= 60:
        raise ValueError("UTC out of range")
    return hours * 3600 + minutes * 60 + seconds


def optional_float(raw):
    value = float(raw) if raw else None
    if value is not None and not math.isfinite(value):
        raise ValueError("non-finite number")
    return value


def parse_sentence(fields):
    kind = fields[0][-3:]
    data = {"sentence": fields[0], "kind": kind}
    if kind == "GGA":
        data.update(utc=fields[1], quality=int(fields[6] or 0),
                    satellites=int(fields[7] or 0), hdop=optional_float(fields[8]),
                    altitude_m=optional_float(fields[9]))
        data["valid"] = data["quality"] in (1, 2, 3, 4, 5) and data["satellites"] > 0
        coords = fields[2:6]
    elif kind == "RMC":
        mode = fields[12] if len(fields) > 12 else ""
        nav_status = fields[13] if len(fields) > 13 else ""
        data.update(utc=fields[1], status=fields[2], date=fields[9], mode=mode,
                    navigation_status=nav_status)
        data["valid"] = fields[2] == "A" and mode in ("", "A", "D", "P", "R", "F")
        coords = fields[3:7]
    elif kind == "GSA":
        data.update(fix_type=int(fields[2] or 1), pdop=optional_float(fields[15]),
                    hdop=optional_float(fields[16]), vdop=optional_float(fields[17]))
    elif kind == "GSV":
        data.update(visible=int(fields[3] or 0), message=int(fields[2]),
                    messages=int(fields[1]))
        data["snr"] = [float(fields[i]) for i in range(7, len(fields), 4) if fields[i]]
    elif kind == "TXT":
        data["text"] = ",".join(fields[4:])
    if kind in ("GGA", "RMC") and data["valid"]:
        data["seconds"] = utc_seconds(data["utc"])
        data["latitude"] = coordinate(coords[0], coords[1], True)
        data["longitude"] = coordinate(coords[2], coords[3], False)
    return data


class Monitor:
    def __init__(self, required=3):
        self.required = required
        self.counts = Counter()
        self.bad_lines = 0
        self.parse_errors = 0
        self.latest = {}
        self.visible = {}
        self.max_visible = {}
        self.max_snr = 0
        self.max_used = 0
        self.pending = {}
        self.last_epoch = None
        self.last_pair_elapsed = None
        self.streak = 0
        self.max_streak = 0
        self.paired_fixes = 0
        self.first_fix = None
        self.success_elapsed = None
        self.last_fix = None

    def feed(self, line, elapsed):
        try:
            fields = fields_from_line(line)
        except ValueError:
            self.bad_lines += 1
            return
        self.counts[fields[0]] += 1
        try:
            data = parse_sentence(fields)
        except (ValueError, IndexError):
            self.parse_errors += 1
            if fields[0][-3:] in ("GGA", "RMC"):
                self.streak = 0
                self.pending.clear()
            return
        kind = data["kind"]
        self.latest[kind] = dict(data, received_elapsed_s=round(elapsed, 3))
        if kind == "GSV":
            talker = fields[0][:2]
            self.visible[talker] = data["visible"]
            self.max_visible[talker] = max(self.max_visible.get(talker, 0), data["visible"])
            self.max_snr = max([self.max_snr] + data["snr"])
        if kind == "GGA":
            self.max_used = max(self.max_used, data["satellites"])
        if kind not in ("GGA", "RMC"):
            return
        if not data["valid"]:
            self.streak = 0
            self.pending.clear()
            return
        # Match the UTC epoch and require both messages to have just arrived.
        self.pending = {key: value for key, value in self.pending.items()
                        if elapsed - value[0] <= 2.5}
        epoch = data["seconds"]
        _, pair = self.pending.setdefault(epoch, (elapsed, {}))
        pair[kind] = data
        if "GGA" not in pair or "RMC" not in pair or epoch == self.last_epoch:
            return
        gga, rmc = pair["GGA"], pair["RMC"]
        if any(abs(gga[key] - rmc[key]) > 0.0001 for key in ("latitude", "longitude")):
            self.streak = 0
            return
        continuous = (self.last_epoch is not None
                      and 0 < (epoch - self.last_epoch) % 86400 <= 2.5
                      and elapsed - self.last_pair_elapsed <= 2.5)
        self.streak = self.streak + 1 if continuous else 1
        self.max_streak = max(self.max_streak, self.streak)
        self.last_epoch, self.last_pair_elapsed = epoch, elapsed
        self.paired_fixes += 1
        self.last_fix = {"elapsed_s": round(elapsed, 3), "gga": gga, "rmc": rmc}
        if self.first_fix is None:
            self.first_fix = self.last_fix
        if self.streak >= self.required and self.success_elapsed is None:
            self.success_elapsed = round(elapsed, 3)
            print(f"POSITION FIX CONFIRMED: {gga['latitude']:.7f}, {gga['longitude']:.7f}; "
                  f"satellites={gga['satellites']}; HDOP={gga['hdop']}", flush=True)

    def status(self, elapsed):
        gga, rmc, gsa = (self.latest.get(kind, {}) for kind in ("GGA", "RMC", "GSA"))
        print(f"[{elapsed:6.1f}s] GGA quality={gga.get('quality', '?')} "
              f"used={gga.get('satellites', '?')} HDOP={gga.get('hdop', '?')} "
              f"RMC={rmc.get('status', '?')} GSA={gsa.get('fix_type', '?')} "
              f"visible={self.visible} max_SNR={self.max_snr} "
              f"valid_NMEA={sum(self.counts.values())} paired_fixes={self.paired_fixes}", flush=True)

    def summary(self):
        return {"positioning_success": self.success_elapsed is not None,
                "required_consecutive_fixes": self.required,
                "success_elapsed_s": self.success_elapsed,
                "paired_fixes": self.paired_fixes, "max_consecutive_fixes": self.max_streak,
                "first_fix": self.first_fix, "last_fix": self.last_fix,
                "max_satellites_used": self.max_used,
                "max_visible_by_talker": self.max_visible, "max_snr_reported": self.max_snr,
                "sentence_counts": dict(self.counts), "invalid_nmea_lines": self.bad_lines,
                "parse_errors": self.parse_errors, "latest": self.latest}


def positive_float(raw):
    value = float(raw)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError("must be a positive finite number")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="COM port; auto-select only if exactly one port exists")
    parser.add_argument("--baudrate", type=int, default=9600)
    parser.add_argument("--duration", type=positive_float, default=300)
    parser.add_argument("--stable-fixes", type=int, default=3)
    parser.add_argument("--list-ports", action="store_true")
    parser.add_argument("--log-dir", type=Path, default=Path(__file__).resolve().parent / "logs")
    parser.add_argument("--environment", default="unspecified", help="record antenna/test conditions")
    args = parser.parse_args()
    if args.stable_fixes < 1 or args.baudrate < 1:
        parser.error("--stable-fixes and --baudrate must be positive")
    ports = list(list_ports.comports())
    if args.list_ports:
        for port in ports:
            print(f"{port.device}: {port.description} [{port.hwid}]")
        return 0
    if not args.port:
        if len(ports) != 1:
            print(f"Found {len(ports)} serial ports; specify --port (see --list-ports).")
            return 2
        args.port = ports[0].device
    info = next((port for port in ports if port.device.upper() == args.port.upper()), None)
    started = datetime.now().astimezone()
    output = args.log_dir / started.strftime("%Y%m%d_%H%M%S_%f")
    output.mkdir(parents=True, exist_ok=False)
    monitor = Monitor(args.stable_fixes)
    error, interrupted, total_bytes = None, False, 0
    start = time.monotonic()
    # Set control lines before opening; never send configuration or reset commands.
    connection = serial.Serial(port=None, baudrate=args.baudrate, timeout=0.25)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    print(f"Reading {args.port} @ {args.baudrate} 8N1 for {args.duration:g}s; logs: {output}", flush=True)
    try:
        with (output / "raw.bin").open("wb") as raw_log, (output / "nmea.log").open("w", encoding="ascii") as nmea_log:
            connection.open()
            start = time.monotonic()
            buffer, next_status = b"", 0.0
            while time.monotonic() - start < args.duration:
                chunk = connection.read(min(connection.in_waiting or 1, 4096))
                elapsed = time.monotonic() - start
                if chunk:
                    total_bytes += len(chunk)
                    raw_log.write(chunk)
                    raw_log.flush()
                    buffer += chunk
                    while b"\n" in buffer:
                        line, buffer = buffer.split(b"\n", 1)
                        line = line.strip()
                        if not line:
                            continue
                        # A partial first sentence is retained in raw.bin, never accepted as a fix.
                        nmea_log.write(line.decode("ascii", errors="backslashreplace") + "\n")
                        nmea_log.flush()
                        try:
                            text = line.decode("ascii")
                        except UnicodeDecodeError:
                            monitor.bad_lines += 1
                            continue
                        monitor.feed(text, elapsed)
                    if len(buffer) > 16384:
                        monitor.bad_lines += 1
                        buffer = b""
                if elapsed >= next_status:
                    monitor.status(elapsed)
                    next_status = elapsed + 10
    except KeyboardInterrupt:
        interrupted = True
    except (serial.SerialException, OSError) as exc:
        error = str(exc)
        print(f"Serial/logging error: {error}", file=sys.stderr)
    finally:
        connection.close()
    elapsed = time.monotonic() - start
    summary = monitor.summary()
    result = ("INTERRUPTED" if interrupted else "IO_ERROR" if error else
              "PASS" if summary["positioning_success"] else
              "NO_FIX" if monitor.counts else "NO_VALID_NMEA")
    summary.update(result=result, started_at=started.isoformat(),
                   ended_at=datetime.now().astimezone().isoformat(),
                   duration_s=round(elapsed, 3), requested_duration_s=args.duration,
                   port=args.port, baudrate=args.baudrate, environment=args.environment,
                   serial_device={"description": info.description, "hwid": info.hwid} if info else None,
                   bytes_received=total_bytes, error=error,
                   note="Elapsed fix time is measured from monitoring start, not receiver power-on/TTFF.")
    (output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    monitor.status(elapsed)
    print(f"RESULT: {result}; summary: {output / 'summary.json'}", flush=True)
    return 130 if interrupted else 2 if error or not monitor.counts else 0 if result == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())

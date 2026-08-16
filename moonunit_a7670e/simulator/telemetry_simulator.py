#!/usr/bin/env python3
"""Simple telemetry simulator for the Moon Unit API format.

Generates realistic-ish telemetry payloads and optionally POSTs them to the API.
"""

from __future__ import annotations

import argparse
import json
import random
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any, Dict, List


DEFAULT_API_HOST = "telemetry-api-functions.azurewebsites.net"


@dataclass
class SimState:
    lat: float
    lon: float
    altitude_m: float
    speed_kph: float
    course_deg: float
    hdop: float
    satellites: int
    modem_temp_c: float
    battery_v: float
    battery_charge_pct: float
    battery_charge_state: int  # 0=not charging,1=charging,2=full


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def init_state(rng: random.Random, lat: float, lon: float) -> SimState:
    return SimState(
        lat=lat,
        lon=lon,
        altitude_m=120.0,
        speed_kph=0.0,
        course_deg=0.0,
        hdop=1.2,
        satellites=10,
        modem_temp_c=36.0,
        battery_v=4.08,
        battery_charge_pct=83.0,
        battery_charge_state=0,
    )


def evolve_state(rng: random.Random, s: SimState) -> None:
    # GNSS drift / movement
    s.speed_kph = clamp(s.speed_kph + rng.uniform(-2.0, 2.2), 0.0, 62.0)
    s.course_deg = (s.course_deg + rng.uniform(-12.0, 12.0)) % 360.0
    s.altitude_m = clamp(s.altitude_m + rng.uniform(-0.8, 0.8), -20.0, 5000.0)
    s.hdop = clamp(s.hdop + rng.uniform(-0.08, 0.12), 0.6, 4.0)
    s.satellites = int(clamp(s.satellites + rng.randint(-1, 1), 5, 22))

    # Very rough random walk in degrees
    step = s.speed_kph / 111_139.0  # deg-ish scale per tick (not geodetically exact)
    s.lat += rng.uniform(-step, step)
    s.lon += rng.uniform(-step, step)

    # Modem temp follows activity a bit
    temp_delta = 0.01 * s.speed_kph + rng.uniform(-0.35, 0.35)
    s.modem_temp_c = clamp(s.modem_temp_c + temp_delta, 28.0, 68.0)

    # Battery model
    # Charging state occasionally toggles for test coverage.
    if rng.random() < 0.02:
        s.battery_charge_state = rng.choice([0, 1, 2])

    if s.battery_charge_state == 1:
        s.battery_v = clamp(s.battery_v + rng.uniform(0.001, 0.006), 3.45, 4.22)
        s.battery_charge_pct = clamp(s.battery_charge_pct + rng.uniform(0.1, 0.7), 0.0, 100.0)
        if s.battery_charge_pct >= 99.5:
            s.battery_charge_state = 2
    elif s.battery_charge_state == 2:
        s.battery_v = clamp(s.battery_v + rng.uniform(-0.001, 0.001), 4.15, 4.22)
        s.battery_charge_pct = clamp(s.battery_charge_pct + rng.uniform(-0.05, 0.05), 98.0, 100.0)
    else:
        s.battery_v = clamp(s.battery_v - rng.uniform(0.001, 0.004), 3.25, 4.22)
        s.battery_charge_pct = clamp(s.battery_charge_pct - rng.uniform(0.05, 0.45), 0.0, 100.0)


def build_payload(message_id: int, session_id: str, serial_number: str, s: SimState, gnss_valid: bool) -> Dict[str, Any]:
    payload: List[Dict[str, Any]] = [
        {"name": "gnssValid", "time": 0, "value": 1.0 if gnss_valid else 0.0},
        {"name": "satellites", "time": 0, "value": float(s.satellites if gnss_valid else 0)},
        {"name": "temperature", "time": 0, "value": round(s.modem_temp_c, 2)},
        {"name": "battery_charge_state", "time": 0, "value": float(s.battery_charge_state)},
        {"name": "battery_charge", "time": 0, "value": round(s.battery_charge_pct, 1)},
        {"name": "battery_voltage", "time": 0, "value": round(s.battery_v, 3)},
    ]

    if gnss_valid:
        payload.extend(
            [
                {"name": "latitude", "time": 0, "value": round(s.lat, 6)},
                {"name": "longitude", "time": 0, "value": round(s.lon, 6)},
                {"name": "altitude", "time": 0, "value": round(s.altitude_m, 2)},
                {"name": "speed", "time": 0, "value": round(s.speed_kph, 2)},
                {"name": "course", "time": 0, "value": round(s.course_deg, 2)},
                {"name": "hdop", "time": 0, "value": round(s.hdop, 2)},
            ]
        )

    return {
        "messageId": message_id,
        "sessionId": session_id,
        "serialNumber": serial_number,
        "payload": payload,
    }


def post_json(url: str, body: Dict[str, Any], timeout_s: float) -> Dict[str, Any]:
    raw = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(
        url=url,
        data=raw,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout_s) as resp:
            response_text = resp.read().decode("utf-8", errors="replace")
            return {
                "ok": True,
                "status": resp.status,
                "body": response_text,
            }
    except urllib.error.HTTPError as e:
        body_text = e.read().decode("utf-8", errors="replace")
        return {
            "ok": False,
            "status": e.code,
            "body": body_text,
        }
    except Exception as e:  # noqa: BLE001 - simple CLI tool
        return {
            "ok": False,
            "status": 0,
            "body": str(e),
        }


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Telemetry simulator for Moon Unit API")
    p.add_argument("--serial-number", default="unit-2", help="Device serial number used for identity fields.")
    p.add_argument("--url", default="", help="Optional full endpoint override. If omitted, URL is https://<api-host>/api/ingest/sensorlogger.")
    p.add_argument("--api-host", default=DEFAULT_API_HOST, help="Host used when deriving URL from --serial-number.")
    p.add_argument("--endpoint-base", default="/api/ingest/sensorlogger", help="Base endpoint path (serial number will be appended).")
    p.add_argument("--session-id", default="esp32-unit-sim")
    p.add_argument("--start-message-id", type=int, default=0)
    p.add_argument("--count", type=int, default=10, help="Number of messages to send. Use 0 for infinite.")
    p.add_argument("--interval-s", type=float, default=5.0)
    p.add_argument("--timeout-s", type=float, default=15.0)
    p.add_argument("--dry-run", action="store_true", help="Do not POST, only print generated payloads.")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--start-lat", type=float, default=51.5074)
    p.add_argument("--start-lon", type=float, default=-0.1278)
    p.add_argument("--gnss-valid-prob", type=float, default=0.85, help="Probability that a sample has GNSS fix.")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    rng = random.Random(args.seed)
    state = init_state(rng, args.start_lat, args.start_lon)

    target_url = args.url.strip()
    if not target_url:
        endpoint_base = args.endpoint_base.strip() or "/"
        if not endpoint_base.startswith("/"):
            endpoint_base = "/" + endpoint_base
        while len(endpoint_base) > 1 and endpoint_base.endswith("/"):
            endpoint_base = endpoint_base[:-1]

        target_url = f"https://{args.api_host}{endpoint_base}"

    print(f"[sim] Serial number: {args.serial_number}")
    print(f"[sim] Target URL: {target_url}")
    print(f"[sim] Session: {args.session_id}")
    print(f"[sim] Dry run: {args.dry_run}")

    message_id = args.start_message_id
    sent = 0

    while args.count == 0 or sent < args.count:
        evolve_state(rng, state)
        gnss_valid = rng.random() < args.gnss_valid_prob
        body = build_payload(message_id, args.session_id, args.serial_number, state, gnss_valid)

        if args.dry_run:
            print(f"[sim] messageId={message_id} payload={json.dumps(body, separators=(',', ':'))}")
        else:
            result = post_json(target_url, body, args.timeout_s)
            status = result["status"]
            print(f"[sim] messageId={message_id} status={status} ok={result['ok']}")
            text = result["body"]
            try:
                parsed = json.loads(text)
                cfg = parsed.get("config") if isinstance(parsed, dict) else None
                if isinstance(cfg, dict):
                    fields = cfg.get("fields")
                    print(f"[sim] response.config.fields={fields}")
                else:
                    print(f"[sim] response={text}")
            except Exception:  # noqa: BLE001
                print(f"[sim] response={text}")

        sent += 1
        message_id += 1
        if args.count == 0 or sent < args.count:
            time.sleep(max(0.0, args.interval_s))

    print(f"[sim] Done. Sent={sent}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


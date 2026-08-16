# Telemetry Simulator

This simulator generates Moon Unit-style telemetry payloads and can POST them to your API endpoint.

Default target URL is built from two parts:

- `apiHost` (e.g. `telemetry-api-functions.azurewebsites.net`)
- `endpointBase` (e.g. `/api/ingest/sensorlogger`)

Result: `https://<apiHost><endpointBase>`

`serialNumber` is included in the JSON payload only.

## What It Sends

Each message includes:

- `gnssValid`, `satellites`
- `temperature` (simulated modem temp)
- `battery_charge_state`, `battery_charge`, `battery_voltage`
- GNSS fields (`latitude`, `longitude`, `altitude`, `speed`, `course`, `hdop`) when fix is valid

## Requirements

- Python 3.9+

No external Python packages are needed.

## Quick Start

From the firmware repo root:

```bash
python3 simulator/telemetry_simulator.py --dry-run --count 3 --interval-s 1
```

Send real POST requests:

```bash
python3 simulator/telemetry_simulator.py \
  --serial-number "unit-2" \
  --session-id "esp32-unit-sim" \
  --count 10 \
  --interval-s 5
```

Override URL explicitly (optional):

```bash
python3 simulator/telemetry_simulator.py \
  --url "https://telemetry-api-functions.azurewebsites.net/api/ingest/sensorlogger" \
  --count 5
```

Run continuously:

```bash
python3 simulator/telemetry_simulator.py --count 0 --interval-s 10
```

## Useful Flags

- `--dry-run` : prints generated payloads, does not POST
- `--serial-number` : used for simulated device identity fields
- `--endpoint-base` : API endpoint path (default `/api/ingest/sensorlogger`)
- `--count` : number of messages (`0` = infinite)
- `--interval-s` : seconds between sends
- `--gnss-valid-prob` : chance each sample has GNSS fix
- `--seed` : deterministic pseudo-random generation
- `--start-message-id` : initial message id


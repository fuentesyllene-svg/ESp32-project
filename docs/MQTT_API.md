# Cloud Dashboard Interface (MQTT)

Topics are rooted at `pest/<DEVICE_ID>/`, so several traps can share one broker
and one dashboard.

| Topic | Direction | Retained | Purpose |
|---|---|---|---|
| `pest/trap-01/telemetry` | device -> cloud | no | 15-minute records |
| `pest/trap-01/event` | device -> cloud | no | captures, faults, state changes |
| `pest/trap-01/status` | device -> cloud | **yes** | online/offline (MQTT last will) |
| `pest/trap-01/cmd` | cloud -> device | no | commands |

Any broker works: Mosquitto on a VPS, HiveMQ Cloud, ThingsBoard, Adafruit IO.
The dashboard can be Node-RED, Grafana, ThingsBoard, or a Blynk-style app -
nothing in the payload is vendor-specific.

## Envelope and integrity

Every published record is wrapped so the receiver can prove it arrived intact -
this is the mechanism behind the study's "without localized transmission
corruption" criterion:

```json
{
  "seq": 412,
  "d": { ... the record ... },
  "crc": "9f3a10c2"
}
```

* `seq` increments per published record. Gaps mean records were lost in a way
  the store-and-forward spool did not recover.
* `crc` is a CRC-32 (IEEE 802.3, reflected, init 0xFFFFFFFF, final XOR) over
  the **exact serialised bytes of `d`**.

Verify in Python:

```python
import json, zlib

def verify(raw):
    msg = json.loads(raw)
    body = json.dumps(msg["d"], separators=(",", ":"))   # key order preserved
    return format(zlib.crc32(body.encode()), "08x") == msg["crc"]
```

`json.dumps` with `separators=(",", ":")` reproduces ArduinoJson's compact
output, and Python preserves insertion order, so the bytes match.

## Telemetry record

```json
{
  "_t": "telemetry",
  "id": "trap-01",
  "site": "Malamba-Salaysay",
  "ts": "2026-09-01T22:15:03+08:00",
  "up": 187423,
  "state": "LURE",
  "power": {"vbat": 12.41, "vocv": 12.42, "vpv": 0.12, "soc": 74.2,
            "load_a": 0.68, "charging": false},
  "env":   {"t": 26.4, "rh": 88.1, "ldr": 3612},
  "det":   {"cat": 1, "aph": 4, "non": 0, "conf": 82,
            "captures": 17, "fan_s": 214},
  "sys":   {"cam_state": 2, "cam_online": true, "cam_lost": 3,
            "rssi": -71, "heap": 214880, "spool": 0, "deliv": 99.7}
}
```

`env.t` and `env.rh` are `null` when the DHT22 did not answer, rather than a
placeholder number - a gap in the record must not look like a reading.

`state` is one of `BOOT`, `DAY_IDLE`, `LURE`, `CAPTURE`, `PURGE`, `COOLDOWN`,
`LOW_BATTERY`, `DISABLED`.

## Event record

```json
{"_t": "event", "id": "trap-01", "up": 187500,
 "type": "capture", "detail": "trigger=camera"}
```

| `type` | Meaning |
|---|---|
| `armed` / `disarmed` | night window opened / closed |
| `capture` | blower fired; `detail` gives `trigger=camera`, `ultrasonic` or `manual` |
| `capture_skipped` | target detected but battery or fan budget forbade it |
| `purge` | scheduled sweep |
| `low_battery` / `recovered` | crossed the 30 % / 40 % thresholds |
| `cam_reset` | camera power-cycled after a link timeout |
| `disabled` | switched off remotely |

## Commands

Publish JSON to `pest/trap-01/cmd`. The same payloads work against the local
web UI at `POST http://<ip>/cmd`, which is what the dashboard buttons use.

| Payload | Effect |
|---|---|
| `{"cmd":"capture","sec":8}` | run the blower now (0.5-30 s) |
| `{"cmd":"uv","pct":100}` | pin the UV array; `-1` returns it to the duty cycle |
| `{"cmd":"enable","on":false}` | stop the trap (stays powered and logging) |
| `{"cmd":"sensitivity","pct":65}` | detector foreground threshold, 0-100 |
| `{"cmd":"camcycle"}` | power-cycle the camera node |
| `{"cmd":"wipe"}` | clear the CSV log and spool - use at the start of a trial |
| `{"cmd":"reboot"}` | restart the controller |

## Offline behaviour

Records that cannot be published are appended to `/spool.jsonl` in the ESP32's
LittleFS and re-sent in order on reconnect, five at a time. The counters behind
`deliv` are: `generated` (records created), `published` (accepted by the
broker, live or from the spool), `spooled` (waiting), `dropped` (lost to a full
filesystem). `deliv = published / generated`. A trial that reports 100 % on a
flaky link is almost certainly reporting a bug - check `spool` too.

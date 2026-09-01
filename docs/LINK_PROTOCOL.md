# Controller <-> Camera Link Protocol

The ESP32-CAM does the vision work and the ESP32 DevKit does everything else,
so the two boards need a link that survives a noisy 12 V enclosure with a
blower in it. The link is a plain 115200 8N1 UART carrying NMEA-style ASCII
sentences: one line per message, printable characters only, XOR checksum.

ASCII was chosen over a binary protocol on purpose - during a field trial you
can clip a USB-serial adapter onto the link and read exactly what the camera is
reporting, without a decoder.

## Frame format

```
$PEST,<TYPE>,<field>,<field>,...*<CS><CR><LF>
```

* `CS` is the XOR of every character strictly between `$` and `*`, printed as
  two uppercase hex digits.
* Maximum line length is 120 characters. Anything longer is discarded.
* A receiver silently drops any line that does not start with `$PEST,` or whose
  checksum does not match. That is what lets ordinary debug prints share the
  wire without confusing the parser.

## Camera -> Controller

### `DET` - detection report (sent on every processed frame)

```
$PEST,DET,<seq>,<uptime_s>,<caterpillars>,<aphids>,<nontarget>,<conf_pct>,<motion_px>,<fps_x10>*CS
```

| Field | Meaning |
|---|---|
| `seq` | sentence counter, wraps at 65535 - lets the controller count losses |
| `uptime_s` | camera node uptime; a reset shows up as this going backwards |
| `caterpillars` | blobs classified as caterpillar-like this frame |
| `aphids` | blobs classified as aphid-like (small, clustered) this frame |
| `nontarget` | blobs rejected as non-target (fast movers, oversized) |
| `conf_pct` | 0-100 confidence of the strongest target blob |
| `motion_px` | foreground pixel count, a raw activity measure |
| `fps_x10` | processing rate x 10, e.g. `38` = 3.8 fps |

### `STA` - status (every 10 s)

```
$PEST,STA,<seq>,<uptime_s>,<state>,<free_heap>,<frames>,<errors>*CS
```

`state`: `0` booting, `1` learning background, `2` armed, `3` camera error.

## Controller -> Camera

### `CFG` - operating context

```
$PEST,CFG,<uv_on>,<sensitivity>*CS
```

`uv_on` is `1` while the UV array is lit. The camera resets its background
model whenever this changes, because switching a UV array on rewrites every
pixel in the scene and would otherwise register as one enormous detection.
`sensitivity` is 0-100 and scales the foreground threshold.

### `PNG` - ping

```
$PEST,PNG*CS
```

The camera answers with a `STA` sentence. The controller pings when it has not
heard a `DET` for `CAM_LINK_TIMEOUT_MS`; if the ping also goes unanswered it
power-cycles the camera through GPIO23.

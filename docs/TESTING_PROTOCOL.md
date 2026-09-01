# Calibration and Testing Protocol

This maps the study's Testing and Evaluation Plan onto concrete procedures
against this firmware: five consecutive trials, each a continuous 72-hour
window spanning three full day-and-night cycles, with automatic 15-minute
logging and a manual chamber audit at the end of each trial.

Do sections 1-4 **once, on the bench**, before the first field deployment. An
uncalibrated unit produces data that cannot be defended.

---

## 1. Voltage sense calibration (bench, ~15 min)

The ADC dividers are the basis of every power claim in the study.

1. Flash the controller and open the serial monitor at 115200.
2. Put a bench meter across the battery terminals.
3. Read `vbat` from the serial log or `http://<ip>/status.json`.
4. Correct the constant in `config.h`:

   ```
   VBAT_DIVIDER_RATIO_new = VBAT_DIVIDER_RATIO_old x (meter_volts / reported_volts)
   ```

5. Reflash and confirm agreement within **+/-0.05 V** at two points: a rested
   battery (~12.6 V) and one under blower load (~12.0 V).
6. Repeat for `VPV_DIVIDER_RATIO` against the panel's open-circuit voltage.

Record the final ratios in your logbook - they are per-unit and differ with
resistor tolerance.

## 2. Battery internal resistance (bench, ~10 min)

`BATT_INTERNAL_OHMS` is what stops a running blower from being misread as a
flat battery.

1. Note the rested terminal voltage `V_rest`.
2. Run the blower alone (`{"cmd":"capture","sec":20}` on the dashboard) and
   note the loaded voltage `V_load`.
3. `R = (V_rest - V_load) / I_fan`, where `I_fan` is the measured blower
   current. A healthy 9 Ah SLA lands near 0.030 ohm; above ~0.060 ohm the
   battery is aged and the study's power claims should be re-checked on a new
   one.

## 3. Detection calibration (bench, 1-2 h)

The camera thresholds in the `config.h` tab of the `PestTrapCamera` sketch are in units of the
80x60 working grid and depend entirely on camera-to-target distance. They must
be set on the assembled prototype, at the real mounting geometry.

1. Mount the camera at its final position: looking **across** the UV array's
   face, 15-25 cm from the intake, never into the array.
2. Set `CAM_WIFI_DIAGNOSTICS 1` in the camera sketch's `config.h` tab, flash, and
   open `http://<cam-ip>/pgm` to check framing and focus. The OV2640's lens is
   a screw thread - focus it at the intake plane.
3. Place specimens (or, for a first pass, paper cut-outs of realistic size) in
   the field of view and read `http://<cam-ip>/blobs`. It returns the measured
   `area`, `elongation`, `fill`, `speed` and `age` of every blob.
4. Set the thresholds from the measured values:
   * `CAT_MIN_AREA` / `CAT_MAX_AREA` around the observed larval areas,
   * `CAT_MIN_ELONGATION` just below the observed larval elongation,
   * `APH_MIN_AREA` / `APH_MAX_AREA` around individual aphids,
   * `CAT_CONFIDENT_AREA` at the low end of typical larval area.
5. **Set `CAM_WIFI_DIAGNOSTICS` back to 0 and reflash before any trial.** It
   roughly doubles the node's average current and would invalidate section 6.

Record a confusion matrix from this session (targets presented vs. classified).
It is the honest basis for any detection-accuracy claim, and it is a stronger
result than a percentage with no method behind it.

## 4. Dry run (bench, 24 h)

Run the assembled system on the bench for one full day-night cycle with the
real battery and panel. Confirm:

- [ ] `http://<ip>/status.json` reachable, and the fallback hotspot
      `PestTrap-trap-01` appears when the field Wi-Fi is switched off
- [ ] a row lands in `/log.csv` every 15 minutes, with no gaps
- [ ] the trap arms at 18:00 and disarms at 05:00 local (check `state`)
- [ ] the UV array cycles 2 min on / 3 min off while armed
- [ ] the camera's 5 V is cut during the day (`cam_powered` false)
- [ ] MQTT delivery ratio stays at 100 % on a good link
- [ ] pulling the network for an hour spools records, and they arrive on
      reconnect (`spool` returns to 0, `delivery` stays >= 95 %)

## 5. Field trial procedure (per 72-hour trial)

**Before (day 0):**

1. Charge the battery to a rested 12.7 V or above.
2. `{"cmd":"wipe"}` from the dashboard to start the trial with a clean log, or
   download and archive the previous `/log.csv` first.
3. Empty and photograph the collection chamber.
4. Record: trial number, start timestamp, plot location, plant growth stage,
   and the local pest pressure observed by eye.

**During:**

5. Leave the unit alone. Every intervention is a confound.
6. Watch the dashboard remotely. If the unit enters `LOW_BATTERY` before dawn,
   record the timestamp - that is a result, not a failure to be fixed mid-trial.

**After (hour 72):**

7. Download `/log.csv` **before** touching anything. Name it
   `trial-<n>-<date>.csv`.
8. Open the collection chamber and, under magnification, count and categorise:
   caterpillars, aphids, and **every** non-target insect (beneficials
   separately: parasitoid wasps, syrphid flies, lady beetles, pollinators).
9. Photograph the chamber contents next to a scale.
10. Run `python3 tools/analyze_trial.py trial-<n>-<date>.csv` and archive the
    summary with the chamber counts.

## 6. Success criteria and how each is measured

| Criterion (from the study) | Measured by | Passes when |
|---|---|---|
| Battery above 30 % during continuous night operation | `min_soc_night` from `analyze_trial.py` | > 30 % on every night of every trial |
| >= 95 % of logged packets transmitted without corruption | `delivery_pct` (published/generated) and the per-record CRC32 | >= 95 %, with CRC mismatches at 0 |
| Measurable reduction in localised target pests | chamber counts across trials 1-5, against an untrapped control plot | declining trend in the trapped plot |
| High biological selectivity | non-target fraction of the chamber audit | non-targets a small minority of the catch |

Note the fourth row honestly: the firmware's `nontarget` counter measures what
the *camera rejected*, not what the *trap caught*. Only the physical chamber
audit measures selectivity, because the blower captures whatever is in the
suction cone regardless of what the classifier decided. Report the two
separately; conflating them would overstate the result.

## 7. Controls worth running

The study design compares the prototype against manual practice. Two cheap
additions make the efficacy claim much harder to dismiss:

* **An untrapped control plot** of the same variety and growth stage, same
  distance from the field edge, audited on the same schedule.
* **A UV-only night** (`{"cmd":"enable","on":false}` after the array is lit
  manually with `{"cmd":"uv","pct":100}`), separating "insects attracted" from
  "insects removed by the blower". Without it, attraction and capture cannot be
  told apart in the results.

## 8. Troubleshooting during a trial

| Symptom | Likely cause | Action |
|---|---|---|
| Unit resets when the blower starts | inrush sag; missing bulk capacitor or flyback diode | fit both (`docs/WIRING.md` section 2); note the affected trial |
| `cam_online` false at night | link wiring, or the camera browning out | check GPIO13/14 and the 5 V rail under load |
| Detections but no captures | SoC between 30 % and 35 %, or fan budget spent | check `capture_skipped` events; this is designed behaviour |
| Capture on every UV transition | camera not receiving `CFG`, so it never resets its background | check the controller-to-camera direction of the link |
| Delivery ratio below 95 % | broker unreachable for longer than the spool holds | check `spool`; enlarge `SPOOL_MAX_BYTES` or shorten the interval |
| Clock reads `uptime+NNN` | NTP never synced | data is still valid; align it to the trial start time |

# CAD Mouse MK2 - Alternative Firmware

This is an alternative implementation for the awesome CAD Mouse MK2
([Code](https://github.com/sb-ocr/cad-mouse-mk2), [YouTube Video](https://youtu.be/62xlzGs8LXA), [Instructables](https://www.instructables.com/CAD-Mouse-MK2-a-6DoF-Space-Mouse-Using-Magnets), [preassembled PCB](https://ocrlab.myshopify.com/products/sensor-board-cad-mouse-mk2))

*I am not affiliated with the original author in any kind.*

I opted for a reimplementation rather than a fork because I wanted to learn how it works. Yet, several features closely follow the original implementation.

## Hardware

I chose a (pin compatible) SeeedStudio XIAO RP2350 instead of the original RP2040 because the former has

- generally more processing power
- hardware accelerated floating point capabilities

The PCB is a preassembled one I bought. Only hickup was a broken LED.
Hence my config.h contains setting for 7 instead of original 8 LEDs.

## PlatformIO Environments

The project now provides two PlatformIO environments:

- `RP2040`
- `RP2350`

`RP2040` is the default environment in `platformio.ini`; it runs the RP2040 at
200 MHz (`board_build.f_cpu`), which the core supports by raising the core
voltage to 1.15 V.

## Performance Status

- **RP2040 (after recent performance improvements):**
  - Filter runtime is roughly **6.6 ms** on average.
  - HID report interval is set to **7 ms**, which corresponds to about **142.9 Hz**.
- **RP2350 (before recent performance improvements):**
  - HID report interval was set to **4 ms** (**250 Hz**), with a noted Core1 roundtrip time of about **2 ms** (from `config.h` comment).
  - Post-improvement numbers on RP2350 are not measured yet.

## Features

### Concept

The core idea was to improve the sensor processing / motion engine with

- a physics based dipole model
- in combination with an extended Kalman filter
- using dual-core capabilities of the MCU for the filtering

#### Dipole Physics Model

- The physics model represents each magnet by a dipole equation at the center of each magnet.
- The Hall-sensors are assumed to be in an equilateral triangle in a x-y-plane.
- The magnets are assumed to be in an equilateral triangle
  - in a x-y-plane above the sensors in neutral position
  - fixed to each other but transalted or rotated around the springs center
- [An external test recording raw readings at various known fixed positions revealed that a single dipole per magnet is sufficient to predict raw readings](https://github.com/sb-ocr/cad-mouse-mk2/issues/19#issuecomment-4987111997)

#### Kalman Filter and Postprocessing

- The sensor data are read in main loop and sent to Core1 for filtering
- an Extended Kalman filter based on the physics model then filter and predicts smooth x,y,z,rx,ry,rz state, as well as their (angular) velocities vx,vy,vz,vrx,vry,vrz
- Postprocessing does
  - normalize the translation (mm) and rotation (deg / rad) to [-1,1] -> still need to expose the limits
  - translation and rotation vectors are independently deadzones by calculating their vector magnitude, so we don't have any jitter in neutral position
  - to isolate movements, the combined state vector is cubed and renomarlized to pronounce movements of a dominant axis

### Calibration

Calibration is done in firmware by

- collecting and averaging sensor data
- fitting each magnets dipole magnetic moment via the dipole physics engine to best represent the Hall sensor magnetic field data
- fitting tiny x,y,z,rx,ry,rz offsets due to slight assembly inacuracies also using the dipole physics engine
- persisting the calibration results to file with LittleFS

Initial start triggers calibration and attempts to store it to LittleFS. Consecutive calibrations can be manually triggered by long press of both buttons simultaneously.

### LED Color Codes

All firmware LED colors use `0xRRGGBB` values from `firmware/include/config.h`.
Permanent effects describe the current state and remain active until another
state changes them. Queued animations are one-time event notifications and
restore the relevant permanent effect afterwards.

#### Permanent Colors and Effects

| Constant                                | Hex code   | Color  | Effect                     | Meaning                              |
| --------------------------------------- | ---------- | ------ | -------------------------- | ------------------------------------ |
| `LED_BOOT_COLOR`                        | `0xFFFF00` | Yellow | Solid                      | Boot state                           |
| `LED_ERROR_COLOR`                       | `0xFF0000` | Red    | Full spinner               | Sensor error                         |
| `LED_CALIBRATION_COLOR`                 | `0x0000FF` | Blue   | Full spinner               | Calibration in progress              |
| `LED_RUNNING_COLOR`                     | `0xFFFFFF` | White  | Solid; optional input glow | Normal running with calibration      |
| `LED_RUNNING_WITHOUT_CALIBRATION_COLOR` | `0xFF6600` | Orange | Solid                      | Running without calibration          |
| `LED_NOMAD_MODE_COLOR`                  | `0x6000FF` | Violet | Solid                      | Nomad mode                           |
| `LED_INPUT_GLOW_COLOR`                  | `0x00FFFF` | Cyan   | Dynamic input glow         | Input feedback during normal running |
| internal `0x000000`                     | `0x000000` | Black  | Off                        | LEDs switched off, including sleep   |

#### One-Time Queued Animations

All entries below use `queue_blinking_animation` with 200 ms on and 200 ms
off durations. The blink count is part of the event code and should remain
unique for different events that use the same color.

| Constant                        | Hex code   | Color   | Blinks | Event                                                          |
| ------------------------------- | ---------- | ------- | -----: | -------------------------------------------------------------- |
| `LED_SUCCESS_COLOR`             | `0x00FF00` | Green   |      1 | Sensor check succeeded                                         |
| `LED_CALIBRATION_SUCCESS_COLOR` | `0x00FFFF` | Cyan    |      1 | Calibration file loaded successfully                           |
| `LED_CALIBRATION_FAILURE_COLOR` | `0xFF00FF` | Magenta |      1 | Filesystem initialization failed at startup                    |
| `LED_CALIBRATION_FAILURE_COLOR` | `0xFF00FF` | Magenta |      2 | Calibration file could not be loaded; fresh calibration starts |
| `LED_CALIBRATION_FAILURE_COLOR` | `0xFF00FF` | Magenta |      3 | Calibration sample collection timed out                        |
| `LED_CALIBRATION_FAILURE_COLOR` | `0xFF00FF` | Magenta |      4 | Fresh calibration succeeded, but saving failed                 |
| `LED_CALIBRATION_FAILURE_COLOR` | `0xFF00FF` | Magenta |      5 | Fresh calibration failed; existing calibration remains active  |
| `LED_CALIBRATION_FAILURE_COLOR` | `0xFF00FF` | Magenta |      6 | Fresh calibration failed; no calibration is available          |
| `LED_SUCCESS_COLOR`             | `0x00FF00` | Green   |      2 | Fresh calibration succeeded and was saved                      |
| `LED_NOMAD_MODE_COLOR`          | `0x6000FF` | Violet  |      3 | Nomad mode switched on (blinks 150 ms on / 150 ms off)         |
| `LED_NOMAD_MODE_COLOR`          | `0x6000FF` | Violet  |      1 | Nomad mode switched off                                        |

The blink code is unique for every event within a color. Filesystem and
calibration-file failures intentionally use the same Magenta family because
the filesystem is currently used exclusively for calibration persistence.
`LED_CALIBRATION_SUCCESS_COLOR` and `LED_INPUT_GLOW_COLOR` also share
`0x00FFFF`, but one is a queued event and the other is a permanent dynamic
effect.

### Configurator

`tools/configurator.html` tunes the mouse live without reflashing. Open it in
Chrome or Edge (it uses Web Serial), click "Подключить мышь" and pick the CAD
Mouse port. It shows all six axes before and after the deadzones, changes the
settings below immediately, runs a calibration, and stores the settings in
LittleFS (`/settings.bin`). Saved settings override the `config.h` defaults on
boot; "По умолчанию" restores the `config.h` values.

#### Page on the mouse

The page is also stored in the mouse. Hold one button while plugging the mouse
in and it additionally shows up as a read-only USB drive `CADMOUSE` with
`configurator.html`; plug it in again without a button to hide the drive. Do
not hold both buttons, a long press of both starts a calibration. The page's
"Показать диск сейчас" button (command `drive`) reboots straight into drive
mode.

The disk image is a FAT12 volume generated from `tools/configurator.html` by
`tools/make_drive_image.py`, which PlatformIO runs before every build
(`extra_scripts`). The generated `lib/config_drive/drive_image.h` is not
committed.

#### Axis decoupling

The "Развязка осей" wizard asks to move the knob in 12 directions, records the
physical pose (mm, rad) of all six axes, and fits a 6x6 matrix that keeps the
intended axis and removes the response of the others (least squares with a
small pull towards identity, on axes scaled by their full-scale travel). The
matrix is applied before normalization (`lib/decoupling`), so it does not depend
on the sensitivity settings, and is stored in LittleFS (`/decoupling.bin`).
With good decoupling `ISOLATION_POWER` can be lowered to about 1.5–2 for more
natural combined movements.

#### Nomad mode

Nomad Sculpt (desktop) reads a 3D mouse on its own, while 3DxWare also turns
the motion into mouse wheel events for it, and both act at the same time. In
Nomad mode the knob instead drives Nomad's own mouse navigation, which is what
its default bindings are built around:

| Knob | Mouse | Nomad |
| --- | --- | --- |
| Twist (RZ) and sideways tilt (RY) | right button drag, horizontal | view_rotate |
| Forward/backward tilt (RX) | right button drag, vertical | view_rotate |
| Push sideways (X), pull up/press down (Z) | middle button drag | view_pan |
| Push forward/backward (Y) | wheel | zoom |
| Short press left / right button | Ctrl+Z / Ctrl+Space | undo / view_focus |

A long press of the right button (1.5 s) or the page toggles the mode; the
mouse always starts in CAD mode and does not sleep while the mode is on. The
3D mouse interface sends nothing meanwhile, so neither 3DxWare nor Nomad's own
3D mouse support act on the motion.

The drags move the real cursor. It is kept within 100 px of where the gesture
started: past that the button is released, the cursor moved back and the button
pressed again, so a continuous rotation never hits a screen edge. When the knob
returns to rest the button is released and the cursor moved back to where it
was. Mouse and keyboard reports go through a separate interface, so the 3D
mouse interface stays unchanged for 3DxWare.

Each axis can be assigned to rotate, pan or zoom and inverted, the speeds, the
mouse buttons (with an optional Ctrl/Shift/Alt modifier) and the shortcuts of
the knob buttons can be changed on the page. The configuration is stored in
LittleFS (`/nomad_mode.bin`). Building with `-DNOMAD_MODE_SELFTEST` adds an
`nm_test` command that runs scripted knob input through the state machine and
prints the resulting reports without sending anything over USB.

#### Protocol

The page talks a line-based protocol over the USB serial port; replies are
single-line JSON objects:

| Command | Effect |
| --- | --- |
| `get` | Print the current settings |
| `set <key> <value>` | Change one setting immediately (clamped to its range) |
| `save` / `reload` / `defaults` | Store, reload the stored, or restore `config.h` settings |
| `dc_get` / `dc_set <36 values>` | Print or replace the decoupling matrix (row by row) |
| `dc_on <0\|1>` | Apply or bypass the decoupling matrix |
| `dc_save` / `dc_reload` / `dc_reset` | Store, reload the stored, or reset to identity (off) |
| `nm_get` / `nm_set <24 values>` | Print or replace the Nomad mode configuration (order as in `lib/configurator/configurator.cpp`) |
| `nm_on <0\|1>` / `nm_save` / `nm_defaults` | Toggle Nomad mode, store its configuration, or restore the Nomad defaults |
| `info` | Firmware version, CPU clock, drive mode, calibrated magnetic moments and offsets |
| `stream 1` / `stream 2` / `stream 0` | ~30 Hz telemetry (incl. Nomad mode and the current gesture); `2` adds the physical pose for the wizard; the mouse stays awake while streaming |
| `calibrate` | Start a calibration, same as a long press of both buttons |
| `drive` / `reboot` | Reboot with or without the configurator drive |

Setting keys and ranges are defined in `lib/settings/settings.cpp` and must
match the page.

### Quick Tuning Guide

The filtering pipeline is applied in this order:

1. The dipole model and EKF estimate the physical pose: translation in mm and
   rotation in radians.
   - `EKF_PROCESS_NOISE_STD`: controls how much the estimate can respond to
     model changes. Higher values are more responsive but noisier; lower values
     are smoother but slower.
   - `EKF_SENSOR_NOISE_STD`: controls how much the EKF trusts sensor readings.
     Higher values produce smoother output; lower values follow the sensors more
     directly.
   - For EKF tuning, temporarily use very low thresholds in step 3 (around
     `0.005` to `0.02`); otherwise, the deadzone may hide the residual jitter
     being assessed.
2. Normalization scales pose values relative to their configured limits. Values
   may temporarily exceed `[-1, 1]`; the later isolation step 4 bounds the
   final pose output to that range.
   - `NORMALIZATION_*_MAX` / `NORMALIZATION_*_MIN`: define the physical movement
     required to reach normalized `+1` or `-1`. Translation limits are in mm;
     rotation limits are configured in degrees and converted to radians
     internally. Separate positive and negative limits allow asymmetric
     behavior to be tuned, for example when pulling the knob up requires a
     different effort than pushing it down.
3. Separate translation and rotation deadzones remove small residual drift.
   - `DEADZONE_TRANSLATION_THRESHOLD`: removes small translation movement around
     zero.
   - `DEADZONE_ROTATION_THRESHOLD`: removes small rotation movement around zero.
   - Both thresholds use the magnitude of their respective 3-dimensional
     vector, so a combined translation or rotation movement is evaluated as a
     whole rather than axis by axis.
   - With the current value of `0.05`, magnitudes below 5% of the normalized
     range are treated as zero, filtering residual drift and noise after EKF
     processing.
   - Values outside the deadzone are rescaled to preserve the full usable
     range.
   - `DEADZONE_CROSS_AXIS_RATIO`: ignores axes weaker than this share of the
     strongest of all six axes, e.g. the slight tilt that comes with a sideways
     push. Stronger axes shrink smoothly and the dominant axis is unchanged.
     `0` (default) disables it; higher values isolate more but make combined
     movements harder, and a slight push when grabbing the knob can win the
     start of every movement.
   - `AUTO_ZERO_TIME_CONSTANT_S`: while the knob rests inside both deadzones,
     the rest pose is followed with this time constant and subtracted, so
     spring creep and hysteresis do not bias the next movement. The learned
     offset is limited to half a deadzone, so it always heals itself, and it is
     reset after a calibration. `0` (default) disables it.
4. 6DoF isolation emphasizes the dominant movement and suppresses unintended
   secondary axes.
   - `ISOLATION_POWER`: controls how strongly the dominant movement is emphasized
     across all six degrees of freedom. Higher values make an isolated shift or
     tilt cleaner by suppressing smaller unintended components; lower values
     preserve more blended movement.
5. The normalized values are converted into HID reports.
   - `AXIS_LIMIT`: sets the maximum HID output value. Higher values produce
     faster maximum on-screen motion; lower values reduce maximum speed without
     changing the physical normalization.

The EKF also estimates velocity components. They retain their physical units;
for example, moving 1 mm in 1 second corresponds to 1 mm/s. During
postprocessing, pose and velocity values use the same configured limits and
then receive the same deadzone and isolation gains. This removes physical units
from the pose representation while keeping velocity changes consistent with
the corresponding pose changes.

For example, with a 2 mm translation limit, 1 mm becomes `0.5` and `0.5 mm/s`
becomes `0.25` normalized units per second.

Only the six pose components are sent in the HID report. We still process
velocities consistently because the additional cost is negligible and keeps
future or alternative uses of the velocity data correct.

### ToDo

The filter settings and normalization limits may need further tuning.

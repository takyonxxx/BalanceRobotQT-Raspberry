# BalanceRobot — Two-Wheel Self-Balancing Robot with BLE Remote Control

A two-wheel self-balancing robot built on a Raspberry Pi with an MPU6050 IMU,
controlled remotely over Bluetooth Low Energy. Includes an iOS remote control
app, on-device text-to-speech, optional Wikipedia lookup, and an ALSA-based
audio capture pipeline.

Project demo: https://www.youtube.com/watch?v=immSrXEHzQE&feature=youtu.be

Motor driver: Waveshare RPi Motor Driver Board —
https://www.waveshare.com/wiki/RPi_Motor_Driver_Board

---

## What's new in v2

The control stack was rewritten on top of the original codebase. Key changes:

- **IMU-only stabilization.** No encoders required. Pitch (balance) and yaw
  (heading) are both controlled from a single MPU6050.
- **Two PID loops:**
  - **Pitch PID** — Kalman fusion of accel and gyro X, output is common motor PWM.
  - **Yaw PID** — bias-corrected, low-pass-filtered gyro Z, output is left/right
    motor differential. This keeps the robot pointing straight without an
    external compass or encoders.
- **Automatic gyro bias calibration** on every startup (~2 s with the robot
  held still). Saved to `imu_bias.ini`.
- **Auto-arm and auto-recovery.** When you place the robot upright (~8° of
  vertical) and hold it still for ~300 ms, it arms itself. If it falls past
  ±40°, motors stop; when you stand it back up, it auto-arms again.
- **Soft-start motor ramp** (20 cycles ≈ 100 ms) to avoid a hard kick on arm.
- **Long-term auto-trim.** While idle and balanced, the controller slowly
  learns the natural lean angle so center-of-gravity changes (e.g. battery
  swap, mounted payload) self-correct over a few seconds.
- **Live telemetry stream over BLE** (~10 Hz). iOS app shows angle, gyro,
  target, PWM, and status flags.
- **Improved BLE protocol.** New commands: `mTelemetry`, `mAutoMode`,
  `mTrimFine`, `mPositionHold` (now acts as yaw-lock toggle), `mResetTrim`.
- **iOS app fixes.** Two pre-existing bugs were fixed:
  - File-scope `delegate` declaration in `BluetoothEventsHandler.swift` that
    prevented compilation.
  - Wrong peripheral name filter (`"raspberrypi"` instead of `"Balance Robot"`)
    that made the app unable to find the robot.
- **iOS app additions.** Analog joystick (touch-drag), live telemetry panel,
  ARM / AUTO / HOLD toggles, fine-trim buttons.

The original control loop, the `Kalman.h` filter (previously declared but
never used), the encoder ISRs, and the velocity/position-hold cascade are all
replaced by the new architecture.

---

## Control architecture

```
┌──────────────────────────────────────────────────────────────────┐
│  MPU6050                                                          │
│   • accel y/z → atan2  →┐                                         │
│   • gyro X (bias-corr.) ├─→ Kalman ──→ pitch angle (°)            │
│   • gyro Z (bias-corr.) ──→ LPF ──→ yaw rate (°/s)                │
└──────────────────────────────────────────────────────────────────┘
                            │                         │
                            ▼                         ▼
              ┌──────────────────────┐    ┌──────────────────────┐
              │  PITCH PID           │    │  YAW PID             │
              │  setpoint = trim     │    │  setpoint = 0 °/s    │
              │  out: common PWM     │    │  out: L/R diff       │
              └──────────────────────┘    └──────────────────────┘
                            │                         │
                            └──────────┬──────────────┘
                                       ▼
                              L = pidOut − yawDiff
                              R = pidOut + yawDiff
                              (+ user turn / speed commands)
```

When the user issues a turn command (joystick or button), the yaw PID is
temporarily disabled and its integral is reset, so the robot turns freely.
When the user releases, the yaw lock re-engages at the new heading.

When the tilt error exceeds 2°, the yaw correction is linearly faded out (zero
above 6°). Balance is always the priority.

---

## PID tuning notes

### Pitch loop

- **Proportional (Kp)** is the primary response. Too low: the robot leans and
  doesn't recover. Too high: visible oscillation, motors slamming both ways.
- **Integral (Ki)** removes steady-state lean (e.g. center-of-gravity offset).
  Too high: low-frequency wobble.
- **Derivative (Kd)** adds damping. Note: in v2 the derivative is computed
  from the gyro X rate directly (°/s), not from the angle error difference,
  so the Kd scale is roughly **10× smaller** than the original code's Kd.
  Old `Kd = 1.2` corresponds to roughly `Kd = 0.1` here.

Defaults: `Kp = 18`, `Ki = 80` (slider), `Kd = 0.1`. Slider values for Ki are
scaled internally by `× 0.01` (slider 80 → effective Ki 0.8). Slider values
for Kd are sent as `× 10` (slider 1 → effective Kd 0.1).

### Yaw loop

The **SD slider** now controls yaw gain instead of a wheel-speed-difference
constant. Slider 0–100 maps to:
- `yawKp = SD × 0.1` (e.g. SD 20 → yawKp 2.0)
- `yawKi = SD × 0.001`

Default `SD = 2.0` is intentionally conservative. If the robot drifts in
heading, raise SD. If you see the robot fighting itself or oscillating left
and right, lower SD.

### When to invert a sign

Several sign flags are stored in `settings.ini`. If the robot does the
opposite of what's expected, edit the relevant flag:

| Flag | Effect | Default |
|---|---|---|
| `pidInvert` | Inverts the pitch PID output. If the robot accelerates the way it's already falling, flip this. | `true` |
| `motorInvertL` | Reverses left motor direction (independent of PID sign). | `false` |
| `motorInvertR` | Reverses right motor direction. | `false` |
| `yawInvert` | Inverts the yaw correction. If the robot spins faster when yaw correction kicks in (positive feedback), flip this. | `true` |

These are also visible in every `ARMED` log message as e.g. `inv=P--Y`
(uppercase letter = that flag is on).

---

## First-time setup

Wipe any old config so the v2 defaults apply:

```bash
cd ~/BalanceRobotPI
rm -f settings.ini imu_bias.ini
```

On every startup the robot must be **still on a level surface** for the first
two seconds while the gyro biases are measured and written to `imu_bias.ini`.

After that, place the robot upright; auto-arm engages once it has been within
~8° of vertical for ~300 ms.

---

## Diagnostic log

The control thread prints a status line about every 250 ms:

```
ang=+0.50 trg=+0.05 gyroX= +2.3 gyroZ= +1.1 yawC= +0.0 | pid=  -8 L= -8 R= -8
```

| Field | Meaning |
|---|---|
| `ang` | Filtered pitch angle (°). |
| `trg` | Target angle (trim + learned auto-zero). |
| `gyroX` | Pitch rate, bias-corrected (°/s). |
| `gyroZ` | Yaw rate, bias-corrected and LP-filtered (°/s). |
| `yawC` | Yaw correction applied to the L/R differential. |
| `pid` | Pitch PID output (signed PWM). |
| `L`, `R` | Final PWM sent to each motor (after yaw, turn, ramp). |

Use the log to tune:
- `gyroZ` consistently nonzero with `yawC` near zero → raise SD (yaw gain).
- `yawC` saturated (±25) for long stretches → yaw is fighting reality;
  check `yawInvert` and lower SD.
- `pid` saturating to ±255 while `ang` keeps growing → pitch loop is losing
  control; lower Kp and/or raise Kd.

---

## BLE protocol summary

Each frame: `header(0xB0) | length | rw | command | payload...`

Read/write commands the iOS app already used:
`mForward 0xA0`, `mBackward 0xA1`, `mLeft 0xB0`, `mRight 0xB1`,
`mPP 0xC0`, `mPI 0xC1`, `mPD 0xC2`, `mAC 0xD0`, `mSD 0xD1`,
`mSpeak 0xE0`, `mData 0xE1`, `mArmed 0x03`, `mDisArmed 0x04`.

New v2 commands:

| ID | Direction | Payload | Meaning |
|---|---|---|---|
| `mTelemetry 0xF0` | Pi → Phone | 14 bytes (see below) | Live status, ~10 Hz |
| `mAutoMode 0xF1` | Phone → Pi | 1 byte | 0/1 toggle auto-arm |
| `mTrimFine 0xF2` | Phone → Pi | int16 BE | Signed trim offset, 0.01° |
| `mPositionHold 0xF3` | Phone → Pi | 1 byte | 0/1 toggle yaw lock |
| `mResetTrim 0xF4` | Phone → Pi | — | Clear learned + manual trim |

Telemetry payload (big-endian):

```
[0..1]  angle       int16,  × 0.01°
[2..3]  pitch rate  int16,  × 0.1 °/s
[4..5]  target ang  int16,  × 0.01°
[6..7]  trim        int16,  × 0.01°
[8..9]  pwmL        int16
[10..11] pwmR       int16
[12]    flags       bit0=armed bit1=fallen bit2=autoMode bit3=yawLock
[13]    reserved
```

---

## iOS Remote Control App

The Xcode project lives in `BalanceRobotRemote_IOS/`. Two source files are
new in v2 and must be added to the Xcode project manually (drag into the
Project Navigator, "Copy items if needed" checked):
- `RobotControlBLE/JoystickView.swift`
- `RobotControlBLE/TelemetryView.swift`

The rest of the changed files (`ViewController.swift`, `MessageService.swift`,
`Ble/BluetoothEventsHandler.swift`, `Ble/BluetoothConnectionHandler.swift`)
overwrite existing files.

UI elements:
- **Telemetry panel** (top): live angle, gyro rates, target, trim, PWM, and
  status indicators (ARM / AUTO / HOLD / FALL).
- **ARM / AUTO / HOLD** buttons: manual arm/disarm, toggle auto mode, toggle
  yaw lock.
- **Trim −/0/+** buttons: ±0.1° fine adjustments, `0` clears learned trim.
- **Analog joystick** (lower right): continuous forward/back + turn. Releases
  to neutral automatically. Commands are throttled to 50 ms intervals so BLE
  is never flooded.

The original directional buttons from the storyboard remain functional for
backward compatibility.

---

## Required libraries

```bash
sudo apt update && sudo apt upgrade
sudo apt install \
  alsa-utils espeak libespeak-dev libasound2-dev \
  libbluetooth-dev libflac-dev bluetooth blueman bluez \
  libusb-dev libdbus-1-dev libglib2.0-dev libudev-dev \
  libical-dev libreadline-dev \
  i2c-tools libi2c-dev \
  qt5-default qtconnectivity5-dev qtmultimedia5-dev \
  libqt5multimedia5-plugins
```

Enable I2C on the Pi:

```bash
sudo raspi-config
# Interface Options → enable I2C, SPI, Remote GPIO
sudo reboot
```

Install the latest WiringPi (the apt version is outdated on Pi 4 / 5):

```bash
sudo apt purge wiringpi
cd /tmp
wget https://project-downloads.drogon.net/wiringpi-latest.deb
sudo dpkg -i wiringpi-latest.deb
gpio -v
```

## Build

```bash
cd BalanceRobotPI
qmake
make -j4
```

## Run

```bash
sudo ./BalanceRobotPI
```

Root is required because of GPIO access (or set up the suid trick below).

---

## Autostart on boot

Assuming the binary lives at `/home/pi/BalanceRobotPI`.

Create the launcher `~/start_robot.sh`:

```bash
#!/bin/bash
sudo chown root.root /home/pi/BalanceRobotPI/BalanceRobotPI
sudo chmod 4755 /home/pi/BalanceRobotPI/BalanceRobotPI
cd /home/pi/BalanceRobotPI
sudo ./BalanceRobotPI
```

Make it executable:

```bash
chmod +x ~/start_robot.sh
```

Create the systemd unit `/lib/systemd/system/balancerobot.service`:

```ini
[Unit]
Description=Balance Robot Service
After=multi-user.target

[Service]
Type=idle
ExecStart=/home/pi/start_robot.sh

[Install]
WantedBy=multi-user.target
```

```bash
sudo chmod 644 /lib/systemd/system/balancerobot.service
sudo systemctl daemon-reload
sudo systemctl enable balancerobot.service
sudo reboot
sudo systemctl status balancerobot.service
```

---

## Troubleshooting

**Robot won't auto-arm.** Check the `AUTO` indicator in the iOS panel. If it's
off, tap `AUTO`. Robot must be within ±8° of vertical and held still for
~300 ms.

**Robot oscillates rapidly back and forth.** Lower Kp and/or raise Kd from
the PID settings dialog. Confirm `pid` values in the log aren't hitting ±255
on every print.

**Robot drifts in one direction even at rest.** Use the `Trim ±` buttons to
nudge the target angle. Long-term auto-trim will then refine it. Press `Trim 0`
to reset what's been learned and start over.

**Robot spins / can't hold heading.** Increase `SD` slider. If raising SD
makes it worse, the yaw direction is reversed — edit `settings.ini`, change
`yawInvert=true` to `false`, and restart.

**Robot accelerates the way it's already tipping (falls immediately when
armed).** `pidInvert` is wrong — edit `settings.ini` and flip it.

**iOS app can't find the robot.** It searches for peripherals whose name
contains `balance` or `rasp`. The Pi advertises as `"Balance Robot"`. If your
device is named otherwise, edit `expectedNamePrefixes` in
`BluetoothConnectionHandler.swift`.

**Gyro biases look extreme** (e.g. > 5 °/s on any axis). The robot wasn't
still during the 2 s startup calibration. Delete `imu_bias.ini` and restart.

---

## Hardware references

- Two-wheel chassis with two DC gear motors (any common ones; the original
  build used the Waveshare drive kit linked above).
- Raspberry Pi (any model with Bluetooth; Pi 4/5 preferred for headroom).
- MPU6050 IMU on I²C (`0x68`).
- H-bridge motor driver — original code uses the Waveshare RPi Motor Driver
  Board. Pin assignments are in `constants.h`:

```
PWMR1 = 31   PWMR2 = 33   PWMR (PWM) = 32
PWML1 = 38   PWML2 = 40   PWML (PWM) = 37
```

The encoder pins (`SPD_INT_*`, `SPD_PUL_*`) defined in `constants.h` are
**not used** by v2 and can be repurposed.

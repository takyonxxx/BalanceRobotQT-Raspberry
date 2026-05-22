# Balance Robot — Pi 5 + iOS Remote (BLE)

A two-wheeled self-balancing robot. Qt/C++ control application on Raspberry Pi 5, with an iOS BLE remote control.

<p align="center">
    <a href="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/remote_ios.jpg">
        <img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/remote_ios.jpg"
             alt="iOS Remote" width="360" border="1">
    </a>
</p>

## Demo video

See the robot in action: [Balance Robot — YouTube](https://www.youtube.com/watch?v=qpsppoBpccU)

## Architecture

```
iOS Joystick ──BLE GATT──> Pi 5 (Qt/C++) ──> Motor Driver
                              ↑                    ↓
                           MPU6050              Encoders
                          (IMU @ I2C)          (Quadrature)
```

**Control architecture — B-Robot style cascaded PID:**

```
joystick ──> Speed PID ──> target tilt ──> Pitch PID ──> motor PWM
              (outer)                       (inner)
                ↑                              ↑
            encoder vel                  IMU angle + gyro
```

- **Speed PID (outer loop):** Computes the correct body tilt to track the target velocity. When the stick is released, it leans the opposite way to **brake automatically**.
- **Pitch PID (inner loop):** Generates motor PWM to chase the target angle and keep the robot upright.

## Parameters

All parameters can be tuned live from the iOS Settings tab and are persisted to `settings.ini` on the Pi.

### Pitch PID — Balance (inner loop)

| Parameter | Default | Description |
|-----------|---------|-------------|
| P (Proportional) | 25 | Instant response to angle error. High = stiff correction, oscillation risk. |
| I (Integral) | 40 (0.40 real) | Corrects persistent angle error (gravity bias). |
| D (Derivative) | 0.10 | Angle rate of change — damping. High = reduces swinging. |
| Angle trim (AC) | 0.0° | Manual angle offset. |
| Yaw gain (SD) | 2.0 | Heading correction from encoder difference. |

### Speed PID — Velocity control (outer loop)

| Parameter | Default | Description |
|-----------|---------|-------------|
| Speed Kp | 0.12 | Instant response to velocity error. High = agile but oscillates. |
| Speed Ki | 0.20 | Integrates velocity error — the workhorse of the Speed PID. |
| Speed Max Tilt | 5° | Maximum tilt angle the Speed PID can request. |
| Speed Max Vel | 3.0 | Target velocity at full stick deflection (encoder ticks/loop). |

### iOS-only

| Parameter | Default | Description |
|-----------|---------|-------------|
| Forward speed | 180 | Max byte value (0-255) sent for forward/back joystick. |
| Turn speed | 60 | Max byte value sent for left/right joystick. |

## Tuning guide

### For snappier response
1. First raise **Speed Max Vel** (3.0 → 4.0 → 5.0) — increases target velocity ceiling
2. Then raise **Speed Kp** (0.12 → 0.15 → 0.18) — more agile response
3. If still not enough, raise **Speed Max Tilt** (5° → 6° → 7°) — more aggressive acceleration

### Symptoms and fixes
- **Long coast after stick release** → raise Speed Ki (0.20 → 0.30)
- **Oscillating / jittery** → lower Speed Kp
- **Swings back and falls after release** → lower Speed Ki
- **Drifts in place, won't sit still** → raise Pitch Ki
- **High-frequency vibration** → raise Pitch Kd
- **Front-to-back wobble during startup** → lower Pitch Kp

### Reset to Defaults
At the bottom of the iOS Settings tab, the red **"Reset all settings to defaults"** button restores every PID parameter to the Pi-side defaults.

## Direction convention

- **Joystick up** = forward
- **Joystick down** = backward
- **Joystick left/right** = turn
- On the Pi: `cmd > 0` means forward, `cmd < 0` means backward
- Encoder positive direction = robot's forward direction

`ControlViewController.swift:41` has `invertY = false` to enforce this. If your motors are wired in reverse, flip it to `true`.

## BLE protocol — Message IDs

`message.h` (Pi) and `MessageService.swift` (iOS) must match.

```
mHeader        = 0xb0  // Packet header
mWrite         = 0x01  // Write request
mRead          = 0x02  // Read request
mArmed         = 0x03  // ARM robot
mDisArmed      = 0x04  // DISARM robot
mForward       = 0xa0  // Forward command
mBackward      = 0xa1  // Backward command
mLeft          = 0xb0  // Turn left
mRight         = 0xb1  // Turn right
mPP            = 0xc0  // Pitch Kp
mPI            = 0xc1  // Pitch Ki
mPD            = 0xc2  // Pitch Kd
mSpdKp         = 0xc3  // Speed Kp (value/100)
mSpdKi         = 0xc4  // Speed Ki (value/100)
mSpdMaxTilt    = 0xc5  // Speed Max Tilt (raw degrees)
mSpdMaxVel     = 0xc6  // Speed Max Vel (value/10)
mAC            = 0xd0  // Angle trim (value/10)
mSD            = 0xd1  // Yaw gain (value/10)
mTelemetry     = 0xf0  // Live telemetry stream
mAutoMode      = 0xf1  // Auto-arm on/off
mTrimFine      = 0xf2  // Fine trim
mPositionHold  = 0xf3  // Position hold
mResetTrim     = 0xf4  // Reset trim
```

## Hardware

### Bill of materials

| Part | Specification |
|---|---|
| **Main controller** | Raspberry Pi 5 (Qt/C++ + WiringPi, `wiringPiSetupPhys()`) |
| **IMU** | MPU6050 — 6-axis (3-axis accel + 3-axis gyro), I²C bus 1, address `0x68`. Fused through a Kalman filter to recover pitch angle. |
| **Motor driver** | Waveshare RPi Motor Driver Board — dual H-bridge HAT, **2× NXP MC33886** (one chip per motor) |
| **Drive motors** | 2× 37 mm geared DC motor with Hall quadrature encoder, ~100 RPM @ 4.5 V / ~200 RPM @ 9 V |
| **Chassis** | 190 mm aluminium balance-bot plate, 3 mm acrylic top, 65 mm × 26 mm rubber tires on electroplated plastic hubs, ~520 g bare |

### Motor driver — Waveshare RPi Motor Driver Board

<p align="center">
  <img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/ProjectFiles/motor_driver_board.png" alt="Waveshare RPi Motor Driver Board labeled" width="520">
</p>

The driver is a HAT that sits on the Pi's 40-pin header. Each of the two **NXP MC33886** H-bridges drives one motor independently:

| Spec | Value |
|---|---|
| H-bridge IC | 2× NXP MC33886 (one per motor) |
| Power input (VIN) | 7–40 V |
| Output current (per motor) | up to 5 A |
| Onboard 5 V regulator | yes — back-feeds the Pi from the same battery |
| Onboard IR receiver | yes — **unused in this project** (BLE replaces IR) |
| Protections | 2 A resettable fuse on the Pi rail, motor-output reverse-voltage protection, supply reverse-voltage protection, plus the MC33886's own short-circuit / over-current / over-voltage / over-temperature protection |
| Logic interface (per motor) | classic *IN1 / IN2 + ENA PWM-enable* — two direction pins + one PWM enable pin |

The Pi drives the H-bridges with **8-bit software PWM** via WiringPi's `softPwm` (`PWM_LIMIT = 255`, `PWM_MIN = 8` deadband). Direction is selected by setting `IN1`/`IN2` to opposite levels; coast = both low; the PWM pin gates the output.

### Drive motors

Each motor is a 6-pin geared DC unit — two pins for the H-bridge winding and four for the Hall quadrature encoder on the rear shaft:

<p align="center">
  <img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/ProjectFiles/motor_pinout.png" alt="DC motor pin definitions" width="560">
</p>

| Motor pin | Function | Connect to |
|---|---|---|
| `M1` | Motor power input 1 | Motor-driver output A (terminal block) |
| `M2` | Motor power input 2 | Motor-driver output B (terminal block) |
| `GND` | Encoder ground | Pi GND (any GND header pin) |
| `3.3V` | Encoder logic supply (3.3 V or 5 V tolerant) | Pi `+5 V` (header pin 2 or 4) |
| `C1` | Encoder channel A | Pi `SPD_INT_*` (interrupt pin) |
| `C2` | Encoder channel B | Pi `SPD_PUL_*` (direction-sample pin) |

The Pi decodes encoders in **single-edge mode**: rising edge on channel A triggers an ISR, which immediately reads channel B to recover direction and increments or decrements a signed tick counter per wheel. All four encoder inputs use the Pi's internal pull-ups (`PUD_UP`). Tick polarity is runtime-configurable through `encoderInvertL` / `encoderInvertR` in `settings.ini`, so reversed motor wiring can be fixed in software.

### Chassis

<p align="center">
  <img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/ProjectFiles/chassis_kit.png" alt="Balance robot chassis kit" width="520">
</p>

Standard self-balancing kit: 190 mm CNC'd aluminium baseplate, motor mounting brackets, two 65 mm wheels with rubber tread, M3 hardware bag, and the encoder/motor wiring harness shown on the right. The Waveshare driver and the Pi stack on top.

#### Baseplate dimensions

The dimensioned drawing below is provided so you can design a custom PCB or laser-cut a top deck that fits the standard hole pattern. All dimensions are in millimetres; the plate is **119 mm wide × 64 mm deep** with 22× Ø3.2 mm mounting holes and rounded corners (R5 / R1.6).

<p align="center">
  <img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/ProjectFiles/chassis_dimensions.png" alt="Balance robot baseplate — dimensioned drawing" width="640">
</p>

#### 3D-printable parts (STL)

The `ProjectFiles/` folder ships with STL meshes for the printable upper structure of the robot. Slice them in your slicer of choice (Cura, PrusaSlicer, etc.). Recommended print settings: PLA, 0.2 mm layer height, 20% infill, supports off (the parts are designed to print flat without overhangs).

| File | What it is |
|---|---|
| [`Robot_chasis_bottom.stl`](ProjectFiles/Robot_chasis_bottom.stl) | Printable lower chassis plate — alternative to the aluminium baseplate if you want a fully printed build. |
| [`Robot_chasis_top.stl`](ProjectFiles/Robot_chasis_top.stl) | Upper deck that bolts onto the standoffs above the Pi + driver stack. |
| [`Robot_safe_top.stl`](ProjectFiles/Robot_safe_top.stl) | Reinforced top cover variant with extra wall thickness — use it if you expect frequent falls during PID tuning. |
| [`Robot_mask.stl`](ProjectFiles/Robot_mask.stl) | Decorative front mask / face plate. Large mesh (~27 MB), purely aesthetic. |

Also previously in `ProjectFiles/`: `Raspberry_Pi_Model_B.STL`, `Robot_chasis_bottom_with_mpu6050.stl`, `Robot_chasis_top_for_raspberry.stl`, `Robot_chasis_top_single_for_raspberry.stl`, `Robot_chasis_top_with_raspberry.stl`, `Robot_safe_top_for_raspberry.stl`, plus the FreeCAD source files `robot_safe.FCStd` and `robot_top.FCStd` if you want to modify them.

### Raspberry Pi pin reference

Pin numbering in `constants.h` follows the **40-pin physical header** (because `robotcontrol.cpp` calls `wiringPiSetupPhys()`) — *not* BCM or wiringPi-logical numbering. Use this header diagram when wiring:

<p align="center">
  <img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/ProjectFiles/raspberry_pi_pinout.png" alt="Raspberry Pi 40-pin header — physical pin numbering" width="780">
</p>

Motor-driver lines (left motor uses `PWML1`/`PWML2` for direction and `PWML` for PWM enable; right motor mirrored):

```
PWMR1 = pin 31   // Right motor IN1 (direction)
PWMR2 = pin 33   // Right motor IN2 (direction)
PWMR  = pin 32   // Right motor ENA (PWM enable, 0–255)
PWML1 = pin 38   // Left  motor IN1 (direction)
PWML2 = pin 40   // Left  motor IN2 (direction)
PWML  = pin 37   // Left  motor ENA (PWM enable, 0–255)
```

Encoders:

```
SPD_INT_L = pin 16   // Left  encoder channel A (interrupt)   ← motor C1
SPD_PUL_L = pin 12   // Left  encoder channel B (direction)   ← motor C2
SPD_INT_R = pin 18   // Right encoder channel A (interrupt)   ← motor C1
SPD_PUL_R = pin 22   // Right encoder channel B (direction)   ← motor C2
```

I²C (MPU6050): SDA on header pin 3, SCL on header pin 5 (`/dev/i2c-1`, addr `0x68`).

### How the 6 motor-drive pins control the H-bridges

Each MC33886 H-bridge needs **3 wires** from the Pi: two direction bits (`IN1` / `IN2`) and one PWM enable (`ENA`). The direction bits pick *which way* the motor spins, and the PWM duty cycle on the enable pin picks *how fast*. With two motors, the Pi spends 6 GPIOs total on motor control — exactly the 6 listed in the `MOTOR DRIVE OUTPUTS` section above.

H-bridge truth table per motor:

| IN1 | IN2 | ENA (PWM) | Result |
|-----|-----|-----------|--------|
| LOW | LOW | x | Coast (motor free-wheels) |
| HIGH | LOW | 0–255 | Forward, speed ∝ duty cycle |
| LOW | HIGH | 0–255 | Reverse, speed ∝ duty cycle |
| HIGH | HIGH | x | Brake (both motor terminals shorted) |

Pin-to-function mapping:

| Side | Pi pin | Name | Role on the H-bridge | Wire color in diagram |
|------|--------|------|----------------------|-----------------------|
| Right motor | Pin 31 | `PWMR1` | IN1 — direction bit 1 | 🔵 blue (direction) |
| Right motor | Pin 32 | `PWMR`  | ENA — PWM enable (speed) | 🟠 orange (PWM) |
| Right motor | Pin 33 | `PWMR2` | IN2 — direction bit 2 | 🔵 blue (direction) |
| Left motor  | Pin 37 | `PWML`  | ENA — PWM enable (speed) | 🟠 orange (PWM) |
| Left motor  | Pin 38 | `PWML1` | IN1 — direction bit 1 | 🔵 blue (direction) |
| Left motor  | Pin 40 | `PWML2` | IN2 — direction bit 2 | 🔵 blue (direction) |

The Pitch PID produces a signed 16-bit PWM command in the range `−255 … +255` for each wheel. `applyMotors(int pwmL, int pwmR)` in `robotcontrol.cpp` converts that signed value into the right combination of direction bits + PWM duty cycle:

```cpp
// Right motor — positive pwmR means forward
if (pwmR > 0) {
    digitalWrite(PWMR1, HIGH);   // pin 31
    digitalWrite(PWMR2, LOW);    // pin 33
    softPwmWrite (PWMR, pwmR);   // pin 32 — duty 0..255
}
else if (pwmR < 0) {             // negative → reverse
    digitalWrite(PWMR1, LOW);
    digitalWrite(PWMR2, HIGH);
    softPwmWrite (PWMR, -pwmR);
}
else {                           // zero → coast
    digitalWrite(PWMR1, LOW);
    digitalWrite(PWMR2, LOW);
    softPwmWrite (PWMR, 0);
}

// Left motor — IN1/IN2 are SWAPPED (motor is mirror-mounted)
if (pwmL > 0) {
    digitalWrite(PWML1, LOW);    // pin 38  ← note swap
    digitalWrite(PWML2, HIGH);   // pin 40  ← note swap
    softPwmWrite (PWML, pwmL);   // pin 37
}
// ...same pattern for pwmL < 0 and pwmL == 0
```

**Why the left motor's IN1/IN2 are swapped:** the two motors are bolted into the chassis facing each other (mirror-mounted), so "wheel rotates forward" corresponds to *opposite* shaft directions on the two motors. Instead of rewiring the M1/M2 power leads, the code flips the polarity in software (`// Sol motor ters monte edildiği için pin'ler ters`) so that a positive PWM on either side means "robot moves forward" at the algorithm level. The Speed PID never has to know that the motors are mirror-mounted.

**Why these specific pin numbers (31, 32, 33, 37, 38, 40 — with gaps):** the gaps (34, 35, 36, 39) are header pins reserved for GND or for GPIOs used elsewhere in the project, so the firmware picks the closest run of physical pins that are all free for general output. Because `wiringPiSetupPhys()` is used, the numbers in `constants.h` are exactly what's printed on a Pi pinout diagram — no BCM ↔ wiringPi translation needed.

The whole 6-pin block is rewritten once per Pitch PID tick (~1 kHz), and **this is where the inner balance loop physically closes** — every other layer (Speed PID, joystick, BLE) ultimately just changes the `pwmL` / `pwmR` numbers that land here.

### Wiring diagram

End-to-end map of every connection that `constants.h` and `robotcontrol.cpp` actually drive. Wire colors: **red** +5 V, **black** GND, **purple** I²C, **blue** H-bridge direction, **orange** PWM enable, **green** encoder A/B.

<p align="center">
  <img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/ProjectFiles/wiring_diagram.png" alt="Balance Robot wiring diagram — Pi 40-pin header to motor driver to motors" width="1000">
</p>

A few code-side details worth knowing when wiring:

- `applyMotors()` in `robotcontrol.cpp` **deliberately swaps IN1/IN2 polarity for the left motor** (`// Sol motor ters monte edildiği için pin'ler ters`), because the left motor is bolted in mirrored. Positive `pwmR` drives `PWMR1` high; positive `pwmL` drives `PWML2` high.
- If a motor spins backwards, prefer flipping `encoderInvertL` / `encoderInvertR` in `settings.ini` over rewiring — the encoder direction must match the motor direction for the Speed PID to work.
- The motor PCB's encoder rail is labelled `3.3V` but accepts 5 V (the Hall output pulls up to whatever you supply). Feeding it from the Pi's 5 V keeps the encoder signal strong, and the Pi's 3.3 V GPIOs still read it cleanly thanks to the internal pull-ups.

## Installation

### Pi 5

The Pi-side app links against Qt5 (Core / Bluetooth / Network / Multimedia), libi2c, ALSA + FLAC for audio capture, and WiringPi for GPIO + software PWM. WiringPi is no longer in the Debian / Raspberry Pi OS repositories, so it must be built from the maintained fork.

```bash
# --- 1. System update ---
sudo apt-get update

# --- 2. Qt5 ---
sudo apt-get install qtmultimedia5-dev \
                     libqt5multimedia5-plugins \
                     libqt5bluetooth5 libqt5bluetooth5-bin \
                     qtconnectivity5-dev

# --- 3. I²C (MPU6050) ---
sudo apt-get install libi2c-dev

# --- 4. Audio (ALSA / PulseAudio / FLAC, used by the on-board speech features) ---
sudo apt-get install libasound2-dev \
                     sox libsox-fmt-all \
                     pulseaudio alsa-tools \
                     libflac-dev

# --- 5. WiringPi — must be built from source (no longer in apt) ---
git clone https://github.com/WiringPi/WiringPi.git
cd WiringPi
sudo ./build
cd ..

# --- 6. Enable I²C on the Pi ---
sudo raspi-config       # Interface Options → I2C → Enable, then reboot

# --- 7. Build the application ---
cd ~/BalanceRobotPI
qmake BalanceRobotPI.pro
make -j4

# --- 8. Run (sudo is required: WiringPi needs /dev/mem and the BLE GATT
#         server needs to register on the system Bluetooth bus) ---
sudo ./BalanceRobotPI
```

After the first run, parameters tuned from the iOS app are persisted to `settings.ini` next to the binary — keep an eye on it the first time you tune the PIDs.

### iOS

1. Extract `BalanceRobotRemote_IOS.zip`
2. Open `RobotControlBLE.xcodeproj` in Xcode
3. Update the bundle identifier with your developer account
4. Build & Run to the device

### Auto-start on boot (Pi systemd)

`/lib/systemd/system/balancerobot.service`:

```ini
[Unit]
Description=Balance Robot Service
After=multi-user.target

[Service]
Type=idle
WorkingDirectory=/home/pi/BalanceRobotPI
ExecStart=/home/pi/BalanceRobotPI/BalanceRobotPI

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable balancerobot.service
sudo systemctl start balancerobot.service
```

## File layout

```
BalanceRobotPI/                 # Pi side (Qt/C++)
├── main.cpp
├── balancerobot.cpp/.h         # BLE command handler
├── robotcontrol.cpp/.h         # Main control loop (PID, ISRs)
├── pid.cpp/.h                  # Pitch PID
├── mpu6050.cpp/.h              # IMU driver
├── message.h                   # BLE protocol message IDs
├── kalmanfilter.cpp/.h         # Angle fusion
├── gattserver.cpp/.h           # BLE GATT server
└── settings.ini                # Runtime-persisted parameters

BalanceRobotRemote_IOS/         # iOS side (Swift)
└── RobotControlBLE/
    ├── AppDelegate.swift
    ├── ControlViewController.swift   # Main screen (joystick + telemetry)
    ├── SettingsViewController.swift  # Settings tab (PID sliders)
    ├── JoystickView.swift            # Joystick widget
    ├── MessageService.swift          # BLE message IDs
    ├── AppSettings.swift             # UserDefaults wrapper
    └── Ble/
        └── BluetoothService.swift    # CoreBluetooth wrapper
```

## Runtime flow

1. Pi boots → `settings.ini` is loaded → BLE starts advertising
2. iOS connects → Settings tab requests current parameters from Pi
3. Robot held upright → IMU detects upright pose → if `Auto-arm` is enabled, it automatically ARMs
4. Pitch PID begins balancing; Speed PID requests a tilt based on joystick input
5. If fall is detected (>40°) → DISARM, motors stop
6. When held upright again, it auto-restarts

## Known limits

- At low battery voltage, PWM saturates and balancing degrades
- On slippery floors, wheel slip causes encoder velocity to lie
- Very fast maneuvers can saturate the IMU rate gyro

## License

MIT.

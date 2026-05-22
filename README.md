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

- **Raspberry Pi 5** — main controller
- **MPU6050** — IMU (I2C, address 0x68)
- **L298N / RPi Motor Driver** — motor driver
- **2× DC motor + quadrature encoder** — drive train

### GPIO pins (wiringPi numbering)

```
SPD_INT_L = 16   // Left encoder INT
SPD_PUL_L = 12   // Left encoder PUL
SPD_INT_R = 18   // Right encoder INT
SPD_PUL_R = 22   // Right encoder PUL
```

## Installation

### Pi 5

```bash
# Dependencies
sudo apt install qt5-default libqt5bluetooth5 libi2c-dev wiringpi

# Build
cd ~/BalanceRobotPI
make clean && make

# Run
sudo ./BalanceRobotPI
```

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

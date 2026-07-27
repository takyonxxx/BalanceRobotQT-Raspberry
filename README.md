# Balance Robot — Pi 5 + iOS Remote (BLE)

A two-wheeled self-balancing robot. Qt/C++ control application on Raspberry Pi 5, with an iOS BLE remote control featuring a **PID auto-tune (learning) mode** and a **Claude voice assistant** (speak to the robot, Claude drives it).

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

## PID auto-tune — learning mode

Instead of tuning the Pitch PID by hand, the robot can tune itself. The Pi runs a **Twiddle (coordinate-descent) auto-tuner** that evaluates candidate `[Kp, Kd, Ki]` sets live while the robot balances, and commits the best set to `settings.ini` when done.

**How it works:**

1. Each candidate is evaluated for ~9 s (2 s settle + 7 s measure).
2. During measurement the tuner injects small **virtual push pulses (±1.5°)** into the target angle — the cost function scores exactly the failure mode that matters in practice: recovery from a forward/backward command step.
3. Cost = angle-error² + gyro-rate² + PWM² (weighted) → tracking accuracy, oscillation and motor chatter are all penalized together.
4. Twiddle shrinks/grows the search deltas per gain; the run converges typically in 3–6 minutes (hard cap: 40 evaluations).

**Safety:** a fall during an evaluation gives that candidate infinite cost and instantly reverts to the best known gains. Three falls abort the whole run (best-so-far is kept). Touching the joystick restarts the current candidate's measurement — learning is never corrupted, just extended.

**How to start it:**

- iOS **Settings tab → "PID LEARNING (AUTO-TUNE)" card → Start**. Live status lines from the Pi (baseline cost, per-evaluation results, best gains found) stream into the card.
- Or by voice, from the **Claude tab**: *"PID öğrenmeyi başlat"* / *"start PID learning"*.

**Ground rules:** run it on a hard, flat floor with ~1 m of clear space around the robot — it wobbles on purpose. The robot must already be able to balance on its own before you start. Only the Pitch PID (inner loop) is optimized; Speed PID stays on the sliders.

## Claude voice assistant (iOS)

The third tab, **Claude**, turns the phone into a voice interface for the robot. Tap the mic, speak (Turkish or English), and Claude answers out loud — and can actually drive the robot through tool calls.

```
Mic ──> SFSpeechRecognizer (tr-TR) ──> Anthropic Messages API ──> tool calls ──> BLE commands
                                              │
                                              └──> reply text ──> AVSpeechSynthesizer (spoken)
```

**Tools Claude can use:**

| Tool | What it does |
|---|---|
| `move_robot` | forward / backward / left / right, speed 10–100 %, duration 0.3–5 s (auto-stops when the duration ends) |
| `stop_robot` | zero all motion commands immediately |
| `set_armed` | ARM / DISARM |
| `start_pid_learning` / `stop_pid_learning` | PID auto-tune control |
| `get_robot_status` | reads live telemetry (angle, PWM, armed / fallen / learning flags) |
| `reset_trim` | zero the trim integrator |

Example voice commands: *"ileri git"*, *"iki saniye sağa dön"*, *"dur"*, *"PID öğrenmeyi başlat"*, *"robot nasıl, denge durumu ne?"*, *"motorları kapat"*.

**Setup — API key only, no username/password:**

**Free offline mode (no API, no cost):** if no API key is entered, the Claude tab automatically falls back to an **on-device keyword parser** (`LocalCommandParser.swift`). All robot voice commands — move, stop, arm/disarm, PID learning, status, trim — work **completely free and offline** (speech recognition itself is Apple's, also free). What you lose without an API key is only the conversational part: free-form questions, multi-step reasoning, and natural phrasing ("take it easy and go a bit forward"). Commands must roughly match the keywords (*ileri, geri, sola, sağa, dur, durum, PID öğrenmeyi başlat, trim sıfırla, motorları kapat* — with optional "*iki saniye*" duration and "*hızlı/yavaş/%60*" speed modifiers).

If you do want full conversational Claude: the assistant talks directly to the Anthropic API. There is **no login, no username, no password anywhere** — the only credential is an **API key** (pay-per-use, separate from any Claude subscription):

1. Create a key at [console.anthropic.com](https://console.anthropic.com) → **API Keys** (the key starts with `sk-ant-`). This is tied to your Anthropic account and billing; the app itself never asks you to sign in.
2. In the app, open the **Claude** tab and tap the **key (🔑) button**, paste the key. It is stored on-device in `UserDefaults` and sent only in the `x-api-key` header of API requests.
3. On first mic use, iOS will ask for **Microphone** and **Speech Recognition** permissions — both must be granted (usage descriptions are already in `Info.plist`).

Default model is `claude-sonnet-4-6` (`AppSettings.claudeModel`). The **speak replies** switch on the tab toggles text-to-speech; typing in the text field works as an alternative to the mic.

**Free cloud alternative — Google Gemini (free quota):** full conversational assistant without paying, using Gemini's free API tier:

1. Go to [aistudio.google.com/apikey](https://aistudio.google.com/apikey), sign in with any Google account and create an API key (starts with `AIza`). **No credit card required** — the free tier gives a daily request quota that is plenty for hobby use.
2. In the app, tap the **key (🔑) button** on the Claude tab → **"Gemini anahtarı gir (ücretsiz)"**, paste the key.
3. Done — the same voice pipeline, tools and robot commands now run through Gemini (`GeminiService.swift`, default model `gemini-2.0-flash`).

Provider priority is automatic: **Claude key → Gemini key → offline parser**. If both keys are set, Claude is used; delete keys from the same 🔑 menu to fall back. If the Gemini free quota runs out for the day (HTTP 429), the app tells you to wait — or just use the offline command mode, which never runs out.

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
mData          = 0xe1  // Pi → Phone IP / diagnostics text
mTelemetry     = 0xf0  // Live telemetry stream
mAutoMode      = 0xf1  // Auto-arm on/off
mTrimFine      = 0xf2  // Fine trim
mPositionHold  = 0xf3  // Position hold
mResetTrim     = 0xf4  // Reset trim
mPidLearn      = 0xf5  // PID auto-tune: write 1=start 0=stop, read=state
mPidStatus     = 0xf6  // Pi -> Phone: PID learn status text (UTF-8)
```

> `0xe0` is reserved (formerly `mSpeak` / on-board TTS, removed in the cleanup pass).

Telemetry flags byte (packet byte 12): bit0 = armed, bit1 = fallen, bit2 = auto-mode, bit3 = position-hold, **bit4 = PID learning active**.

## Voice assistant on the Pi — phone-free operation

The robot can also listen and act **entirely on its own**: a USB microphone on the Pi, offline Turkish speech recognition, and the same command/LLM pipeline as the iOS app — working even when the mobile app is closed. Main control lives on the Pi; the phone becomes optional.

```
USB mic ──> arecord ──> Vosk (offline TR STT) ──> intent router
                                                     │
                                     ┌───────────────┴───────────────┐
                                     │ command?                       │ question?
                                     ▼                                ▼
                          local keyword parser              Gemini (free) / Claude API
                          (ileri/geri/dur/PID/durum)        with the same robot tools
                                     │                                │
                                     └────────────┬───────────────────┘
                                                  ▼
                                   RobotControl (atomic, thread-safe)
                                                  +
                                   Piper / espeak-ng TTS ──> speaker
```

**How commands vs questions are separated:** every recognized utterance first goes through the local keyword parser (same vocabulary as the iOS offline mode). If it matches a robot command it executes immediately — offline, free, low latency. Anything that doesn't match is treated as a question and forwarded to the LLM (Gemini free tier or Claude, whichever key is set in `settings.ini`), which can still drive the robot through the same 7 tools. No key set → commands still work, questions get a spoken "komutları söyle" hint.

**Wake word:** by default the robot only reacts to utterances containing "robot" ("robot ileri git"). After the wake word it keeps listening for 10 s so follow-ups don't need it. Set `wakeWord=` (empty) in `settings.ini` to react to everything.

**Language — speak TURKISH to the Pi:** the bundled speech model (`vosk-model-small-tr`) understands **Turkish only**. English keywords exist in the parser for completeness, but the Turkish acoustic model will not transcribe spoken English reliably — so on the Pi, use Turkish. (The iOS app is different: Apple's recognizer handles both TR and EN.) To make the Pi English-speaking instead, download `vosk-model-small-en-us-0.15` and point `voskModelPath` at it.

**Voice command reference (Pi) — complete list.** Say the wake word first — *"robot …"*. After any command a 10-second attention window opens: follow-ups don't need the wake word. Saying just *"robot"* gets *"Evet, dinliyorum"* and opens the window. Word order inside the sentence doesn't matter.

| Amaç | Tetikleyici kelimeler (herhangi biri yeter) | Örnek |
|---|---|---|
| **İleri** | ileri, öne, forward | *"robot ileri git"* |
| **Geri** | geri, arkaya, back | *"robot geri git"* |
| **Sola dön** | sol, sola, left (tam kelime) | *"robot sola dön"* (varsayılan %50) |
| **Sağa dön** | sağ, sağa, right (tam kelime) | *"robot sağa dön"* (varsayılan %50) |
| **Dur** | dur, stop, kes, bekle | *"dur"* (pencere içinde "robot" gerekmez) |
| **Durum oku** | durum, nasıl, status, telemetri, açı, denge | *"robot durum ne"*, *"robot nasılsın"* |
| **PID öğrenme başlat** | pid/öğren/learn + başlat, başla, start, aç | *"robot PID öğrenmeyi başlat"* |
| **PID öğrenme durdur** | pid/öğren/learn + durdur, bitir, iptal, stop, kapat | *"robot PID öğrenmeyi durdur"* |
| **Trim sıfırla** | trim + sıfırla, resetle, reset, temizle | *"robot trim sıfırla"* |
| **Motorları kapat (disarm)** | disarm, motorları kapat, motoru kapat, devre dışı | *"robot motorları kapat"* |
| **Motorları aç (arm)** | arm, hazırlan, dengele, motorları aç, motoru aç | *"robot motorları aç"* |
| **Serbest soru** (API anahtarı gerekir) | yukarıdakilerle eşleşmeyen her şey | *"robot neden devriliyorsun?"* |

**Hareket komutlarına eklenebilen niteleyiciler** (ileri/geri/sola/sağa ile birlikte, sırası önemsiz):

| Niteleyici | Söyleyiş | Etki |
|---|---|---|
| Hız — kelimeyle | yavaş / nazik → %30 · hızlı → %75 · çok hızlı → %90 · tam gaz / full → %100 | *"robot yavaş ileri git"* |
| Hız — yüzdeyle | "yüzde" + sayı (on…doksan, yüz veya rakam), %10–100 | *"robot yüzde altmış ileri git"* |
| Süre | sayı + "saniye" (yarım, buçuk, bir…on veya rakam), 0.3–8 sn | *"robot üç saniye geri git"* |
| Birleşik | hız + süre aynı cümlede | *"robot yüzde kırk iki saniye sola dön"* |

Varsayılanlar (niteleyici söylenmezse): ileri/geri **%100**, dönüşler **%50**, süre **1.5 saniye** (`settings.ini` → `moveDefaultPct` / `turnDefaultPct` / `moveDefaultSecs`). Hareket süre sonunda otomatik durur; *"dur"* her an keser.

The rows above are matched **offline by the local parser** — free, instant, no internet. Only free-form questions need an API key.

**New files:** `voiceassistant.cpp/.h` (mic capture, Vosk STT via `dlopen`, intent routing, TTS queue), `llmclient.cpp/.h` (Gemini + Claude REST clients with the tool loop, Qt-native). `libvosk` is loaded at **runtime** — it is *not* a build dependency, so the firmware builds and runs unchanged if voice components aren't installed.

### Setup

```bash
# 1. Audio tools + TTS
sudo apt-get install alsa-utils espeak-ng

# 2. Vosk library (prebuilt aarch64, ~2 MB)
cd /tmp
wget https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-linux-aarch64-0.3.45.zip
unzip vosk-linux-aarch64-0.3.45.zip
sudo cp vosk-linux-aarch64-0.3.45/libvosk.so /usr/local/lib/
sudo ldconfig

# 3. Turkish speech model (~45 MB, runs real-time on one Pi 5 core)
cd ~/BalanceRobotPI
wget https://alphacephei.com/vosk/models/vosk-model-small-tr-0.3.zip
unzip vosk-model-small-tr-0.3.zip

# 4. Microphone: the WEBCAM'S BUILT-IN MIC works - no separate mic needed.
#    The camera streaming service only holds the VIDEO device (v4l2), so the
#    same webcam's audio capture is free to use concurrently.
#    micDevice=auto scans `arecord -l` and picks the USB/webcam mic itself;
#    to check manually:
arecord -l          # e.g. "card 1: Device [USB Audio Device], device 0" -> plughw:1,0
arecord -D plughw:1,0 -f S16_LE -r 16000 -c 1 -d 3 test.wav && aplay test.wav   # 3 s mic test

# 5. settings.ini ([assistant] section, created on first run - voice is
#    ENABLED BY DEFAULT; set enabled=false to turn it off):
#    enabled=true
#    micDevice=auto              ; or plughw:1,0 to pin a specific card
#    voskModelPath=/home/pi/BalanceRobotPI/vosk-model-small-tr-0.3
#    wakeWord=robot
#    geminiApiKey=AIza...        ; optional - free quota, for questions
#    claudeApiKey=               ; optional - takes priority if set
#    piperModel=                 ; optional - path to a Piper .onnx voice for natural TTS
#    moveDefaultPct=100          ; voice move speed %% when not specified (10-100)
#    moveDefaultSecs=1.5         ; voice move duration when not specified (0.5-8 s)
#    turnDefaultPct=50           ; default turn speed %% (100%% turns tip the robot)
#    voiceMaxVel=6.0             ; TEMP speed ceiling for voice fwd/back moves
#                                ; (ticks/loop; joystick keeps its own spdMaxVel;
#                                ;  released when the move stops; 0=disable)
#
# Feeling sluggish even at 100%? The speed cascade is deliberately gentle.
# Three knobs (root section of settings.ini, shared with the joystick):
#    spdTiltSlew=0.04   ; accel ramp, deg/loop (0.02=4°/s gentle, 0.06=12°/s brisk)
#    spdMaxVel=3.0      ; top speed target, encoder ticks/loop - try 4.0-5.0
#    spdKi=0.20         ; speed integral - try 0.30 for faster spool-up
# Raise ONE at a time and retest; too much of any can reintroduce tip-overs.

# 6. Restart the app; look for:  VoiceAssistant: listening on plughw:1,0 (wake word: "robot")
```

**Better TTS (optional):** `espeak-ng` is robotic. For a natural Turkish voice install [Piper](https://github.com/rhasspy/piper) and point `piperModel` at a `tr_TR` `.onnx` voice file; the assistant automatically prefers it and falls back to espeak-ng.

**CPU budget:** the assistant thread runs at low priority; Vosk small uses well under one core. Combined with the camera stream, prefer 640×480 video or disable the camera during PID auto-tune — same guidance as before, the 200 Hz balance loop always has priority.

**Coexistence with the app:** BLE and voice use the same atomic `RobotControl` interface, so both can be active at once. Voice `move` commands auto-stop after their duration; touching the phone joystick simply overrides them.

## Hardware

### Bill of materials

| Part | Specification |
|---|---|
| **Main controller** | Raspberry Pi 5 (Qt/C++ + WiringPi, `wiringPiSetupPhys()`) |
| **IMU** | MPU6050 — 6-axis (3-axis accel + 3-axis gyro), I²C bus 1, address `0x68`. Fused through a Kalman filter to recover pitch angle. |
| **Motor driver** | Waveshare RPi Motor Driver Board — dual H-bridge HAT, **2× NXP MC33886** (one chip per motor) |
| **Drive motors** | 2× 37 mm geared DC motor with Hall quadrature encoder, ~100 RPM @ 4.5 V / ~200 RPM @ 9 V |
| **Battery** | 3S LiPo — 11.1 V nominal (12.6 V full, 9.9 V suggested cut-off), 4000 mAh, 20C+ discharge |
| **Camera (optional)** | Generic UVC USB webcam — MJPEG 1280×720 @ 30 fps, streamed as RTSP H.264 via GStreamer + MediaMTX |
| **Chassis** | 190 mm aluminium balance-bot plate, 3 mm acrylic top, 65 mm × 26 mm rubber tires on electroplated plastic hubs, ~520 g bare |

### Motor driver — Waveshare RPi Motor Driver Board

<p align="center">
  <img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/ProjectFiles/motor_driver_board.png" alt="Waveshare RPi Motor Driver Board — labeled callouts" width="520">
</p>

**Callouts on the photo above:**

| # | Part | Role |
|---|---|---|
| **1** | Raspberry Pi GPIO interface (40-pin header) | Mates with the Pi's header — this is how the six motor-control lines (`PWMR1/PWMR/PWMR2/PWML1/PWML/PWML2`) reach the driver. |
| **2** | Motor screw terminals (M1 A/B, M2 A/B) | Where the two DC motors connect. M1 = right motor, M2 = left motor in this project. |
| **3** | External power input (VIN, GND) | The 3S LiPo (11.1 V) wires land here. 7–40 V accepted. |
| **4** | 74LVC4245AD level-shifter | Voltage translator between the Pi's 3.3 V GPIOs and the H-bridge logic. Acts as a buffer too — protects the Pi. |
| **5** | MC33886 H-bridge × 2 | The actual motor drivers. One chip per motor, up to 5 A each. |
| **6** | LM2596-5.0 5 V regulator | Steps the battery voltage down to 5 V for the Pi. Lets a single battery power the whole robot. |
| **7** | Power indicator LED | Lights when VIN is present and the 5 V rail is healthy. |
| **8** | **Pi power-source switch (OFF / ON)** | Selects who powers the Pi. **OFF** = Pi powered externally (USB-C, the driver is *not* feeding the Pi). **ON** = the driver's 5 V regulator feeds the Pi through the 40-pin header. **For this project, set it to ON** so a single LiPo pack runs both. |
| **9** | 2 A self-recovery (polyfuse) | Resettable fuse on the Pi's 5 V rail. If something shorts, it opens; after it cools down it conducts again. |
| **10** | IR receiver | Onboard infrared receiver. **Unused in this project** — the iOS app talks to the Pi over BLE instead. |
| **11** | Schottky diodes | Protect each H-bridge output from reverse-voltage spikes when the motor's inductive load is switched off. |
| **12** | Supply anti-reverse diode | Protects the whole board if you wire the battery backwards. Don't rely on it as a habit, but it has saved many builds. |

The driver is a HAT that sits on the Pi's 40-pin header. Each of the two **NXP MC33886** H-bridges (#5) drives one motor independently:

| Spec | Value |
|---|---|
| H-bridge IC | 2× NXP MC33886 (one per motor) |
| Power input (VIN) | 7–40 V |
| Output current (per motor) | up to 5 A |
| Onboard 5 V regulator | LM2596-5.0 (#6) — back-feeds the Pi from the same battery when switch #8 is ON |
| Onboard IR receiver | yes (#10) — **unused in this project** (BLE replaces IR) |
| Protections | 2 A resettable fuse on the Pi rail (#9), motor-output reverse-voltage protection (#11), supply reverse-voltage protection (#12), plus the MC33886's own short-circuit / over-current / over-voltage / over-temperature protection |
| Logic interface (per motor) | classic *IN1 / IN2 + ENA PWM-enable* — two direction pins + one PWM enable pin |

> **Before first power-up:** make sure switch **#8 is set to ON** so the driver feeds the Pi. If it's OFF and you've removed the USB-C cable, the Pi simply won't boot — a confusing first symptom when nothing else is wrong.

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

### IMU mounting (MPU6050)

The MPU6050 is the single most physically-sensitive component on the robot — where and how you mount it directly affects whether the balance loop can do its job.

<p align="center">
  <img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/ProjectFiles/imu_mounting.png" alt="MPU6050 mounting — top-down view of the lower chassis with axes labelled" width="780">
</p>

**Position:** the IMU sits on the **lower chassis plate, centred between the two motors** — i.e. right on the wheel axis line. This is the rotation axis of the robot, so the IMU sees pure pitch when the chassis tilts and isn't contaminated by extra rotational acceleration. Mounting it high up (e.g. next to the Pi on the top deck) makes the gyro pick up swing artefacts and the Kalman filter has to fight that.

**Orientation:** the MPU6050 breakout is mounted **flat (horizontal)**, with the chip and components facing **upwards**. Looking at the robot from the front, the pin header (`VCC / GND / SCL / SDA / XDA / XCL / ADO / INT`) is on the **left edge** of the breakout. This orientation puts:

- IMU **Z axis** → up (against gravity) — accelerometer reads ≈ +1 g on Z when level
- IMU **X axis** → forward (along the direction of travel) — this is the *pitch* axis the Kalman filter reads
- IMU **Y axis** → sideways (roll axis, unused by the balance loop)

If you mount the breakout in a different orientation, you'll need to either remap axes in software or live with `accelAngle` reading garbage. The Kalman code in `robotcontrol.cpp` assumes this exact orientation — easiest to just copy it.

**Wiring:** four wires up to the Pi's 40-pin header — `VCC → pin 2 (+5V)`, `GND → pin 6`, `SDA → pin 3`, `SCL → pin 5`. The breakout's onboard 3.3 V regulator + level shifter accept 5 V on VCC, so feeding from the Pi's 5 V rail is fine and gives a stronger signal than 3.3 V.

**Mechanical tip:** double-sided foam tape works well and adds a tiny bit of mechanical damping (motor vibration → IMU noise). Hot glue is fine too. Don't bolt it rigidly to the chassis with metal standoffs — the gyro picks up every motor whine that way.

### Chassis

<p align="center">
  <img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/ProjectFiles/chassis_kit.png" alt="Balance robot chassis kit" width="520">
</p>

Standard self-balancing kit: 190 mm CNC'd aluminium baseplate, motor mounting brackets, two 65 mm wheels with rubber tread, M3 hardware bag, and the encoder/motor wiring harness shown on the right. The Waveshare driver and the Pi stack on top.

**Two-deck layout.** In a finished build the chassis ends up as two stacked plates joined by **brass M3 standoffs** (typically 40–60 mm tall):

- **Lower plate** — motors + brackets at each end, MPU6050 in the centre (between the wheel axes), LiPo battery, power switch, XT60 connector
- **Upper plate** — Pi 5 with the Waveshare driver HAT mounted on top, plus an optional USB camera

Encoder and motor wires route up from the lower plate through the standoff gaps to the Pi/driver above. The two-deck arrangement keeps the heavy battery low (good for the centre of mass) while putting the brains, status LEDs and camera up top where they're accessible.

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

### Power system

The robot runs on a single **3S LiPo battery — 11.1 V nominal**. The Waveshare driver's `VIN` input accepts 7–40 V, so 3S is comfortably within range. A single pack powers everything:

```
3S LiPo (11.1 V) ──> Power switch ──> Driver VIN ──> MC33886 H-bridges ──> motors (full 11.1 V)
                                          │
                                          └──> Onboard 5 V regulator ──> Raspberry Pi 5 (via the HAT)
                                                                          │
                                                                          └──> +5 V rail ──> MPU6050 + encoder PCBs
```

| Spec | Value |
|---|---|
| Battery | 3S LiPo — 3 cells in series |
| Nominal voltage | 11.1 V (3 × 3.7 V) |
| Fully charged | 12.6 V (3 × 4.20 V) |
| Suggested cut-off | ~9.9 V (3 × 3.30 V) — below this, balancing degrades and cells age fast |
| Capacity | 4000 mAh (≈60–90 min runtime depending on motor load) |
| Discharge rating | 20C or higher recommended — peak current can hit 10 A momentarily during a hard correction |
| Connector | XT60 or XT30 to the driver's VIN terminal block (XT60 preferred for >20 A continuous) |
| Inline switch | A latching power switch is wired between the battery's + lead and the driver's VIN. Lets you cut power without unplugging the LiPo every time. |

**Safety notes:**

- **LiPo packs are fire hazards if mistreated.** Always charge in a LiPo-safe bag, never leave a charging pack unattended, and never discharge any cell below 3.0 V.
- A small **inline voltage alarm** that buzzes below 3.3 V/cell is cheap insurance — the Pi has no battery monitoring on its own.
- **Polarity matters.** The driver has reverse-voltage protection on the supply input, but the protection has its limits; double-check polarity before plugging in a freshly-charged pack.
- When the pack voltage drops below ~9.5 V, you'll notice the robot can no longer balance under hard corrections (PWM saturates). That's your cue to land it and swap packs.

### Onboard camera (optional)

The robot can carry a USB webcam that publishes a live RTSP stream — useful for FPV-style remote driving or for tuning the PIDs from a distance. The stream is **independent of the balance control loop** (it runs in its own systemd service) so you can disable it at any time without touching the balancing code.

**Stack:**

```
USB UVC webcam ──> /dev/video0 ──> GStreamer (decode → H.264 encode) ──> MediaMTX ──> RTSP clients
                                                                            (8554/tcp)
```

| Component | Role |
|---|---|
| Any generic UVC webcam | Source. Tested with a "GENERAL WEBCAM" branded MJPEG 1080p USB cam on `/dev/video0`. |
| **GStreamer** (`gst-launch-1.0`) | Pulls MJPEG frames from the camera, decodes to raw, re-encodes to H.264 with `x264enc`, pushes to MediaMTX over `rtspclientsink`. Runs as `webcam-stream.service`. |
| **MediaMTX** | Lightweight RTSP/RTMP/HLS server. Receives the push on `rtsp://localhost:8554/webcam` and re-serves it on the network. Runs as `mediamtx.service`. |

**Stream URL once running:**

```
rtsp://<pi-ip>:8554/webcam
```

**Performance tuning — why 720p, not 1080p:** software H.264 encoding at 1080p30 uses 2–3 cores on the Pi 5 and competes with the Pitch PID loop, which can introduce balance jitter. The recommended pipeline uses `1280×720 @ 30 fps`, `bitrate=1000 kbps`, `speed-preset=ultrafast`, `threads=2` — this keeps two cores free for `BalanceRobotPI` and stays under ~120% total CPU for the camera service. If you don't need the camera, just `sudo systemctl disable --now webcam-stream mediamtx` and reclaim the CPU.

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

> **Note.** In older versions of `constants.h`, the inline comments next to the encoder `#define`s (e.g. `//interrupt R Phys:16`, `//Phys:22`) contradicted the symbolic names and were leftover copy-paste artefacts. The cleaned-up `constants.h` shipped with this project no longer has them — the pin numbers themselves were always correct and consistent end-to-end (`SPD_INT_L = 16` → triggers `encLeftISR` → increments `encLeftTicks_` → drives `encL_eff` → feeds the left-wheel PID).

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

**Why the left motor's IN1/IN2 are swapped:** the two motors are bolted into the chassis facing each other (mirror-mounted), so "wheel rotates forward" corresponds to *opposite* shaft directions on the two motors. Instead of rewiring the M1/M2 power leads, the code flips the polarity in software so that a positive PWM on either side means "robot moves forward" at the algorithm level. The Speed PID never has to know that the motors are mirror-mounted.

**Why these specific pin numbers (31, 32, 33, 37, 38, 40 — with gaps):** the gaps (34, 35, 36, 39) are header pins reserved for GND or for GPIOs used elsewhere in the project, so the firmware picks the closest run of physical pins that are all free for general output. Because `wiringPiSetupPhys()` is used, the numbers in `constants.h` are exactly what's printed on a Pi pinout diagram — no BCM ↔ wiringPi translation needed.

The whole 6-pin block is rewritten once per Pitch PID tick (~1 kHz), and **this is where the inner balance loop physically closes** — every other layer (Speed PID, joystick, BLE) ultimately just changes the `pwmL` / `pwmR` numbers that land here.

### Wiring diagram

End-to-end map of every connection that `constants.h` and `robotcontrol.cpp` actually drive. Wire colors: **red** +5 V, **black** GND, **purple** I²C, **blue** H-bridge direction, **orange** PWM enable, **green** encoder A/B.

<p align="center">
  <img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/ProjectFiles/wiring_diagram.png" alt="Balance Robot wiring diagram — Pi 40-pin header to motor driver to motors" width="1000">
</p>

A few code-side details worth knowing when wiring:

- `applyMotors()` in `robotcontrol.cpp` **deliberately swaps IN1/IN2 polarity for the left motor**, because the left motor is bolted in mirrored. Positive `pwmR` drives `PWMR1` high; positive `pwmL` drives `PWML2` high.
- If a motor spins backwards, prefer flipping `encoderInvertL` / `encoderInvertR` in `settings.ini` over rewiring — the encoder direction must match the motor direction for the Speed PID to work.
- The motor PCB's encoder rail is labelled `3.3V` but accepts 5 V (the Hall output pulls up to whatever you supply). Feeding it from the Pi's 5 V keeps the encoder signal strong, and the Pi's 3.3 V GPIOs still read it cleanly thanks to the internal pull-ups.

## Installation

### Pi 5

The Pi-side app links against Qt6 (Core / Bluetooth / Network), libi2c, and WiringPi for GPIO + software PWM. Raspberry Pi OS Bookworm (the current Pi 5 default) ships Qt6 in apt — use `qmake6` to build. WiringPi is no longer in the Debian / Raspberry Pi OS repositories, so it must be built from the maintained fork.

```bash
# --- 1. System update ---
sudo apt-get update

# --- 2. Qt6 (Bluetooth + Network are the only required modules) ---
sudo apt-get install qmake6 \
                     qt6-base-dev \
                     qt6-connectivity-dev \
                     libqt6bluetooth6 \
                     libqt6network6

# --- 3. I²C (MPU6050) ---
sudo apt-get install libi2c-dev

# --- 4. WiringPi — must be built from source (no longer in apt) ---
git clone https://github.com/WiringPi/WiringPi.git
cd WiringPi
sudo ./build
cd ..

# --- 5. Enable I²C on the Pi ---
sudo raspi-config       # Interface Options → I2C → Enable, then reboot

# --- 6. Copy the project to the Pi (from your dev machine) ---
# On Windows: note the colon ":" — without it scp copies to a local folder!
#   scp -r BalanceRobotPI pi@<pi-ip>:/home/pi/
# On macOS / Linux: same syntax
#   scp -r BalanceRobotPI pi@<pi-ip>:~/

# --- 7. Build the application (on the Pi) ---
cd ~/BalanceRobotPI
qmake6 BalanceRobotPI.pro
make -j4

# --- 8. Run (sudo is required: WiringPi needs /dev/mem and the BLE GATT
#         server needs to register on the system Bluetooth bus) ---
sudo ./BalanceRobotPI
```

After the first run, parameters tuned from the iOS app are persisted to `settings.ini` next to the binary — keep an eye on it the first time you tune the PIDs.

> **If you're on an older Pi OS with only Qt5**, replace step 2 with
> `sudo apt-get install qtconnectivity5-dev libqt5bluetooth5 libqt5bluetooth5-bin`,
> use `qmake` (not `qmake6`) in step 7, and Qt's classes will resolve to Qt5 headers automatically.

### iOS

1. Extract `BalanceRobotRemote_IOS.zip`
2. Open `RobotControlBLE.xcodeproj` in Xcode
3. Update the bundle identifier with your developer account
4. Build & Run to the device
5. (Optional) For full conversational Claude: get an API key from [console.anthropic.com](https://console.anthropic.com), enter it via the key button on the **Claude** tab, and grant the microphone + speech recognition permission prompts on first use. No username/password is needed anywhere — the API key is the only credential. **Without a key, voice robot commands still work for free** via the built-in offline parser — and for free conversational AI, a Gemini key from [aistudio.google.com/apikey](https://aistudio.google.com/apikey) works too (free quota, no credit card).

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

### Camera streaming (optional)

If your build has a USB webcam mounted on the chassis, set up two services on the Pi — **MediaMTX** (the RTSP server) and **webcam-stream** (the GStreamer pipeline that pushes camera frames into it).

#### 1. Install GStreamer

```bash
sudo apt-get install gstreamer1.0-tools \
                     gstreamer1.0-plugins-base \
                     gstreamer1.0-plugins-good \
                     gstreamer1.0-plugins-bad \
                     gstreamer1.0-plugins-ugly \
                     gstreamer1.0-libav \
                     v4l-utils
```

#### 2. Install MediaMTX

MediaMTX is a single static binary — download the latest aarch64 release for the Pi 5:

```bash
cd /tmp
LATEST=$(curl -s https://api.github.com/repos/bluenviron/mediamtx/releases/latest | grep tag_name | cut -d '"' -f 4)
wget "https://github.com/bluenviron/mediamtx/releases/download/${LATEST}/mediamtx_${LATEST}_linux_arm64.tar.gz"
tar -xzf "mediamtx_${LATEST}_linux_arm64.tar.gz"
sudo mv mediamtx /usr/local/bin/
sudo mkdir -p /etc/mediamtx
sudo mv mediamtx.yml /etc/mediamtx/mediamtx.yml
```

Create the systemd unit `/etc/systemd/system/mediamtx.service`:

```bash
sudo tee /etc/systemd/system/mediamtx.service > /dev/null << 'EOF'
[Unit]
Description=MediaMTX RTSP Server
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/mediamtx /etc/mediamtx/mediamtx.yml
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF
```

#### 3. Verify the camera is detected

Plug the USB webcam in, then:

```bash
v4l2-ctl --list-devices
v4l2-ctl --device=/dev/video0 --list-formats-ext
```

You should see your webcam under a `/dev/video0` (and usually a sibling `/dev/video1`). Confirm it supports `MJPG @ 1280×720 @ 30fps` — if not, adjust the pipeline below to a resolution your camera advertises.

#### 4. Install the GStreamer push service

```bash
sudo tee /etc/systemd/system/webcam-stream.service > /dev/null << 'EOF'
[Unit]
Description=Webcam RTSP Streaming Service
After=network.target mediamtx.service
Requires=mediamtx.service

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi
ExecStart=/usr/bin/gst-launch-1.0 -v \
  v4l2src device=/dev/video0 ! \
  image/jpeg,width=1280,height=720,framerate=30/1 ! \
  jpegdec ! \
  videoconvert ! \
  x264enc tune=zerolatency bitrate=1000 speed-preset=ultrafast threads=2 key-int-max=30 ! \
  h264parse ! \
  rtspclientsink location=rtsp://localhost:8554/webcam
Restart=on-failure
RestartSec=10
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF
```

> If you prefer to edit either service later, use `sudo nano /etc/systemd/system/<name>.service` — do **not** use `systemctl edit --full`, which insists on saving to a `.save` file and confuses the unit.

#### 5. Enable and start both services

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now mediamtx.service webcam-stream.service
```

#### 6. Verify

```bash
systemctl status mediamtx --no-pager       | head -10
systemctl status webcam-stream --no-pager  | head -10
```

Both should show `active (running)`. If `webcam-stream` shows `activating (auto-restart)`, look at `journalctl -u webcam-stream -n 50 --no-pager` for the underlying error (camera unplugged, wrong resolution, GStreamer plugin missing, etc.).

#### 7. View the stream

From any device on the same network:

| Client | How |
|---|---|
| **VLC** (macOS / Windows / Linux) | `File → Open Network → rtsp://<pi-ip>:8554/webcam` |
| **VLC for Mobile** (iOS / Android) | `+ → Open Network Stream → rtsp://<pi-ip>:8554/webcam` |
| **ffplay** (CLI) | `ffplay -fflags nobuffer rtsp://<pi-ip>:8554/webcam` |
| **On the Pi itself** | `ffplay rtsp://localhost:8554/webcam` |

Typical end-to-end latency over local Wi-Fi is 300–500 ms with this pipeline — acceptable for FPV driving and PID tuning.

#### Quick on/off

If you ever want to disable the camera (e.g. to free up CPU during aggressive PID tuning), without uninstalling anything:

```bash
sudo systemctl stop    webcam-stream mediamtx
sudo systemctl disable webcam-stream mediamtx   # don't auto-start on boot
```

Re-enable later with `sudo systemctl enable --now webcam-stream mediamtx`.

## File layout

```
BalanceRobotPI/                 # Pi side (Qt/C++)
├── main.cpp                    # Entry point, signal handling
├── balancerobot.cpp/.h         # BLE command handler, top-level orchestration
├── robotcontrol.cpp/.h         # Main control loop (PID, ISRs, motor I/O)
├── pid.cpp/.h                  # Pitch PID
├── kalman.h                    # Header-only Kalman fusion (TKJ Electronics)
├── mpu6050.cpp/.h              # IMU driver
├── i2cdev.cpp/.h               # I²C transport for the MPU6050
├── gattserver.cpp/.h           # BLE GATT server
├── message.cpp/.h              # BLE wire-format pack / parse
├── pidautotuner.cpp/.h         # Twiddle PID auto-tuner (learning mode)
├── voiceassistant.cpp/.h       # Phone-free voice assistant (mic + Vosk + TTS)
├── llmclient.cpp/.h            # Gemini/Claude REST client with robot tools (Qt)
├── constants.h                 # Pin definitions + small helpers
├── BalanceRobotPI.pro          # qmake project
└── settings.ini                # Runtime-persisted parameters (created on first run)

BalanceRobotRemote_IOS/         # iOS side (Swift)
└── RobotControlBLE/
    ├── AppDelegate.swift
    ├── ControlViewController.swift   # Main screen (joystick + telemetry)
    ├── SettingsViewController.swift  # Settings tab (PID sliders)
    ├── JoystickView.swift            # Joystick widget
    ├── MessageService.swift          # BLE message IDs
    ├── AppSettings.swift             # UserDefaults wrapper (incl. Claude API key/model)
    ├── Assistant/
    │   ├── AssistantViewController.swift # Claude tab (chat UI, mic, TTS)
    │   ├── ClaudeService.swift           # Anthropic Messages API client + tool loop
    │   ├── LocalCommandParser.swift      # FREE offline keyword command mode (no API)
    │   ├── GeminiService.swift           # FREE-quota Google Gemini client + tool loop
    │   ├── RobotCommandExecutor.swift    # Claude tool calls -> BLE robot commands
    │   └── SpeechRecognizer.swift        # AVAudioEngine + SFSpeechRecognizer
    └── Ble/
        ├── BluetoothService.swift        # CoreBluetooth wrapper
        └── BluetoothEventsHandler.swift  # Telemetry / notification parsing
```

## Runtime flow

1. Pi boots → `settings.ini` is loaded → BLE starts advertising
2. iOS connects → Settings tab requests current parameters from Pi
3. Robot held upright → IMU detects upright pose → if `Auto-arm` is enabled, it automatically ARMs
4. Pitch PID begins balancing; Speed PID requests a tilt based on joystick input
5. If fall is detected (>40°) → DISARM, motors stop
6. When held upright again, it auto-restarts

## First boot checklist

Before strapping the robot to a LiPo and watching it dance across the floor, run through this list with the chassis sitting on a table (motors free to spin but the robot itself supported so it can't fall).

### 1. I²C and the IMU

With the Pi powered (USB-C is fine for this — driver doesn't need to be on yet):

```bash
sudo apt-get install i2c-tools   # if not installed
sudo i2cdetect -y 1
```

You should see a device at address `68`:

```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- --
...
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- --
70: -- -- -- -- -- -- -- --
```

**If `68` is missing:**
- I²C is not enabled — run `sudo raspi-config` → Interface Options → I2C → Enable, then reboot
- Wiring is wrong — check VCC/GND/SDA/SCL on header pins 2/6/3/5
- The breakout is dead — try another MPU6050

### 2. Bluetooth

```bash
systemctl status bluetooth        # should be "active (running)"
bluetoothctl list                 # at least one controller, "powered: yes"
hciconfig                         # hci0 should be "UP RUNNING"
```

**If the controller is `DOWN`:** `sudo hciconfig hci0 up` or `sudo systemctl restart bluetooth`.

### 3. Driver power switch

On the Waveshare driver, the **OFF/ON switch (callout #8)** decides who powers the Pi. For battery operation, set it to **ON** so the driver's 5 V regulator feeds the Pi from the LiPo. Forget this and the Pi will refuse to boot the moment you unplug USB-C.

### 4. First app launch (with motors disconnected)

Disconnect the motor wires from the driver's screw terminals for this test — we just want to see the app boot cleanly without anything moving.

```bash
cd ~/BalanceRobotPI
sudo ./BalanceRobotPI
```

Look for these lines on stdout / journal:

```
WiringPi ready (encoder ISRs enabled)
RobotControl ready. Kp= 29.2 Ki= 64 Kd= 0.103 trim= 0
Local IP: 192.168.x.x MAC: xx:xx:xx:xx:xx:xx
GattServer advertising as "BalanceRobot"
```

If you see *"wiringPiSetupPhys failed"* → you forgot `sudo` (WiringPi needs `/dev/mem`).

If you see *"Failed to register left/right encoder ISR"* → another process is holding `/sys/class/gpio` — usually a leftover instance, kill it with `sudo pkill BalanceRobotPI`.

### 5. Gyro bias calibration

In the first few seconds after `BalanceRobotPI` starts, **before** the BLE server begins advertising, the code runs an automatic gyro bias calibration. The IMU is sampled 400 times at 5 ms intervals (~2 s per attempt, up to 3 attempts with 1 s pause between) and the mean is stored as the bias to subtract from every subsequent reading. **If the robot moves at all during this window, calibration fails and balance accuracy suffers.**

The acceptance thresholds in `robotcontrol.cpp`:

| Metric | Limit |
|---|---|
| Mean gyro magnitude per axis | < **5 °/s** |
| Sample standard deviation per axis | < **3 °/s** (this is the motion detector) |

**How to hold the robot during calibration:**

The shortest version — **lay the robot on its side on a hard, level surface and don't touch it.** That's it.

Why on its side, not held in your hand?
- Hand tremor easily produces > 3 °/s of jitter, even when you think you're holding still — and the code reads that as motion, then retries until it fails.
- A hard surface (desk, tile, hardwood) couples the IMU to the room; the calibration window sees a true zero.
- Carpet or foam pads partially work but aren't ideal — motor weight makes the chassis settle, and even small settling looks like motion to the gyro.

What's happening on stdout:

```
Calibrating gyro bias (attempt 1/3) - keep the robot still...
Gyro bias OK: X= 0.012  Y= -0.034  Z= 0.008 °/s
```

If you see `⚠ motion detected during calibration` instead, the code automatically retries. Two more failures and it gives up, keeps the previous bias (or zeros, on a fresh install), and logs:

```
✗ Calibration FAILED after 3 attempts. Robot may not balance properly.
   Place the robot flat on a hard surface and restart.
```

**The good news — it's a one-time event per session.** Once calibration succeeds, the bias values are persisted to a separate `bias.ini` file next to `BalanceRobotPI`, so even if a subsequent start-up fails calibration (someone bumps the table) the loaded values are still usable.

**When you should force a fresh calibration:**
- You re-seated the MPU6050 breakout on the chassis
- You swapped in a different MPU6050 board
- The robot was stored in very different temperature than usual (gyro bias has a thermal drift coefficient)
- The bias log lines look way off (e.g. `X= 4.2 °/s` instead of `X= 0.05 °/s`)

To force a re-calibration: stop the app, `sudo rm bias.ini` from the `BalanceRobotPI` directory, restart.

### 6. iOS pairing

On the iPhone, in the iOS app:

1. Open the **Control** tab
2. Tap **Connect**
3. The scanner should pick up a device called **`BalanceRobot`**
4. Tap it — within ~2 seconds you should see `Connected` and the robot's IP appear underneath

If you don't see `BalanceRobot` in the list:
- iOS Bluetooth is off — toggle it
- The Pi isn't advertising — recheck step 2 above
- The Pi was paired before and is now using a different MAC — open iOS Settings → Bluetooth, forget the old entry, retry

Once connected, the iOS app pulls the current parameters from the Pi and populates the Settings tab. If the sliders all read `0`, the parameter read didn't complete — disconnect, reconnect.

## First calibration

Now reconnect the motors (driver screw terminals: M1 = right, M2 = left) and reattach the LiPo (switch #8 ON). The robot is still on a stand or held by hand — don't let it fall over yet.

### A. Encoder direction sanity check

The Speed PID needs encoders that *count up when the wheel rolls forward*. If either wheel's encoder counts the wrong way, the robot will accelerate uncontrollably instead of braking — easy to spot but dangerous to ignore.

With `BalanceRobotPI` running, lift the robot off the stand and **manually roll the right wheel forward** (top of wheel moves toward the front of the robot). Watch the telemetry on the iOS app — the encoder count or velocity for that wheel should go *positive*.

- Goes negative? → set `encoderInvertR=true` in `settings.ini` (or have the iOS app send the right BLE command — depends on the build)
- Same test for the left wheel

This is much safer to do *before* arming the robot.

### B. Mechanical balance point (AC trim)

Power up, then hold the robot perfectly upright by gripping the top deck. Read the **ANGLE** field on the iOS Control tab:

- Reads `0.0° ± 0.5°` → great, your IMU is mounted level, leave `AC = 0`
- Reads e.g. `+1.8°` when you feel the robot is upright → the IMU sits slightly tilted (or the chassis itself does); compensate by setting `AC = -1.8°` from the Settings tab
- The trim is in degrees, signed; positive = front of robot is biased forward

After tuning AC, the robot should feel "neutral" when held upright — releasing it should let it fall in any direction with equal probability, not consistently toward one side.

### C. First armed run

Now the moment of truth:

1. Hold the robot upright, on the floor (carpet is more forgiving than tile)
2. Make sure `Auto-arm` is **ON** in the iOS Settings tab
3. The instant the IMU sees the upright pose, the motors should jump to life and try to hold the position
4. **Slowly** release your grip — fingers ready to catch
5. If it stays up for more than ~2 seconds, you've made it past the hardest part

If it falls immediately:
- Watch which way it falls. **Always the same direction** → AC trim is off, tweak it.
- Both directions, ~50/50 → Kp is too low, raise from 25 to 30
- High-frequency vibration before falling → Kp is too high, lower to 20

If it stays up but drifts in one direction:
- That's normal until Speed PID is tuned — see **Tuning guide** above
- Or your floor isn't level — try a different room

### D. Joystick test

With the robot balancing (or held in your hand):
- Push the joystick gently forward → the robot should *lean forward* and start rolling
- Release → it should *lean back* to brake itself to a stop (this is the Speed PID doing its job)
- If pushing forward makes it lean *backward* → joystick polarity is inverted, set `invertY = true` in `ControlViewController.swift:41`

## Troubleshooting

Common symptoms grouped by what's failing, with most likely cause first.

### Build / install

| Symptom | Likely cause | Fix |
|---|---|---|
| `qmake6: command not found` | Qt6 not installed | `sudo apt-get install qmake6 qt6-base-dev qt6-connectivity-dev` |
| `wiringPi.h: No such file or directory` | WiringPi not built | Clone from `github.com/WiringPi/WiringPi`, `sudo ./build` |
| `i2c/smbus.h: No such file or directory` | `libi2c-dev` missing | `sudo apt-get install libi2c-dev` |
| `cannot find -lwiringPi` at link time | WiringPi built but `ldconfig` cache stale | `sudo ldconfig`, retry |
| Build OK but `BalanceRobotPI` won't start: *"wiringPiSetupPhys failed"* | Ran without `sudo` | Use `sudo ./BalanceRobotPI` |

### Hardware / first power-up

| Symptom | Likely cause | Fix |
|---|---|---|
| Pi won't boot when LiPo is connected, USB-C unplugged | Driver power switch (#8) is OFF | Set it to ON |
| No I²C devices in `i2cdetect` | I²C not enabled in raspi-config | Enable + reboot |
| `0x68` shows but reads garbage | SDA/SCL swapped, or weak pull-ups | Check header pins 3/5; the MPU6050 breakout has its own pull-ups, but bad jumper wires defeat them |
| Motors hum but don't move | Driver VIN voltage too low (LiPo flat) | Check VIN with multimeter, charge pack |
| One motor spins, the other doesn't | Loose screw terminal, or motor M1/M2 swapped | Re-seat both pairs of motor leads in the driver |
| Motor spins, but the wrong direction | Driver output A/B swapped, *or* encoder direction inverted | Easier fix in software: `encoderInvertL/R` |

### BLE

| Symptom | Likely cause | Fix |
|---|---|---|
| iOS app doesn't see `BalanceRobot` in scanner | Pi BLE not advertising | `sudo systemctl restart bluetooth`, restart `BalanceRobotPI` |
| Connects but sliders all read 0 | Parameter read raced the connection event | Disconnect, reconnect once |
| Connects then drops within 1–2 seconds | Pi went to sleep or BlueZ crashed | Check `journalctl -u bluetooth -n 50`; disable Wi-Fi power save: `sudo iwconfig wlan0 power off` |
| Telemetry frozen (always same values) | BalanceRobotPI process died but BLE stayed up | `ps aux \| grep Balance` — restart if it's gone |

### Balance behaviour

| Symptom | Likely cause | Fix |
|---|---|---|
| Always falls in the same direction the instant you let go | AC trim off, or IMU tilted | Adjust `AC` to compensate |
| Both directions, falls in <1 second | Kp too low | Raise to 30–35 |
| High-frequency vibration, then falls | Kp too high | Lower Kp; if still vibrating, raise Kd to 0.15 |
| Balances but drifts steadily forward / backward | Speed PID integral not catching up | Raise Speed Ki (0.20 → 0.30) |
| Balances but oscillates back and forth slowly | Speed Kp too high | Lower to 0.10 |
| Long coast after stick release before braking | Speed Ki too low | Raise to 0.25–0.30 |
| Twitches violently the moment Auto-arm fires | IMU still settling (calibration period missed) | Hold robot perfectly still for 3 seconds before raising upright |
| Balance steadily drifts in pitch even when sitting still | Gyro bias calibration was bad (e.g. robot moved during the 2 s window) | Stop the app, `sudo rm bias.ini`, lay the robot on its side on a hard surface, restart |
| Disarms randomly mid-run | Fall detector triggered by sharp transient | Check that IMU isn't loose; reduce `Kd` if vibration is the trigger |
| Wheels slip and robot can't catch its balance back | Slippery floor / worn tyre rubber | Different surface, or fresh tyres |

### Camera streaming

| Symptom | Likely cause | Fix |
|---|---|---|
| `webcam-stream` shows `activating (auto-restart)` | GStreamer pipeline failed | `journalctl -u webcam-stream -n 50` — usually wrong resolution or camera unplugged |
| Stream URL opens in VLC but no image | Camera initialised but pipeline negotiating | Wait 3–5 s; if still black, check `v4l2-ctl --list-formats-ext` matches the pipeline caps |
| Stream works, but balance robot now jitters | Software H.264 encoder is starving the PID loop | Lower video to 640×480, or `sudo systemctl stop webcam-stream` during tuning |
| `Connection refused` on port 8554 | MediaMTX not running | `sudo systemctl status mediamtx`; restart if dead |

## Known limits

- Below ~9.5 V on the 3S pack (≈3.17 V/cell), PWM saturates during hard corrections and balancing degrades — land and swap packs
- On slippery floors, wheel slip causes encoder velocity to lie
- Very fast maneuvers can saturate the IMU rate gyro

## License

MIT.

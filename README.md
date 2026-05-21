# Balance Robot — Pi 5 + iOS Remote (BLE)

İki tekerlekli kendini dengeleyen robot. Raspberry Pi 5 üzerinde Qt/C++ kontrol uygulaması ve iOS BLE uzaktan kumanda.

## Mimari

```
iOS Joystick ──BLE GATT──> Pi 5 (Qt/C++) ──> Motor Driver
                              ↑                    ↓
                           MPU6050              Encoders
                          (IMU @ I2C)          (Quadrature)
```

**Kontrol mimarisi — B-Robot tarzı kademeli (cascade) PID:**

```
joystick ──> Speed PID ──> hedef tilt ──> Pitch PID ──> motor PWM
              (outer)                       (inner)
                ↑                              ↑
            encoder vel                  IMU açısı + gyro
```

- **Speed PID (dış halka):** Hedef hızı tutturmak için robot için doğru eğim açısını hesaplar. Komut bırakıldığında karşı yöne eğilerek **otomatik fren** yapar.
- **Pitch PID (iç halka):** Hedef açıya yetişmek için motor PWM üretir, robotu dengede tutar.

## Parametreler

Tüm parametreler iOS Settings sekmesinden canlı ayarlanır ve Pi'da `settings.ini`'ye kaydedilir.

### Pitch PID — Denge (iç halka)

| Parametre | Default | Açıklama |
|-----------|---------|----------|
| P (Proportional) | 25 | Açı hatasına anlık tepki. Yüksek = sert düzeltme, salınım riski. |
| I (Integral) | 40 (0.40 real) | Sürekli açı hatasını düzeltir, gravite bias'ı için. |
| D (Derivative) | 0.10 | Açı değişim hızı — sönümleme. Yüksek = sallanmayı azaltır. |
| Angle trim (AC) | 0.0° | Manuel açı ofseti. |
| Yaw gain (SD) | 2.0 | Encoder farkından yön düzeltme. |

### Speed PID — Hız kontrolü (dış halka)

| Parametre | Default | Açıklama |
|-----------|---------|----------|
| Speed Kp | 0.12 | Hız hatasına anlık tepki. Yüksek = çevik ama salınım. |
| Speed Ki | 0.20 | Hız hatasının integraliyle sürekli düzeltme. Asıl iş bunda. |
| Speed Max Tilt | 5° | Speed PID'in talep edebileceği maksimum eğim. |
| Speed Max Vel | 3.0 | Joystick tam ittiğindeki hedef hız (encoder tick/loop). |

### iOS-only

| Parametre | Default | Açıklama |
|-----------|---------|----------|
| Forward speed | 180 | Joystick ileri/geri maksimum byte değeri (0-255). |
| Turn speed | 60 | Joystick sol/sağ maksimum byte değeri. |

## Tuning rehberi

### Hızlı tepki istiyorsan
1. Önce **Speed Max Vel** artır (3.0 → 4.0 → 5.0) — hedef hız sınırı yükselir
2. Sonra **Speed Kp** artır (0.12 → 0.15 → 0.18) — daha çevik tepki
3. Yetmezse **Speed Max Tilt** artır (5° → 6° → 7°) — daha güçlü ivme

### Sorun işaretleri ve çözümleri
- **Komut bırakınca uzun süre kayıyor** → Speed Ki artır (0.20 → 0.30)
- **Salınımlı / titrek** → Speed Kp düşür
- **Bırakınca geri sallanıp düşüyor** → Speed Ki düşür
- **Yerinde durmuyor, sürekli kayıyor** → Pitch Ki artır
- **Yüksek frekanslı titreme** → Pitch Kd artır
- **Kalkış sırasında ileri-geri sallanma** → Pitch Kp düşür

### Reset to Defaults
iOS Settings sekmesinin alt kısmında **kırmızı "Reset all settings to defaults"** butonu. Tüm PID parametrelerini Pi default değerlerine geri çevirir.

## Yön konvansiyonu

- **Joystick yukarı** = ileri komut
- **Joystick aşağı** = geri komut
- **Joystick sağ/sol** = yön dönüşü
- Pi tarafında: `cmd > 0` ileri, `cmd < 0` geri
- Encoder pozitif yön = robotun ileri yönü

`ControlViewController.swift:41` içindeki `invertY = false` bunu sağlıyor. Eğer motorlarını ters bağladıysan `invertY = true` yap.

## BLE protokol — Mesaj ID'leri

`message.h` (Pi) ve `MessageService.swift` (iOS) eşleşmeli.

```
mHeader        = 0xb0  // Paket başlığı
mWrite         = 0x01  // Yazma isteği
mRead          = 0x02  // Okuma isteği
mArmed         = 0x03  // Robot ARM
mDisArmed      = 0x04  // Robot DISARM
mForward       = 0xa0  // İleri komut
mBackward      = 0xa1  // Geri komut
mLeft          = 0xb0  // Sol dönüş
mRight         = 0xb1  // Sağ dönüş
mPP            = 0xc0  // Pitch Kp
mPI            = 0xc1  // Pitch Ki
mPD            = 0xc2  // Pitch Kd
mSpdKp         = 0xc3  // Speed Kp (value/100)
mSpdKi         = 0xc4  // Speed Ki (value/100)
mSpdMaxTilt    = 0xc5  // Speed Max Tilt (raw degrees)
mSpdMaxVel     = 0xc6  // Speed Max Vel (value/10)
mAC            = 0xd0  // Açı trim (value/10)
mSD            = 0xd1  // Yaw gain (value/10)
mTelemetry     = 0xf0  // Canlı telemetri
mAutoMode      = 0xf1  // Otomatik kalkış
mTrimFine      = 0xf2  // Hassas trim
mPositionHold  = 0xf3  // Pozisyon kilidi
mResetTrim     = 0xf4  // Trim sıfırla
```

## Donanım

- **Raspberry Pi 5** — ana kontrolcü
- **MPU6050** — IMU (I2C, address 0x68)
- **L298N / RPi Motor Driver** — motor sürücü
- **2× DC motor + quadrature encoder** — tahrik

### GPIO pinleri (wiringPi numerasyonu)

```
SPD_INT_L = 16   // Sol encoder INT
SPD_PUL_L = 12   // Sol encoder PUL
SPD_INT_R = 18   // Sağ encoder INT
SPD_PUL_R = 22   // Sağ encoder PUL
```

## Kurulum

### Pi 5

```bash
# Bağımlılıklar
sudo apt install qt5-default libqt5bluetooth5 libi2c-dev wiringpi

# Derle
cd ~/BalanceRobotPI
make clean && make

# Çalıştır
sudo ./BalanceRobotPI
```

### iOS

1. `BalanceRobotRemote_IOS.zip` indir, çıkar
2. `RobotControlBLE.xcodeproj` Xcode'da aç
3. Bundle identifier kendi geliştirici hesabınla güncelle
4. Build & Run telefona yükle

### Otomatik başlatma (Pi systemd)

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

## Dosya yapısı

```
BalanceRobotPI/                 # Pi tarafı (Qt/C++)
├── main.cpp
├── balancerobot.cpp/.h         # BLE komut handler
├── robotcontrol.cpp/.h         # Ana kontrol döngüsü (PID, ISR)
├── pid.cpp/.h                  # Pitch PID
├── mpu6050.cpp/.h              # IMU
├── message.h                   # BLE protokol mesaj ID'leri
├── kalmanfilter.cpp/.h         # Açı füzyonu
├── gattserver.cpp/.h           # BLE GATT sunucu
└── settings.ini                # Çalışma zamanında kalıcı parametreler

BalanceRobotRemote_IOS/         # iOS tarafı (Swift)
└── RobotControlBLE/
    ├── AppDelegate.swift
    ├── ControlViewController.swift   # Ana ekran (joystick + telemetri)
    ├── SettingsViewController.swift  # Settings sekmesi (PID slider'ları)
    ├── JoystickView.swift            # Joystick widget
    ├── MessageService.swift          # BLE mesaj ID'leri
    ├── AppSettings.swift             # UserDefaults wrapper
    └── Ble/
        └── BluetoothService.swift    # CoreBluetooth wrapper
```

## Çalışma akışı

1. Pi açılır → `settings.ini` yüklenir → BLE advertise eder
2. iOS bağlanır → Settings sekmesi açıldığında Pi'dan tüm parametreleri ister
3. Robot dik tutulur → IMU upright algılar → `Auto-arm` aktifse otomatik ARM olur
4. Pitch PID dengelemeyi başlatır, Speed PID joystick komutlarına göre tilt önerir
5. Düşme tespit edilirse (>40°) → DISARM, motorlar durur
6. Tekrar dik tutulduğunda otomatik yeniden başlar

## Bilinen sınırlar

- Düşük gerilimde (pil zayıfsa) PWM yetersiz kalır, dengeleme bozulur
- Pürüzsüz zeminlerde tekerlek kayması olursa encoder okuma yanılır
- Çok hızlı manevra sırasında IMU saturation olabilir

## Lisans

MIT.

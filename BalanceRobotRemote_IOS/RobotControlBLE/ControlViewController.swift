//
//  ControlViewController.swift
//  RobotControlBLE
//
//  Main control tab: connection, live telemetry, joystick, ARM/STOP.
//

import UIKit
import CoreBluetooth

class ControlViewController: UIViewController {
    
    // MARK: - UI
    private let statusLabel    = UILabel()
    private let connectButton  = UIButton(type: .system)
    private let telemetryView  = TelemetryView()
    private let joystick       = JoystickView()
    private let armButton      = UIButton(type: .system)
    private let stopButton     = UIButton(type: .system)
    
    // Joystick side value displays
    private let fwdValueView   = ValueChip(title: "FWD")
    private let turnValueView  = ValueChip(title: "TRN")
    
    // MARK: - State
    private let bluetoothService = BluetoothService.shared
    private lazy var pairingFlow = PairingFlow(bluetoothService: self.bluetoothService)
    
    private var isConnected = false
    private var isArmedRemote = false
    
    // Joystick state — pumped to Pi by a 20 Hz timer
    private var joyX: Float = 0
    private var joyY: Float = 0
    private var commandTimer: Timer?
    private var lastSentSpeed: Int = 0
    private var lastSentTurn:  Int = 0
    
    // Direction-invert flags. Depending on motor wiring, "forward" on the
    // stick can map to backwards on the robot. Flip these if needed.
    private let invertY: Bool = false
    private let invertX: Bool = false
    
    // Deadzone — ignore joystick wobble around the center.
    private let deadzoneRadius: Float = 0.12   // 0..1
    
    // Axis separation: when the joystick is mostly pushed up, a small
    // horizontal component would still produce a turn command. If one
    // axis dominates, suppress the other when it's smaller than this
    // fraction of the dominant one.
    private let dominantAxisRatio: Float = 0.5
    
    // MARK: - Lifecycle
    
    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .compatScreenBackground
        
        bluetoothService.delegate = self
        bluetoothService.flowController = pairingFlow
        
        // Bluetooth durumu CBCentralManager tarafından asenkron olarak set
        // ediliyor. Notification ile değişimi dinle ki Connect butonu doğru
        // anda enable olsun.
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(bluetoothStateChanged),
            name: BluetoothService.stateDidChange,
            object: nil)
        
        // Peripheral bağlantı durumu değiştiğinde de UI güncellensin.
        // Robot kendiliğinden disconnect olursa isConnected = false yap,
        // joystick komutları boşa BLE'ye saplanmasın.
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(connectionChanged),
            name: BluetoothService.connectionDidChange,
            object: nil)
        
        buildUI()
        startCommandTimer()
    }
    
    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        // Settings sekmesi delegate'i değiştirmiş olabilir, geri al
        bluetoothService.delegate = self
        updateConnectionUI()
    }
    
    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        // Görünür olduğunda da yenile — bluetooth açılması viewDidLoad'tan
        // sonra ama notification bizden önce gelmiş olabilir.
        updateConnectionUI()
    }
    
    @objc private func bluetoothStateChanged() {
        DispatchQueue.main.async { [weak self] in
            self?.updateConnectionUI()
        }
    }
    
    @objc private func connectionChanged() {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            // BLE bağlantısı hala canlı mı? peripheral + RX karakteristiği varsa evet.
            let stillConnected = (self.bluetoothService.peripheral != nil)
                              && (self.bluetoothService.rxCharacteristic != nil)
            if !stillConnected && self.isConnected {
                self.isConnected = false
                self.isArmedRemote = false
                // Hareket halinde olan joystick komutlarını sıfırla
                self.joyX = 0; self.joyY = 0
                self.lastSentSpeed = 0
                self.lastSentTurn = 0
                self.fwdValueView.update(0)
                self.turnValueView.update(0)
                self.statusLabel.text = "Robot disconnected"
            }
            self.updateConnectionUI()
        }
    }
    
    // MARK: - UI build
    
    private func buildUI() {
        statusLabel.font = .systemFont(ofSize: 14, weight: .medium)
        statusLabel.textColor = .compatPrimaryText
        statusLabel.text = "Bluetooth: …"
        statusLabel.textAlignment = .center
        statusLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(statusLabel)
        
        configureBigButton(connectButton, title: "Connect",
                           bgColor: .compatSystemBlue,
                           action: #selector(connectTapped))
        view.addSubview(connectButton)
        
        telemetryView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(telemetryView)
        
        configureBigButton(armButton, title: "ARM",
                           bgColor: .compatSystemGray4,
                           action: #selector(armTapped))
        view.addSubview(armButton)
        
        configureBigButton(stopButton, title: "STOP",
                           bgColor: .compatSystemRed,
                           action: #selector(stopTapped))
        view.addSubview(stopButton)
        
        joystick.translatesAutoresizingMaskIntoConstraints = false
        joystick.delegate = self
        view.addSubview(joystick)
        
        fwdValueView.translatesAutoresizingMaskIntoConstraints = false
        turnValueView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(fwdValueView)
        view.addSubview(turnValueView)
        
        // Joystick'i dikey ortalamak için bir spacer layout guide kullan:
        // joyArea = armButton.bottom + 20 .. safeArea.bottom - 20
        // joystick.centerY = joyArea.centerY
        let joyArea = UILayoutGuide()
        view.addLayoutGuide(joyArea)
        
        let g = view.safeAreaLayoutGuide
        NSLayoutConstraint.activate([
            joyArea.topAnchor.constraint(equalTo: armButton.bottomAnchor, constant: 12),
            joyArea.bottomAnchor.constraint(equalTo: g.bottomAnchor, constant: -12),
            joyArea.leadingAnchor.constraint(equalTo: g.leadingAnchor),
            joyArea.trailingAnchor.constraint(equalTo: g.trailingAnchor),
            statusLabel.topAnchor.constraint(equalTo: g.topAnchor, constant: 8),
            statusLabel.leadingAnchor.constraint(equalTo: g.leadingAnchor, constant: 16),
            statusLabel.trailingAnchor.constraint(equalTo: g.trailingAnchor, constant: -16),
            
            connectButton.topAnchor.constraint(equalTo: statusLabel.bottomAnchor, constant: 8),
            connectButton.leadingAnchor.constraint(equalTo: g.leadingAnchor, constant: 16),
            connectButton.trailingAnchor.constraint(equalTo: g.trailingAnchor, constant: -16),
            connectButton.heightAnchor.constraint(equalToConstant: 44),
            
            telemetryView.topAnchor.constraint(equalTo: connectButton.bottomAnchor, constant: 12),
            telemetryView.leadingAnchor.constraint(equalTo: g.leadingAnchor, constant: 16),
            telemetryView.trailingAnchor.constraint(equalTo: g.trailingAnchor, constant: -16),
            telemetryView.heightAnchor.constraint(equalToConstant: 140),
            
            armButton.topAnchor.constraint(equalTo: telemetryView.bottomAnchor, constant: 14),
            armButton.leadingAnchor.constraint(equalTo: g.leadingAnchor, constant: 16),
            armButton.widthAnchor.constraint(equalTo: g.widthAnchor, multiplier: 0.45, constant: -20),
            armButton.heightAnchor.constraint(equalToConstant: 50),
            
            stopButton.topAnchor.constraint(equalTo: telemetryView.bottomAnchor, constant: 14),
            stopButton.trailingAnchor.constraint(equalTo: g.trailingAnchor, constant: -16),
            stopButton.widthAnchor.constraint(equalTo: g.widthAnchor, multiplier: 0.45, constant: -20),
            stopButton.heightAnchor.constraint(equalToConstant: 50),
            
            // Joystick — joyArea içinde dikey ortalı, ekran genişliğinin %58'i,
            // 180..260px arasında.
            joystick.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            joystick.centerYAnchor.constraint(equalTo: joyArea.centerYAnchor),
            joystick.topAnchor.constraint(greaterThanOrEqualTo: joyArea.topAnchor),
            joystick.bottomAnchor.constraint(lessThanOrEqualTo: joyArea.bottomAnchor),
            joystick.widthAnchor.constraint(equalTo: joystick.heightAnchor),
            joystick.widthAnchor.constraint(equalTo: g.widthAnchor,
                                            multiplier: 0.58).withPriority(.defaultHigh),
            joystick.widthAnchor.constraint(lessThanOrEqualToConstant: 260),
            joystick.widthAnchor.constraint(greaterThanOrEqualToConstant: 180),
            
            // FWD chip — joystick'in solunda, dikey olarak joystick ile aynı
            fwdValueView.centerYAnchor.constraint(equalTo: joystick.centerYAnchor),
            fwdValueView.trailingAnchor.constraint(lessThanOrEqualTo: joystick.leadingAnchor,
                                                   constant: -12),
            fwdValueView.leadingAnchor.constraint(greaterThanOrEqualTo: g.leadingAnchor,
                                                  constant: 12),
            fwdValueView.widthAnchor.constraint(equalToConstant: 80),
            
            // TRN chip — joystick'in sağında
            turnValueView.centerYAnchor.constraint(equalTo: joystick.centerYAnchor),
            turnValueView.leadingAnchor.constraint(greaterThanOrEqualTo: joystick.trailingAnchor,
                                                   constant: 12),
            turnValueView.trailingAnchor.constraint(lessThanOrEqualTo: g.trailingAnchor,
                                                    constant: -12),
            turnValueView.widthAnchor.constraint(equalToConstant: 80),
        ])
        
        // Yan etiketleri ekran kenarlarına yakın yapıştır (joystick'in
        // tam yanına gelmesin diye preferred trailing/leading koy):
        let fwdTrail = fwdValueView.trailingAnchor.constraint(
            equalTo: joystick.leadingAnchor, constant: -12)
        fwdTrail.priority = .defaultHigh
        fwdTrail.isActive = true
        
        let turnLead = turnValueView.leadingAnchor.constraint(
            equalTo: joystick.trailingAnchor, constant: 12)
        turnLead.priority = .defaultHigh
        turnLead.isActive = true
    }
    
    private func configureBigButton(_ b: UIButton, title: String, bgColor: UIColor, action: Selector) {
        b.setTitle(title, for: .normal)
        b.titleLabel?.font = .systemFont(ofSize: 18, weight: .semibold)
        b.setTitleColor(.white, for: .normal)
        b.backgroundColor = bgColor
        b.layer.cornerRadius = 10
        b.translatesAutoresizingMaskIntoConstraints = false
        b.addTarget(self, action: action, for: .touchUpInside)
    }
    
    // MARK: - UI updates
    
    private func updateConnectionUI() {
        let btState = bluetoothService.bluetoothState
        let btOn = (btState == .poweredOn)
        statusLabel.text = btOn ? (isConnected ? "Connected" : "Bluetooth ready — robot disconnected")
                                : "Bluetooth is OFF"
        
        connectButton.setTitle(isConnected ? "Disconnect" : "Connect", for: .normal)
        connectButton.backgroundColor = isConnected ? .compatSystemGray4 : .compatSystemBlue
        connectButton.isEnabled = btOn
        connectButton.alpha = btOn ? 1.0 : 0.5
        
        updateArmButton()
    }
    
    private func updateArmButton() {
        armButton.setTitle(isArmedRemote ? "DISARM" : "ARM", for: .normal)
        armButton.backgroundColor = isArmedRemote ? .compatSystemGreen : .compatSystemGray4
        armButton.isEnabled = isConnected
        armButton.alpha = isConnected ? 1.0 : 0.5
        stopButton.isEnabled = isConnected
        stopButton.alpha = isConnected ? 1.0 : 0.5
    }
    
    // MARK: - Actions
    
    @objc private func connectTapped() {
        guard bluetoothService.bluetoothState == .poweredOn else { return }
        
        if isConnected {
            pairingFlow.cancel()
            isConnected = false
            updateConnectionUI()
            return
        }
        
        statusLabel.text = "Scanning..."
        pairingFlow.waitForPeripheral { [weak self] in
            guard let self = self else { return }
            self.statusLabel.text = "Connecting..."
            self.pairingFlow.pair { result in
                if let name = self.bluetoothService.peripheral?.name, !name.isEmpty {
                    self.isConnected = true
                    self.statusLabel.text = "Connected: \(name)"
                    self.updateConnectionUI()
                    // PID değerlerini iste (Settings sekmesi göstersin)
                    DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                        let payload = Data([0x00])
                        self.bluetoothService.requestData(msgId: mPP, data: payload)
                        self.bluetoothService.requestData(msgId: mPI, data: payload)
                        self.bluetoothService.requestData(msgId: mPD, data: payload)
                        self.bluetoothService.requestData(msgId: mAC, data: payload)
                        self.bluetoothService.requestData(msgId: mSD, data: payload)
                        self.bluetoothService.requestData(msgId: mArmed, data: payload)
                    }
                } else if result {
                    self.isConnected = true
                    self.statusLabel.text = "Connected"
                    self.updateConnectionUI()
                } else {
                    self.statusLabel.text = "Connection failed"
                    self.updateConnectionUI()
                }
            }
        }
    }
    
    @objc private func armTapped() {
        let nextArmed = !isArmedRemote
        isArmedRemote = nextArmed
        sendOne(msgId: nextArmed ? mArmed : mDisArmed, value: 0)
        updateArmButton()
    }
    
    @objc private func stopTapped() {
        // Emergency stop: zero motion, disarm
        joyX = 0; joyY = 0
        sendOne(msgId: mForward,  value: 0)
        sendOne(msgId: mBackward, value: 0)
        sendOne(msgId: mLeft,     value: 0)
        sendOne(msgId: mRight,    value: 0)
        sendOne(msgId: mDisArmed, value: 0)
        isArmedRemote = false
        lastSentSpeed = 0
        lastSentTurn  = 0
        fwdValueView.update(0)
        turnValueView.update(0)
        updateArmButton()
    }
    
    private func sendOne(msgId: Byte, value: UInt8) {
        var v = value
        let data = Data(bytes: &v, count: 1)
        bluetoothService.sendCommand(msgId: msgId, data: data)
    }
    
    // MARK: - Command timer
    //
    // Joystick durumunu sürekli BLE'ye yollamak yerine sabit 20 Hz tempo ile
    // tek bir komut gönder. State değişmediyse tekrar göndermez.
    
    private func startCommandTimer() {
        commandTimer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { [weak self] _ in
            self?.tickCommand()
        }
    }
    
    deinit {
        commandTimer?.invalidate()
        NotificationCenter.default.removeObserver(self)
    }
    
    private func tickCommand() {
        // İki kontrol: hem flag hem gerçek BLE durumu. Pi düşerse flag güncellenene
        // kadar gerçekçi bir yarış var; RX karakteristiği yoksa kesinlikle gönderme.
        guard isConnected,
              bluetoothService.rxCharacteristic != nil,
              bluetoothService.peripheral?.state == .connected else {
            // Bağlantı zaten kopmuş ama henüz farkındalık yoksa dahili state'i temizle
            if isConnected && bluetoothService.peripheral?.state != .connected {
                isConnected = false
                DispatchQueue.main.async { [weak self] in
                    self?.updateConnectionUI()
                }
            }
            return
        }
        
        // 1) Yön düzeltmesi
        var x = invertX ? -joyX : joyX
        var y = invertY ? -joyY : joyY
        
        // 2) Deadzone — merkeze yakın titremeleri sıfırla
        if abs(x) < deadzoneRadius { x = 0 }
        if abs(y) < deadzoneRadius { y = 0 }
        
        // 3) Eksen yumuşatma — deadzone sonrası kalanı yeniden 0..1'e map et,
        //    sonra kare alarak küçük hareketleri daha ince hale getir.
        x = shapeAxis(x)
        y = shapeAxis(y)
        
        // 4) Baskın eksen — joystick'i tam ileri ittiğinde küçük yatay
        //    sapma istemiyoruz. Daha küçük eksen, büyüğün yarısından da
        //    az ise 0 say.
        let ax = abs(x), ay = abs(y)
        if ay > ax && ax < ay * dominantAxisRatio { x = 0 }
        if ax > ay && ay < ax * dominantAxisRatio { y = 0 }
        
        let speed = Int((y * AppSettings.shared.forwardSpeed).rounded())
        let turn  = Int((x * AppSettings.shared.turnSpeed).rounded())
        
        // Sadece değişimde yolla
        if speed == lastSentSpeed && turn == lastSentTurn { return }
        
        // Linear
        if speed > 0 {
            sendOne(msgId: mForward,  value: UInt8(min(speed, 255)))
            if lastSentSpeed < 0 { sendOne(msgId: mBackward, value: 0) }
        } else if speed < 0 {
            sendOne(msgId: mBackward, value: UInt8(min(-speed, 255)))
            if lastSentSpeed > 0 { sendOne(msgId: mForward, value: 0) }
        } else {
            if lastSentSpeed > 0 { sendOne(msgId: mForward,  value: 0) }
            if lastSentSpeed < 0 { sendOne(msgId: mBackward, value: 0) }
        }
        
        // Turn
        if turn > 0 {
            sendOne(msgId: mRight, value: UInt8(min(turn, 255)))
            if lastSentTurn < 0 { sendOne(msgId: mLeft, value: 0) }
        } else if turn < 0 {
            sendOne(msgId: mLeft, value: UInt8(min(-turn, 255)))
            if lastSentTurn > 0 { sendOne(msgId: mRight, value: 0) }
        } else {
            if lastSentTurn > 0 { sendOne(msgId: mRight, value: 0) }
            if lastSentTurn < 0 { sendOne(msgId: mLeft,  value: 0) }
        }
        
        lastSentSpeed = speed
        lastSentTurn  = turn
        
        // Canlı değer chip'lerini güncelle
        fwdValueView.update(speed)
        turnValueView.update(turn)
    }
    
    /// Deadzone uygulandıktan sonra kalanı yeniden 0..1'e yay ve
    /// kuadratik tepkiye çevir: küçük itme küçük tepki, büyük itme tam tepki.
    private func shapeAxis(_ v: Float) -> Float {
        if v == 0 { return 0 }
        let sign: Float = v >= 0 ? 1 : -1
        let mag = min(abs(v), 1.0)
        if mag <= deadzoneRadius { return 0 }
        let scaled = (mag - deadzoneRadius) / (1.0 - deadzoneRadius)   // 0..1
        return sign * scaled * scaled
    }
}

// MARK: - Joystick delegate

extension ControlViewController: JoystickViewDelegate {
    func joystick(_ view: JoystickView, didMoveTo x: Float, y: Float) {
        joyX = x
        joyY = y
    }
    func joystickDidRelease(_ view: JoystickView) {
        joyX = 0
        joyY = 0
    }
}

// MARK: - Bluetooth delegate

extension ControlViewController: BluetoothServiceDelegate {
    func didReceiveIPAddress(_ ipAddress: String) {
        // Settings sekmesi gösterebilir; burada sadece log için.
        print("Robot IP: \(ipAddress)")
    }
    
    func didReceiveMessage(_ message: String) {
        print(message)
    }
    
    func didUpdateRobotArmedState(_ armed: Bool) {
        isArmedRemote = armed
        updateArmButton()
    }
    
    func didReceiveTelemetry(_ telemetry: RobotTelemetry) {
        DispatchQueue.main.async {
            self.telemetryView.update(telemetry: telemetry)
            if self.isArmedRemote != telemetry.armed {
                self.isArmedRemote = telemetry.armed
                self.updateArmButton()
            }
        }
    }
}

// MARK: - Constraint priority helper

private extension NSLayoutConstraint {
    func withPriority(_ p: UILayoutPriority) -> NSLayoutConstraint {
        self.priority = p
        return self
    }
}

// MARK: - ValueChip
//
// Joystick'in yanında canlı değer gösteren küçük kart.
// Üstte başlık (FWD/TRN), altta büyük değer.

final class ValueChip: UIView {
    private let titleLabel = UILabel()
    private let valueLabel = UILabel()
    private let unitLabel  = UILabel()
    
    init(title: String) {
        super.init(frame: .zero)
        
        backgroundColor = .compatPanelBackground
        layer.cornerRadius = 10
        layer.borderColor  = UIColor(white: 0.30, alpha: 1.0).cgColor
        layer.borderWidth  = 1
        
        titleLabel.text = title
        titleLabel.font = .systemFont(ofSize: 11, weight: .semibold)
        titleLabel.textColor = .compatSecondaryText
        titleLabel.textAlignment = .center
        
        if #available(iOS 13.0, *) {
            valueLabel.font = .monospacedSystemFont(ofSize: 20, weight: .semibold)
        } else {
            valueLabel.font = UIFont(name: "Menlo-Bold", size: 20) ?? .systemFont(ofSize: 20, weight: .semibold)
        }
        valueLabel.textColor = .compatPrimaryText
        valueLabel.textAlignment = .center
        valueLabel.text = "0"
        
        unitLabel.text = "PWM"
        unitLabel.font = .systemFont(ofSize: 9, weight: .medium)
        unitLabel.textColor = .compatSecondaryText
        unitLabel.textAlignment = .center
        
        let stack = UIStackView(arrangedSubviews: [titleLabel, valueLabel, unitLabel])
        stack.axis = .vertical
        stack.alignment = .fill
        stack.spacing = 2
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: topAnchor, constant: 8),
            stack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 6),
            stack.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -6),
            stack.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -8),
        ])
    }
    
    required init?(coder: NSCoder) { fatalError() }
    
    func update(_ value: Int) {
        valueLabel.text = String(format: "%+d", value)
        // Renk: pozitif → açık yeşil, negatif → açık kırmızı, sıfır → birincil
        if value > 0 {
            valueLabel.textColor = UIColor(red: 0.40, green: 0.95, blue: 0.50, alpha: 1.0)
        } else if value < 0 {
            valueLabel.textColor = UIColor(red: 1.00, green: 0.55, blue: 0.55, alpha: 1.0)
        } else {
            valueLabel.textColor = .compatPrimaryText
        }
    }
}

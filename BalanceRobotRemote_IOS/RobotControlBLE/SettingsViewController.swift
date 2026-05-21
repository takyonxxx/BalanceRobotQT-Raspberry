//
//  SettingsViewController.swift
//  RobotControlBLE
//
//  Ayarlar sekmesi: PID slider'ları, modlar, trim, ses, debug log.
//

import UIKit

class SettingsViewController: UIViewController {
    
    // MARK: - UI
    private let scrollView = UIScrollView()
    private let contentStack = UIStackView()
    
    // PID sliders
    private let pSlider = LabeledSlider(title: "P (Proportional)", min: 0, max: 100, format: "%.0f")
    private let iSlider = LabeledSlider(title: "I (Integral)",     min: 0, max: 255, format: "%.2f", scaleDivider: 100)
    private let dSlider = LabeledSlider(title: "D (Derivative)",   min: 0, max: 50,  format: "%.2f", scaleDivider: 100)
    private let acSlider = LabeledSlider(title: "Angle trim (AC)", min: 0, max: 50,  format: "%.1f°", scaleDivider: 10)
    private let sdSlider = LabeledSlider(title: "Yaw gain (SD)",   min: 0, max: 100, format: "%.2f", scaleDivider: 10)
    
    // Speed PID sliders (B-Robot style cascade — outer loop for velocity tracking)
    private let spdKpSlider      = LabeledSlider(title: "Speed Kp",    min: 0, max: 50,  format: "%.2f", scaleDivider: 100)
    private let spdKiSlider      = LabeledSlider(title: "Speed Ki",    min: 0, max: 100, format: "%.2f", scaleDivider: 100)
    private let spdMaxTiltSlider = LabeledSlider(title: "Speed Max Tilt", min: 1, max: 15, format: "%.0f°")
    private let spdMaxVelSlider  = LabeledSlider(title: "Speed Max Vel",  min: 10, max: 80, format: "%.1f", scaleDivider: 10)
    
    // Joystick speeds (iOS-only, stored in UserDefaults via AppSettings)
    private let forwardSpeedSlider = LabeledSlider(
        title: "Forward speed",
        min: AppSettings.shared.forwardSpeedRange.lowerBound,
        max: AppSettings.shared.forwardSpeedRange.upperBound,
        format: "%.0f PWM")
    private let turnSpeedSlider = LabeledSlider(
        title: "Turn speed",
        min: AppSettings.shared.turnSpeedRange.lowerBound,
        max: AppSettings.shared.turnSpeedRange.upperBound,
        format: "%.0f PWM")
    
    // Mode toggles
    private let autoModeSwitch  = LabeledSwitch(title: "Auto-arm",
                                                detail: "Robot starts balancing automatically when held upright; recovers after a fall.")
    private let yawLockSwitch   = LabeledSwitch(title: "Yaw lock (Hold heading)",
                                                detail: "Locks heading using gyro-Z so the robot doesn't drift sideways on its own.")
    
    // Trim
    private let trimLabel  = UILabel()
    private let trimMinus  = UIButton(type: .system)
    private let trimZero   = UIButton(type: .system)
    private let trimPlus   = UIButton(type: .system)
    private var currentTrimFine: Float = 0.0
    
    // Speak
    private let speakField  = UITextField()
    private let speakButton = UIButton(type: .system)
    
    // Debug log
    private let logView = UITextView()
    
    // MARK: - State
    private let bluetoothService = BluetoothService.shared
    
    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .compatScreenBackground
        
        if #available(iOS 13.0, *) {
            navigationController?.navigationBar.prefersLargeTitles = false
        }
        
        buildUI()
        wireUp()
    }
    
    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        bluetoothService.delegate = self
        // Pi'den güncel PID değerlerini iste
        let payload = Data([0x00])
        if BluetoothService.shared.peripheral != nil {
            bluetoothService.requestData(msgId: mPP, data: payload)
            bluetoothService.requestData(msgId: mPI, data: payload)
            bluetoothService.requestData(msgId: mPD, data: payload)
            bluetoothService.requestData(msgId: mAC, data: payload)
            bluetoothService.requestData(msgId: mSD, data: payload)
            bluetoothService.requestData(msgId: mSpdKp, data: payload)
            bluetoothService.requestData(msgId: mSpdKi, data: payload)
            bluetoothService.requestData(msgId: mSpdMaxTilt, data: payload)
            bluetoothService.requestData(msgId: mSpdMaxVel, data: payload)
            bluetoothService.requestData(msgId: mAutoMode, data: payload)
            bluetoothService.requestData(msgId: mPositionHold, data: payload)
        }
    }
    
    private func buildUI() {
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.alwaysBounceVertical = true
        view.addSubview(scrollView)
        
        contentStack.axis = .vertical
        contentStack.spacing = 18
        contentStack.alignment = .fill
        contentStack.translatesAutoresizingMaskIntoConstraints = false
        scrollView.addSubview(contentStack)
        
        let g = view.safeAreaLayoutGuide
        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: g.topAnchor),
            scrollView.leadingAnchor.constraint(equalTo: g.leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: g.trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: g.bottomAnchor),
            
            contentStack.topAnchor.constraint(equalTo: scrollView.topAnchor, constant: 16),
            contentStack.leadingAnchor.constraint(equalTo: scrollView.leadingAnchor, constant: 16),
            contentStack.trailingAnchor.constraint(equalTo: scrollView.trailingAnchor, constant: -16),
            contentStack.bottomAnchor.constraint(equalTo: scrollView.bottomAnchor, constant: -16),
            contentStack.widthAnchor.constraint(equalTo: scrollView.widthAnchor, constant: -32),
        ])
        
        // Section: PID
        contentStack.addArrangedSubview(makeCard(title: "PITCH PID", subviews: [pSlider, iSlider, dSlider]))
        
        // Section: Speed PID (outer loop — controls velocity by adjusting target tilt)
        contentStack.addArrangedSubview(makeCard(title: "SPEED PID",
                                                 subviews: [spdKpSlider, spdKiSlider, spdMaxTiltSlider, spdMaxVelSlider]))
        
        // Section: Stabilization
        contentStack.addArrangedSubview(makeCard(title: "STABILIZATION", subviews: [acSlider, sdSlider]))
        
        // Section: Joystick speeds (iOS only)
        contentStack.addArrangedSubview(makeCard(title: "JOYSTICK SPEEDS",
                                                 subviews: [forwardSpeedSlider, turnSpeedSlider]))
        
        // Section: Modes
        contentStack.addArrangedSubview(makeCard(title: "MODES", subviews: [autoModeSwitch, yawLockSwitch]))
        
        // Section: Fine trim
        contentStack.addArrangedSubview(makeCard(title: "FINE TRIM", subviews: [buildTrimRow()]))
        
        // Section: Speak
        contentStack.addArrangedSubview(makeCard(title: "TEXT TO SPEECH", subviews: [buildSpeakRow()]))
        
        // Section: Debug log (NOT inside a card — let the dark bg show fully)
        let logHeader = makeHeader("DEBUG LOG")
        let logContainer = UIView()
        logContainer.translatesAutoresizingMaskIntoConstraints = false
        configureLogView()
        logView.translatesAutoresizingMaskIntoConstraints = false
        logContainer.addSubview(logHeader)
        logContainer.addSubview(logView)
        logHeader.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            logHeader.topAnchor.constraint(equalTo: logContainer.topAnchor),
            logHeader.leadingAnchor.constraint(equalTo: logContainer.leadingAnchor, constant: 4),
            logHeader.trailingAnchor.constraint(equalTo: logContainer.trailingAnchor, constant: -4),
            
            logView.topAnchor.constraint(equalTo: logHeader.bottomAnchor, constant: 8),
            logView.leadingAnchor.constraint(equalTo: logContainer.leadingAnchor),
            logView.trailingAnchor.constraint(equalTo: logContainer.trailingAnchor),
            logView.bottomAnchor.constraint(equalTo: logContainer.bottomAnchor),
            logView.heightAnchor.constraint(equalToConstant: 180),
        ])
        contentStack.addArrangedSubview(logContainer)
        
        // Reset to defaults button — last thing, separated by extra space
        let resetButton = UIButton(type: .system)
        resetButton.setTitle("Reset all settings to defaults", for: .normal)
        resetButton.titleLabel?.font = .systemFont(ofSize: 15, weight: .semibold)
        resetButton.setTitleColor(.white, for: .normal)
        resetButton.backgroundColor = .compatSystemRed
        resetButton.layer.cornerRadius = 10
        resetButton.translatesAutoresizingMaskIntoConstraints = false
        resetButton.heightAnchor.constraint(equalToConstant: 50).isActive = true
        resetButton.addTarget(self, action: #selector(resetTapped), for: .touchUpInside)
        contentStack.addArrangedSubview(resetButton)
    }
    
    /// Bir bölümü kart şeklinde sarar: başlık + alt başlık + içerik kutusu.
    private func makeCard(title: String, subviews: [UIView]) -> UIView {
        let outer = UIStackView()
        outer.axis = .vertical
        outer.spacing = 6
        outer.alignment = .fill
        
        let header = makeHeader(title)
        outer.addArrangedSubview(header)
        
        let card = UIView()
        card.backgroundColor = .compatCardBackground
        card.layer.cornerRadius = 12
        card.layer.shadowColor = UIColor.black.cgColor
        card.layer.shadowOpacity = 0.15
        card.layer.shadowOffset = CGSize(width: 0, height: 2)
        card.layer.shadowRadius = 6
        
        let inner = UIStackView(arrangedSubviews: subviews)
        inner.axis = .vertical
        inner.spacing = 14
        inner.alignment = .fill
        inner.translatesAutoresizingMaskIntoConstraints = false
        card.addSubview(inner)
        NSLayoutConstraint.activate([
            inner.topAnchor.constraint(equalTo: card.topAnchor, constant: 14),
            inner.leadingAnchor.constraint(equalTo: card.leadingAnchor, constant: 14),
            inner.trailingAnchor.constraint(equalTo: card.trailingAnchor, constant: -14),
            inner.bottomAnchor.constraint(equalTo: card.bottomAnchor, constant: -14),
        ])
        
        outer.addArrangedSubview(card)
        return outer
    }
    
    private func makeHeader(_ text: String) -> UILabel {
        let l = UILabel()
        l.text = text
        l.font = .systemFont(ofSize: 12, weight: .semibold)
        l.textColor = .gray
        l.setContentHuggingPriority(.required, for: .vertical)
        return l
    }
    
    private func buildTrimRow() -> UIView {
        trimLabel.text = "Trim: +0.00°"
        trimLabel.font = .systemFont(ofSize: 15, weight: .medium)
        trimLabel.textColor = .compatPrimaryText
        trimLabel.textAlignment = .center
        
        configureSmallButton(trimMinus, title: "−0.1°", sel: #selector(trimDec))
        configureSmallButton(trimZero,  title: "Reset", sel: #selector(trimReset))
        configureSmallButton(trimPlus,  title: "+0.1°", sel: #selector(trimInc))
        
        let row = UIStackView(arrangedSubviews: [trimMinus, trimZero, trimPlus])
        row.axis = .horizontal
        row.spacing = 8
        row.distribution = .fillEqually
        
        let v = UIStackView(arrangedSubviews: [trimLabel, row])
        v.axis = .vertical
        v.spacing = 8
        return v
    }
    
    private func configureSmallButton(_ b: UIButton, title: String, sel: Selector) {
        b.setTitle(title, for: .normal)
        b.titleLabel?.font = .systemFont(ofSize: 14, weight: .medium)
        b.setTitleColor(.white, for: .normal)
        b.backgroundColor = .compatSystemBlue
        b.layer.cornerRadius = 8
        b.heightAnchor.constraint(equalToConstant: 38).isActive = true
        b.addTarget(self, action: sel, for: .touchUpInside)
    }
    
    private func buildSpeakRow() -> UIView {
        speakField.borderStyle = .roundedRect
        speakField.placeholder = "Text to speak"
        speakField.returnKeyType = .send
        speakField.delegate = self
        
        speakButton.setTitle("Speak", for: .normal)
        speakButton.titleLabel?.font = .systemFont(ofSize: 14, weight: .semibold)
        speakButton.setTitleColor(.white, for: .normal)
        speakButton.backgroundColor = .compatSystemBlue
        speakButton.layer.cornerRadius = 8
        speakButton.heightAnchor.constraint(equalToConstant: 38).isActive = true
        speakButton.widthAnchor.constraint(equalToConstant: 80).isActive = true
        speakButton.addTarget(self, action: #selector(speakTapped), for: .touchUpInside)
        
        let row = UIStackView(arrangedSubviews: [speakField, speakButton])
        row.axis = .horizontal
        row.spacing = 8
        row.alignment = .center
        return row
    }
    
    private func configureLogView() {
        logView.isEditable = false
        logView.backgroundColor = UIColor(red: 0.10, green: 0.10, blue: 0.12, alpha: 1.0)   // ofis-koyu
        logView.textColor = UIColor(red: 0.60, green: 0.95, blue: 0.60, alpha: 1.0)         // terminal yeşili
        if #available(iOS 13.0, *) {
            logView.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
        } else {
            logView.font = UIFont(name: "Menlo", size: 11) ?? .systemFont(ofSize: 11)
        }
        logView.layer.cornerRadius = 10
        logView.layer.borderColor = UIColor(white: 0.20, alpha: 1.0).cgColor
        logView.layer.borderWidth = 1
        logView.textContainerInset = UIEdgeInsets(top: 8, left: 8, bottom: 8, right: 8)
        logView.text = ""
    }
    
    private func appendLog(_ s: String) {
        DispatchQueue.main.async {
            self.logView.text += s + "\n"
            // En alta kaydır
            let bottom = NSMakeRange(self.logView.text.count - 1, 1)
            self.logView.scrollRangeToVisible(bottom)
        }
    }
    
    // MARK: - Wiring
    
    private func wireUp() {
        pSlider.onValueChanged  = { [weak self] v in self?.sendByte(mPP, byte: UInt8(min(255, max(0, Int(v))))) }
        iSlider.onValueChanged  = { [weak self] v in self?.sendByte(mPI, byte: UInt8(min(255, max(0, Int(v))))) }
        dSlider.onValueChanged  = { [weak self] v in self?.sendByte(mPD, byte: UInt8(min(255, max(0, Int(v))))) }
        acSlider.onValueChanged = { [weak self] v in self?.sendByte(mAC, byte: UInt8(min(255, max(0, Int(v))))) }
        sdSlider.onValueChanged = { [weak self] v in self?.sendByte(mSD, byte: UInt8(min(255, max(0, Int(v))))) }
        
        // Speed PID — sliders show real units, send raw int (scaled by Pi):
        //   spdKp:      slider 0..50  → byte 0..50  → Pi /100  → 0.00..0.50
        //   spdKi:      slider 0..100 → byte 0..100 → Pi /100  → 0.00..1.00
        //   spdMaxTilt: slider 1..15  → byte 1..15  → Pi raw   → 1..15°
        //   spdMaxVel:  slider 10..80 → byte 10..80 → Pi /10   → 1.0..8.0
        spdKpSlider.onValueChanged      = { [weak self] v in self?.sendByte(mSpdKp,      byte: UInt8(min(255, max(0, Int(v))))) }
        spdKiSlider.onValueChanged      = { [weak self] v in self?.sendByte(mSpdKi,      byte: UInt8(min(255, max(0, Int(v))))) }
        spdMaxTiltSlider.onValueChanged = { [weak self] v in self?.sendByte(mSpdMaxTilt, byte: UInt8(min(255, max(0, Int(v))))) }
        spdMaxVelSlider.onValueChanged  = { [weak self] v in self?.sendByte(mSpdMaxVel,  byte: UInt8(min(255, max(0, Int(v))))) }
        
        autoModeSwitch.onChanged = { [weak self] on in self?.sendByte(mAutoMode,     byte: on ? 1 : 0) }
        yawLockSwitch.onChanged  = { [weak self] on in self?.sendByte(mPositionHold, byte: on ? 1 : 0) }
        
        // Joystick speeds — store in AppSettings (UserDefaults).
        // Initialize with current persisted values so the UI matches what
        // the joystick will actually use.
        forwardSpeedSlider.setValue(AppSettings.shared.forwardSpeed)
        turnSpeedSlider.setValue(AppSettings.shared.turnSpeed)
        forwardSpeedSlider.onValueChanged = { v in AppSettings.shared.forwardSpeed = v }
        turnSpeedSlider.onValueChanged    = { v in AppSettings.shared.turnSpeed    = v }
    }
    
    private func sendByte(_ msgId: Byte, byte: UInt8) {
        var v = byte
        let d = Data(bytes: &v, count: 1)
        bluetoothService.sendCommand(msgId: msgId, data: d)
    }
    
    // MARK: - Trim
    
    @objc private func trimInc()   { currentTrimFine += 0.1; sendTrim() }
    @objc private func trimDec()   { currentTrimFine -= 0.1; sendTrim() }
    @objc private func trimReset() {
        currentTrimFine = 0.0
        sendTrim()
        sendByte(mResetTrim, byte: 0)
    }
    private func sendTrim() {
        trimLabel.text = String(format: "Trim: %+0.2f°", currentTrimFine)
        let raw = Int16((currentTrimFine * 100).rounded())
        let d = Data.bePackInt16(raw)
        bluetoothService.sendCommand(msgId: mTrimFine, data: d)
    }
    
    // MARK: - Speak
    
    @objc private func speakTapped() {
        guard let t = speakField.text, !t.isEmpty else { return }
        let d = Data(t.utf8)
        bluetoothService.sendCommand(msgId: mSpeak, data: d)
        speakField.resignFirstResponder()
    }
    
    // MARK: - Reset to defaults
    //
    // Robot-side defaults are taken from robotcontrol.h:
    //   Kp = 25, Ki = 40 (UI scale → 0.4 real), Kd = 0.10
    //   AC = 0.0  → slider 0   (×10)
    //   SD = 2.0  → slider 20  (×10)
    //   spdKp = 0.12 → slider 12 (×100)
    //   spdKi = 0.20 → slider 20 (×100)
    //   spdMaxTilt = 5°  → slider 5
    //   spdMaxVel  = 3.0 → slider 30 (×10)
    // iOS-side defaults from AppSettings:
    //   forwardSpeed = 180, turnSpeed = 60
    
    @objc private func resetTapped() {
        let alert = UIAlertController(
            title: "Reset to defaults?",
            message: "All PID values, stabilization parameters, modes and joystick speeds will be restored to their original defaults.",
            preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        alert.addAction(UIAlertAction(title: "Reset", style: .destructive) { [weak self] _ in
            self?.performReset()
        })
        present(alert, animated: true)
    }
    
    private func performReset() {
        appendLog("Resetting to defaults...")
        
        // 1) Pi side — send each default via BLE. Sliders update themselves
        //    locally and the Pi will echo back the new values via mPP/mPI/...
        //    Slider raw values match the byte values sent over BLE:
        //      mPP byte = Kp                (Kp 25  → byte 25)
        //      mPI byte = Ki × 100          (Ki 0.4 → byte 40)
        //      mPD byte = Kd × 100          (Kd 0.10 → byte 10)
        //      mAC byte = AC × 10           (AC 0   → byte 0)
        //      mSD byte = SD × 10           (SD 2.0 → byte 20)
        //
        // These values match the Pi-side loadSettings() defaults so a
        // fresh Pi (no settings.ini) and an iOS reset both end up at
        // the same point.
        let robotDefaults: [(Byte, UInt8, Float)] = [
            (mPP, 25,  25),    // Kp = 25
            (mPI, 40,  40),    // Ki = 0.4 (real); slider raw 40
            (mPD, 10,  10),    // Kd = 0.10
            (mAC, 0,   0),     // AC = 0
            (mSD, 20,  20),    // SD = 2.0
            (mSpdKp,      12, 12), // Speed Kp = 0.12   (slider raw 12 → /100)
            (mSpdKi,      20, 20), // Speed Ki = 0.20   (slider raw 20 → /100)
            (mSpdMaxTilt, 5,  5),  // Speed Max Tilt = 5°
            (mSpdMaxVel,  30, 30), // Speed Max Vel = 3.0 (slider raw 30 → /10)
        ]
        for (cmd, byte, sliderValue) in robotDefaults {
            sendByte(cmd, byte: byte)
            switch cmd {
                case mPP: pSlider.setValue(sliderValue)
                case mPI: iSlider.setValue(sliderValue)
                case mPD: dSlider.setValue(sliderValue)
                case mAC: acSlider.setValue(sliderValue)
                case mSD: sdSlider.setValue(sliderValue)
                case mSpdKp:      spdKpSlider.setValue(sliderValue)
                case mSpdKi:      spdKiSlider.setValue(sliderValue)
                case mSpdMaxTilt: spdMaxTiltSlider.setValue(sliderValue)
                case mSpdMaxVel:  spdMaxVelSlider.setValue(sliderValue)
                default: break
            }
        }
        
        // Mode defaults — Auto-arm ON, yaw lock ON (matches robot defaults)
        sendByte(mAutoMode,     byte: 1)
        sendByte(mPositionHold, byte: 1)
        autoModeSwitch.isOn = true
        yawLockSwitch.isOn  = true
        
        // Clear learned + manual trim on Pi
        currentTrimFine = 0.0
        let raw = Int16(0)
        bluetoothService.sendCommand(msgId: mTrimFine, data: Data.bePackInt16(raw))
        sendByte(mResetTrim, byte: 0)
        trimLabel.text = "Trim: +0.00°"
        
        // 2) iOS side — joystick speeds back to defaults
        AppSettings.shared.forwardSpeed = 180
        AppSettings.shared.turnSpeed    = 60
        forwardSpeedSlider.setValue(180)
        turnSpeedSlider.setValue(60)
        
        appendLog("Reset complete.")
    }
}

// MARK: - TextField delegate
extension SettingsViewController: UITextFieldDelegate {
    func textFieldShouldReturn(_ textField: UITextField) -> Bool {
        speakTapped()
        return true
    }
}

// MARK: - Bluetooth delegate
extension SettingsViewController: BluetoothServiceDelegate {
    func didReceiveIPAddress(_ ipAddress: String) {
        appendLog("IP: \(ipAddress)")
    }
    func didReceiveMessage(_ message: String) {
        appendLog(message)
    }
    func didUpdateRobotArmedState(_ armed: Bool) {
        appendLog(armed ? "Robot ARMED" : "Robot DISARMED")
    }
    func didReceiveTelemetry(_ telemetry: RobotTelemetry) {
        // Settings sekmesindeyken telemetri spam'i istemiyoruz; log'da yok.
        // ControlViewController bunu zaten gösteriyor.
    }
    func didReceiveSettingValue(command: Byte, rawValue: UInt8) {
        // Pi'den gelen ayar değerini ilgili UI'a yansıt.
        DispatchQueue.main.async {
            let v = Float(rawValue)
            switch command {
                case mPP: self.pSlider.setValue(v)
                case mPI: self.iSlider.setValue(v)
                case mPD: self.dSlider.setValue(v)
                case mAC: self.acSlider.setValue(v)
                case mSD: self.sdSlider.setValue(v)
                case mSpdKp:      self.spdKpSlider.setValue(v)
                case mSpdKi:      self.spdKiSlider.setValue(v)
                case mSpdMaxTilt: self.spdMaxTiltSlider.setValue(v)
                case mSpdMaxVel:  self.spdMaxVelSlider.setValue(v)
                case mAutoMode:     self.autoModeSwitch.isOn = (rawValue != 0)
                case mPositionHold: self.yawLockSwitch.isOn  = (rawValue != 0)
                default: break
            }
        }
    }
}

// MARK: - Helper components

/// Tek satırlık başlık + slider + değer etiketi.
final class LabeledSlider: UIView {
    private let titleLabel = UILabel()
    private let valueLabel = UILabel()
    private let slider     = UISlider()
    
    private let format: String
    private let scaleDivider: Float
    
    /// onValueChanged: ham slider değerini (0..max) verir — UI formatlamadan önce.
    /// Yani PID slider 0..100 ise event'te 0..100 gelir.
    var onValueChanged: ((Float) -> Void)?
    
    /// İçeride dragging sırasında her değişimde BLE'yi su basmamak için throttle.
    private var lastSentValue: Float = -1
    private var lastSentAt = Date.distantPast
    private let minIntervalMs: Double = 100  // 10 Hz max
    
    init(title: String, min: Float, max: Float, format: String, scaleDivider: Float = 1.0) {
        self.format = format
        self.scaleDivider = scaleDivider
        super.init(frame: .zero)
        
        titleLabel.text = title
        titleLabel.font = .systemFont(ofSize: 14, weight: .medium)
        titleLabel.textColor = .compatPrimaryText
        valueLabel.font = .systemFont(ofSize: 13, weight: .medium)
        valueLabel.textAlignment = .right
        valueLabel.textColor = UIColor(white: 0.65, alpha: 1.0)
        
        slider.minimumValue = min
        slider.maximumValue = max
        slider.addTarget(self, action: #selector(changed), for: .valueChanged)
        slider.addTarget(self, action: #selector(touchUp),  for: [.touchUpInside, .touchUpOutside])
        
        let top = UIStackView(arrangedSubviews: [titleLabel, valueLabel])
        top.axis = .horizontal
        top.distribution = .fill
        valueLabel.setContentHuggingPriority(.required, for: .horizontal)
        
        let v = UIStackView(arrangedSubviews: [top, slider])
        v.axis = .vertical
        v.spacing = 4
        v.translatesAutoresizingMaskIntoConstraints = false
        addSubview(v)
        NSLayoutConstraint.activate([
            v.topAnchor.constraint(equalTo: topAnchor),
            v.leadingAnchor.constraint(equalTo: leadingAnchor),
            v.trailingAnchor.constraint(equalTo: trailingAnchor),
            v.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])
        updateLabel()
    }
    
    required init?(coder: NSCoder) { fatalError() }
    
    func setValue(_ raw: Float) {
        slider.value = raw
        updateLabel()
        lastSentValue = raw
    }
    
    private func updateLabel() {
        let scaled = slider.value / scaleDivider
        valueLabel.text = String(format: format, scaled)
    }
    
    @objc private func changed() {
        updateLabel()
        // Throttle while dragging
        let now = Date()
        let dt = now.timeIntervalSince(lastSentAt) * 1000
        if dt < minIntervalMs && abs(slider.value - lastSentValue) < 0.5 { return }
        lastSentAt = now
        lastSentValue = slider.value
        onValueChanged?(slider.value)
    }
    
    @objc private func touchUp() {
        // Son değer mutlaka yollansın
        lastSentValue = slider.value
        lastSentAt = Date()
        onValueChanged?(slider.value)
    }
}

/// Satır halinde başlık, açıklama ve UISwitch.
final class LabeledSwitch: UIView {
    private let titleLabel = UILabel()
    private let detailLabel = UILabel()
    private let toggle      = UISwitch()
    
    var onChanged: ((Bool) -> Void)?
    
    init(title: String, detail: String) {
        super.init(frame: .zero)
        
        titleLabel.text = title
        titleLabel.font = .systemFont(ofSize: 15, weight: .medium)
        titleLabel.textColor = .compatPrimaryText
        detailLabel.text = detail
        detailLabel.font = .systemFont(ofSize: 12)
        detailLabel.textColor = UIColor(white: 0.60, alpha: 1.0)
        detailLabel.numberOfLines = 0
        
        toggle.addTarget(self, action: #selector(changed), for: .valueChanged)
        toggle.translatesAutoresizingMaskIntoConstraints = false
        
        let left = UIStackView(arrangedSubviews: [titleLabel, detailLabel])
        left.axis = .vertical
        left.spacing = 2
        
        let row = UIStackView(arrangedSubviews: [left, toggle])
        row.axis = .horizontal
        row.alignment = .center
        row.spacing = 12
        row.translatesAutoresizingMaskIntoConstraints = false
        addSubview(row)
        NSLayoutConstraint.activate([
            row.topAnchor.constraint(equalTo: topAnchor),
            row.leadingAnchor.constraint(equalTo: leadingAnchor),
            row.trailingAnchor.constraint(equalTo: trailingAnchor),
            row.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])
    }
    
    required init?(coder: NSCoder) { fatalError() }
    
    var isOn: Bool {
        get { return toggle.isOn }
        set { toggle.setOn(newValue, animated: false) }
    }
    
    @objc private func changed() {
        onChanged?(toggle.isOn)
    }
}

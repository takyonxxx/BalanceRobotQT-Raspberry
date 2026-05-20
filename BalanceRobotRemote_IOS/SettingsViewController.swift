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
    
    // PID slider'ları
    private let pSlider = LabeledSlider(title: "P (Proportional)", min: 0, max: 100, format: "%.0f")
    private let iSlider = LabeledSlider(title: "I (Integral)",     min: 0, max: 255, format: "%.2f", scaleDivider: 100)
    private let dSlider = LabeledSlider(title: "D (Derivative)",   min: 0, max: 50,  format: "%.2f", scaleDivider: 10)
    private let acSlider = LabeledSlider(title: "Angle trim (AC)", min: 0, max: 50,  format: "%.1f°", scaleDivider: 10)
    private let sdSlider = LabeledSlider(title: "Yaw gain (SD)",   min: 0, max: 100, format: "%.2f", scaleDivider: 10)
    
    // Mode toggles
    private let autoModeSwitch  = LabeledSwitch(title: "Auto-arm",
                                                detail: "Robot dik tutulduğunda kendiliğinden balansa başlar, düşse de kalkar.")
    private let yawLockSwitch   = LabeledSwitch(title: "Yaw lock (Hold heading)",
                                                detail: "Robot kendiliğinden dönmesin diye gyro-Z ile doğrultuyu kilitler.")
    
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
        if #available(iOS 13.0, *) {
            view.backgroundColor = .systemBackground
        } else {
            view.backgroundColor = .white
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
            bluetoothService.requestData(msgId: mAutoMode, data: payload)
            bluetoothService.requestData(msgId: mPositionHold, data: payload)
        }
    }
    
    private func buildUI() {
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(scrollView)
        
        contentStack.axis = .vertical
        contentStack.spacing = 16
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
        
        // Bölüm: PID
        contentStack.addArrangedSubview(makeHeader("PID Tuning"))
        contentStack.addArrangedSubview(pSlider)
        contentStack.addArrangedSubview(iSlider)
        contentStack.addArrangedSubview(dSlider)
        
        // Bölüm: Stabilizasyon
        contentStack.addArrangedSubview(makeHeader("Stabilization"))
        contentStack.addArrangedSubview(acSlider)
        contentStack.addArrangedSubview(sdSlider)
        
        // Bölüm: Modlar
        contentStack.addArrangedSubview(makeHeader("Modes"))
        contentStack.addArrangedSubview(autoModeSwitch)
        contentStack.addArrangedSubview(yawLockSwitch)
        
        // Bölüm: Fine trim
        contentStack.addArrangedSubview(makeHeader("Fine Trim"))
        contentStack.addArrangedSubview(buildTrimRow())
        
        // Bölüm: Speak
        contentStack.addArrangedSubview(makeHeader("Text to Speech"))
        contentStack.addArrangedSubview(buildSpeakRow())
        
        // Bölüm: Debug log
        contentStack.addArrangedSubview(makeHeader("Debug Log"))
        configureLogView()
        contentStack.addArrangedSubview(logView)
        logView.heightAnchor.constraint(equalToConstant: 160).isActive = true
    }
    
    private func makeHeader(_ text: String) -> UILabel {
        let l = UILabel()
        l.text = text.uppercased()
        l.font = .systemFont(ofSize: 13, weight: .semibold)
        l.textColor = .gray
        return l
    }
    
    private func buildTrimRow() -> UIView {
        trimLabel.text = "Trim: +0.00°"
        trimLabel.font = .systemFont(ofSize: 15, weight: .medium)
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
        speakField.placeholder = "Robot'a söyleyeceğin metin"
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
        logView.backgroundColor = .black
        logView.textColor = .green
        if #available(iOS 13.0, *) {
            logView.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
        } else {
            logView.font = UIFont(name: "Menlo", size: 11) ?? .systemFont(ofSize: 11)
        }
        logView.layer.cornerRadius = 8
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
        
        autoModeSwitch.onChanged = { [weak self] on in self?.sendByte(mAutoMode,     byte: on ? 1 : 0) }
        yawLockSwitch.onChanged  = { [weak self] on in self?.sendByte(mPositionHold, byte: on ? 1 : 0) }
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
        valueLabel.font = .systemFont(ofSize: 13)
        valueLabel.textAlignment = .right
        valueLabel.textColor = .darkGray
        
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
        detailLabel.text = detail
        detailLabel.font = .systemFont(ofSize: 12)
        detailLabel.textColor = .gray
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

// MARK: - PIDSettings shim
//
// Eski Pi BluetoothEventsHandler PIDSettings.shared.setPValue() vs çağırıyor.
// Bu sınıf artık görsel modal değil, Settings sekmesinin slider'larına yönlendirir.
class PIDSettings {
    static let shared = PIDSettings()
    
    weak var settingsVC: SettingsViewController?
    
    func setPValue(value: Float)  { /* opsiyonel: settings slider güncelleme */ }
    func setIValue(value: Float)  { }
    func setDValue(value: Float)  { }
    func setACValue(value: Float) { }
    func setSDValue(value: Float) { }
    
    // Eski API uyumluluğu: ViewController.show(animated:) çağırıyordu.
    func show(animated: Bool) { /* no-op — Settings sekmesi zaten görünür */ }
}

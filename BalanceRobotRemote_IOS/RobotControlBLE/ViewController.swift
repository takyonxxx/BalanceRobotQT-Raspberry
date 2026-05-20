import UIKit
import CoreBluetooth

class ViewController: UIViewController {
    
    // MARK: - Storyboard outlets (mevcut)
    @IBOutlet weak var statusLabel: UILabel!
    @IBOutlet weak var exitButton: UIButton!
    @IBOutlet weak var connectButton: UIButton!
    @IBOutlet weak var textBrowser: UITextView!
    @IBOutlet var editSpeak: UITextField!
    
    @IBOutlet weak var rightButton: UIButton!
    @IBOutlet weak var leftButton: UIButton!
    @IBOutlet weak var forwardButton: UIButton!
    @IBOutlet weak var backwardButton: UIButton!
    @IBOutlet var speakButton: UIButton!
    
    // MARK: - Programmatic additions
    private let joystick = JoystickView()
    private let telemetryView = TelemetryView()
    private let armButton    = UIButton(type: .system)
    private let autoModeBtn  = UIButton(type: .system)
    private let posHoldBtn   = UIButton(type: .system)
    private let trimMinus    = UIButton(type: .system)
    private let trimPlus     = UIButton(type: .system)
    private let trimZero     = UIButton(type: .system)
    
    let backgroundForward  = UIImage(named: "Images/forward.png")
    let backgroundBackward = UIImage(named: "Images/backward.png")
    let backgroundLeft     = UIImage(named: "Images/left.png")
    let backgroundRight    = UIImage(named: "Images/right.png")
    
    let bluetoothService = BluetoothService.shared
    lazy var pairingFlow = PairingFlow(bluetoothService: self.bluetoothService)
    
    // Komut throttling - BLE'yi su basmamak için
    private var lastSpeedCmd: Int = 0
    private var lastTurnCmd: Int = 0
    private var lastCommandSent = Date.distantPast
    private let commandIntervalMs = 50    // 20 Hz max
    
    // Trim state (Pi tarafına signed int16 olarak gönderilir)
    private var currentTrimFine: Float = 0.0
    
    // Local state mirrored from telemetry
    private var isArmedRemote = false
    private var isAutoMode    = true
    private var isPositionHold = true
    
    let speedConstant = 0x32
    
    override func viewDidLoad() {
        super.viewDidLoad()
        bluetoothService.delegate = self
        
        setupBluetoothService()
        configureButtons()
        configureTextBrowser()
        addProgrammaticUI()
    }
    
    private func setupBluetoothService() {
        self.bluetoothService.flowController = self.pairingFlow
    }
    
    private func configureButtons() {
        configureDirectionButton(rightButton, image: backgroundRight, downAction: #selector(buttonRightHoldDown), releaseAction: #selector(buttonRightHoldRelease))
        configureDirectionButton(leftButton, image: backgroundLeft, downAction: #selector(buttonLeftHoldDown), releaseAction: #selector(buttonLeftHoldRelease))
        configureDirectionButton(forwardButton, image: backgroundForward, downAction: #selector(buttonForwardHoldDown), releaseAction: #selector(buttonForwardHoldRelease))
        configureDirectionButton(backwardButton, image: backgroundBackward, downAction: #selector(buttonBackwardHoldDown), releaseAction: #selector(buttonBackwardHoldRelease))
        
        connectButton.layer.cornerRadius = 8
        exitButton.layer.cornerRadius = 8
        speakButton.layer.cornerRadius = 8
    }
    
    private func configureDirectionButton(_ button: UIButton, image: UIImage?, downAction: Selector, releaseAction: Selector) {
        button.setTitle("", for: .normal)
        button.showsTouchWhenHighlighted = true
        
        if let backgroundImage = image {
            button.setBackgroundImage(backgroundImage, for: .normal)
            button.contentMode = .scaleAspectFit
            button.imageView?.contentMode = .scaleAspectFit
        }
        button.layer.cornerRadius = 8
        button.clipsToBounds = true
        
        button.addTarget(self, action: downAction, for: .touchDown)
        button.addTarget(self, action: releaseAction, for: .touchUpInside)
        button.addTarget(self, action: releaseAction, for: .touchUpOutside)
    }
    
    private func configureTextBrowser() {
        textBrowser.setContentOffset(.zero, animated: true)
        textBrowser.isEditable = false
        textBrowser.layer.cornerRadius = 8
        textBrowser.clipsToBounds = true
    }
    
    // MARK: - Programmatic UI
    
    private func addProgrammaticUI() {
        // Storyboard yön tuşları — joystick zaten o işlevi yapıyor, hepsini gizle.
        // Storyboard'u editlemeden bu butonları görünmez yapıyoruz.
        forwardButton?.isHidden = true
        backwardButton?.isHidden = true
        leftButton?.isHidden = true
        rightButton?.isHidden = true
        
        // textBrowser (log) görünümünü düzelt — siyah arka plan, yeşil mono yazı.
        // Storyboard'ta beyaz arka plan ayarlanmış olabilir; burada override.
        textBrowser?.backgroundColor = .black
        textBrowser?.textColor = .green
        if #available(iOS 13.0, *) {
            textBrowser?.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
        } else {
            textBrowser?.font = UIFont(name: "Menlo", size: 11) ?? .systemFont(ofSize: 11)
        }
        
        // Telemetri paneli
        telemetryView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(telemetryView)
        
        // Joystick — sağ alt
        joystick.translatesAutoresizingMaskIntoConstraints = false
        joystick.delegate = self
        view.addSubview(joystick)
        
        // Arm / Auto / PosHold butonları
        configureToggleButton(armButton,    title: "ARM",       sel: #selector(toggleArm))
        configureToggleButton(autoModeBtn,  title: "AUTO",      sel: #selector(toggleAuto))
        configureToggleButton(posHoldBtn,   title: "HOLD",      sel: #selector(togglePosHold))
        
        // Trim butonları
        configureSmallButton(trimMinus, title: "Trim −",  sel: #selector(trimDecrement))
        configureSmallButton(trimPlus,  title: "Trim +",  sel: #selector(trimIncrement))
        configureSmallButton(trimZero,  title: "Trim 0",  sel: #selector(trimReset))
        
        let toggleStack = UIStackView(arrangedSubviews: [armButton, autoModeBtn, posHoldBtn])
        toggleStack.axis = .horizontal
        toggleStack.distribution = .fillEqually
        toggleStack.spacing = 6
        toggleStack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(toggleStack)
        
        let trimStack = UIStackView(arrangedSubviews: [trimMinus, trimZero, trimPlus])
        trimStack.axis = .horizontal
        trimStack.distribution = .fillEqually
        trimStack.spacing = 6
        trimStack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(trimStack)
        
        let g = view.safeAreaLayoutGuide
        NSLayoutConstraint.activate([
            // Telemetri üst — daha kompakt (130 px)
            telemetryView.topAnchor.constraint(equalTo: g.topAnchor, constant: 8),
            telemetryView.leadingAnchor.constraint(equalTo: g.leadingAnchor, constant: 12),
            telemetryView.trailingAnchor.constraint(equalTo: g.trailingAnchor, constant: -12),
            telemetryView.heightAnchor.constraint(equalToConstant: 130),
            
            // Toggle butonları telemetri altında
            toggleStack.topAnchor.constraint(equalTo: telemetryView.bottomAnchor, constant: 8),
            toggleStack.leadingAnchor.constraint(equalTo: g.leadingAnchor, constant: 12),
            toggleStack.trailingAnchor.constraint(equalTo: g.trailingAnchor, constant: -12),
            toggleStack.heightAnchor.constraint(equalToConstant: 36),
            
            // Trim butonları toggle altında
            trimStack.topAnchor.constraint(equalTo: toggleStack.bottomAnchor, constant: 6),
            trimStack.leadingAnchor.constraint(equalTo: g.leadingAnchor, constant: 12),
            trimStack.trailingAnchor.constraint(equalTo: g.trailingAnchor, constant: -12),
            trimStack.heightAnchor.constraint(equalToConstant: 30),
            
            // Joystick sağ alt — biraz küçük ki ekrandaki diğer şeyleri ezmesin
            joystick.trailingAnchor.constraint(equalTo: g.trailingAnchor, constant: -16),
            joystick.bottomAnchor.constraint(equalTo: g.bottomAnchor, constant: -24),
            joystick.widthAnchor.constraint(equalToConstant: 160),
            joystick.heightAnchor.constraint(equalToConstant: 160),
        ])
        
        updateToggleAppearance()
    }
    
    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        // Programatik view'lar storyboard'un üstünde olsun — z-order önemli.
        view.bringSubviewToFront(telemetryView)
        view.bringSubviewToFront(armButton)
        view.bringSubviewToFront(autoModeBtn)
        view.bringSubviewToFront(posHoldBtn)
        view.bringSubviewToFront(trimMinus)
        view.bringSubviewToFront(trimZero)
        view.bringSubviewToFront(trimPlus)
        view.bringSubviewToFront(joystick)
    }
    
    private func configureToggleButton(_ b: UIButton, title: String, sel: Selector) {
        b.setTitle(title, for: .normal)
        b.titleLabel?.font = .systemFont(ofSize: 15, weight: .semibold)
        b.backgroundColor = UIColor.compatSystemGray4
        b.setTitleColor(.white, for: .normal)
        b.layer.cornerRadius = 8
        b.addTarget(self, action: sel, for: .touchUpInside)
    }
    
    private func configureSmallButton(_ b: UIButton, title: String, sel: Selector) {
        b.setTitle(title, for: .normal)
        b.titleLabel?.font = .systemFont(ofSize: 13)
        b.backgroundColor = UIColor.compatSystemGray5
        b.setTitleColor(.darkText, for: .normal)
        b.layer.cornerRadius = 6
        b.addTarget(self, action: sel, for: .touchUpInside)
    }
    
    private func updateToggleAppearance() {
        armButton.backgroundColor   = isArmedRemote    ? .compatSystemGreen : .compatSystemGray4
        autoModeBtn.backgroundColor = isAutoMode       ? .compatSystemBlue  : .compatSystemGray4
        posHoldBtn.backgroundColor  = isPositionHold   ? .compatSystemBlue  : .compatSystemGray4
    }
    
    // MARK: - Lifecycle
    
    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        checkBluetoothState()
    }
    
    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        view.frame = UIScreen.main.bounds
        view.layoutIfNeeded()
    }
    
    // MARK: - Bluetooth state
    
    private func checkBluetoothState() {
        if self.bluetoothService.bluetoothState != .poweredOn {
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) { self.checkBluetoothState() }
        }
        self.statusLabel.text = "Status: bluetooth is \(bluetoothService.bluetoothState == .poweredOn ? "ON" : "OFF")"
    }
    
    private func addToTextBrowser(text: String) {
        DispatchQueue.main.async {
            self.textBrowser.text += text + "\n"
            let bottom = NSMakeRange(self.textBrowser.text.count - 1, 1)
            self.textBrowser.scrollRangeToVisible(bottom)
        }
    }
    
    // MARK: - Connect
    
    @IBAction func connectButtonPressed(_ sender: Any) {
        self.statusLabel.text = "Status: bluetooth is \(bluetoothService.bluetoothState == .poweredOn ? "ON" : "OFF")"
        guard self.bluetoothService.bluetoothState == .poweredOn else { return }
        
        if self.connectButton.currentTitle == "Disconnect" {
            self.pairingFlow.cancel()
            self.connectButton.setTitle("Connect", for: .normal)
            self.textBrowser.text = nil
        } else {
            self.addToTextBrowser(text: "Status: waiting for peripheral...")
            self.pairingFlow.waitForPeripheral { [weak self] in
                guard let self = self else { return }
                self.addToTextBrowser(text: "Status: connecting...")
                self.pairingFlow.pair { result in
                    if let periperalName = self.bluetoothService.peripheral?.name {
                        self.addToTextBrowser(text: "Status: connected to: \(periperalName)")
                        self.connectButton.setTitle("Disconnect", for: .normal)
                        
                        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                            let payload = Data([0x00])
                            self.bluetoothService.requestData(msgId: mPP, data: payload)
                            self.bluetoothService.requestData(msgId: mPI, data: payload)
                            self.bluetoothService.requestData(msgId: mPD, data: payload)
                            self.bluetoothService.requestData(msgId: mAC, data: payload)
                            self.bluetoothService.requestData(msgId: mSD, data: payload)
                            self.bluetoothService.requestData(msgId: mArmed, data: payload)
                            self.bluetoothService.requestData(msgId: mAutoMode, data: payload)
                            self.bluetoothService.requestData(msgId: mPositionHold, data: payload)
                        }
                    } else if result {
                        self.addToTextBrowser(text: "Status: connected to peripheral")
                        self.connectButton.setTitle("Disconnect", for: .normal)
                    }
                }
            }
        }
    }
    
    @IBAction func buttonSpeakPressed(_ sender: Any) {
        guard let text = self.editSpeak.text, !text.isEmpty else { return }
        let data = Data(text.utf8)
        self.bluetoothService.sendCommand(msgId: mSpeak, data: data)
    }
    
    // MARK: - Direction buttons (mevcut storyboard butonları)
    
    @objc func buttonRightHoldDown() {
        sendOnce(msgId: mRight, value: UInt8(speedConstant))
    }
    @objc func buttonRightHoldRelease() {
        sendOnce(msgId: mRight, value: 0)
    }
    @objc func buttonLeftHoldDown() {
        sendOnce(msgId: mLeft, value: UInt8(speedConstant))
    }
    @objc func buttonLeftHoldRelease() {
        sendOnce(msgId: mLeft, value: 0)
    }
    @objc func buttonForwardHoldDown() {
        sendOnce(msgId: mForward, value: UInt8(speedConstant))
    }
    @objc func buttonForwardHoldRelease() {
        sendOnce(msgId: mForward, value: 0)
    }
    @objc func buttonBackwardHoldDown() {
        sendOnce(msgId: mBackward, value: UInt8(speedConstant))
    }
    @objc func buttonBackwardHoldRelease() {
        sendOnce(msgId: mBackward, value: 0)
    }
    
    private func sendOnce(msgId: Byte, value: UInt8) {
        var v = value
        let data = Data(bytes: &v, count: 1)
        self.bluetoothService.sendCommand(msgId: msgId, data: data)
    }
    
    // MARK: - New toggle actions
    
    @objc private func toggleArm() {
        let newState = !isArmedRemote
        isArmedRemote = newState
        sendOnce(msgId: newState ? mArmed : mDisArmed, value: 0)
        updateToggleAppearance()
    }
    
    @objc private func toggleAuto() {
        isAutoMode.toggle()
        sendOnce(msgId: mAutoMode, value: isAutoMode ? 1 : 0)
        updateToggleAppearance()
    }
    
    @objc private func togglePosHold() {
        isPositionHold.toggle()
        sendOnce(msgId: mPositionHold, value: isPositionHold ? 1 : 0)
        updateToggleAppearance()
    }
    
    @objc private func trimIncrement() {
        currentTrimFine += 0.1
        sendTrimFine()
    }
    @objc private func trimDecrement() {
        currentTrimFine -= 0.1
        sendTrimFine()
    }
    @objc private func trimReset() {
        currentTrimFine = 0.0
        sendTrimFine()
        sendOnce(msgId: mResetTrim, value: 0)
    }
    
    private func sendTrimFine() {
        // signed int16 BE, 0.01° çözünürlüklü
        let raw = Int16(round(currentTrimFine * 100))
        let data = Data.bePackInt16(raw)
        self.bluetoothService.sendCommand(msgId: mTrimFine, data: data)
    }
    
    // MARK: - Settings & exit
    
    @IBAction func buttonSettingsClicked(_ sender: Any) {
        PIDSettings.shared.show(animated: true)
        let payload = Data([0x00])
        self.bluetoothService.requestData(msgId: mPP, data: payload)
        self.bluetoothService.requestData(msgId: mPI, data: payload)
        self.bluetoothService.requestData(msgId: mPD, data: payload)
        self.bluetoothService.requestData(msgId: mAC, data: payload)
        self.bluetoothService.requestData(msgId: mSD, data: payload)
        self.bluetoothService.requestData(msgId: mArmed, data: payload)
    }
    
    @IBAction func clickedExitButton(_ sender: Any) {
        showMessageExitApp()
    }
    
    func showMessageExitApp() {
        let exitAppAlert = UIAlertController(title: "Exit From App",
                                           message: "Are you sure you want to exit?",
                                           preferredStyle: .alert)
        let resetApp = UIAlertAction(title: "Close Now", style: .destructive) { _ in
            self.pairingFlow.cancel()
            UIControl().sendAction(#selector(URLSessionTask.suspend), to: UIApplication.shared, for: nil)
            DispatchQueue.main.asyncAfter(deadline: .now() + .seconds(1), execute: {
                exit(EXIT_SUCCESS)
            })
        }
        let laterAction = UIAlertAction(title: "Later", style: .cancel) { _ in
            self.dismiss(animated: true, completion: nil)
        }
        exitAppAlert.addAction(resetApp)
        exitAppAlert.addAction(laterAction)
        present(exitAppAlert, animated: true, completion: nil)
    }
}

// MARK: - Joystick input

extension ViewController: JoystickViewDelegate {
    func joystick(_ view: JoystickView, didMoveTo x: Float, y: Float) {
        // x: -1..1 dönüş (sol/sağ),  y: -1..1 ileri/geri
        // Hız komutu maksimumu (UInt8 olduğu için 0..120 civarı tutuyoruz)
        let maxLinear: Float = 80.0
        let maxTurn:   Float = 50.0
        
        let speed = Int(round(y * maxLinear))   // pozitif = ileri
        let turn  = Int(round(x * maxTurn))     // pozitif = sağ
        
        // Throttle (>= 50 ms aralık)
        let now = Date()
        if now.timeIntervalSince(lastCommandSent) * 1000 < Double(commandIntervalMs)
            && speed == lastSpeedCmd && turn == lastTurnCmd {
            return
        }
        
        // Linear: ileri ise mForward(speed), geri ise mBackward(-speed)
        if speed > 0 {
            sendOnce(msgId: mForward,  value: UInt8(min(speed, 255)))
            sendOnce(msgId: mBackward, value: 0)
        } else if speed < 0 {
            sendOnce(msgId: mBackward, value: UInt8(min(-speed, 255)))
            sendOnce(msgId: mForward,  value: 0)
        } else if lastSpeedCmd != 0 {
            sendOnce(msgId: mForward,  value: 0)
            sendOnce(msgId: mBackward, value: 0)
        }
        
        // Turn
        if turn > 0 {
            sendOnce(msgId: mRight, value: UInt8(min(turn, 255)))
            sendOnce(msgId: mLeft,  value: 0)
        } else if turn < 0 {
            sendOnce(msgId: mLeft,  value: UInt8(min(-turn, 255)))
            sendOnce(msgId: mRight, value: 0)
        } else if lastTurnCmd != 0 {
            sendOnce(msgId: mLeft,  value: 0)
            sendOnce(msgId: mRight, value: 0)
        }
        
        lastSpeedCmd = speed
        lastTurnCmd  = turn
        lastCommandSent = now
    }
    
    func joystickDidRelease(_ view: JoystickView) {
        // Bırakıldığında dur — komut sıfırla
        sendOnce(msgId: mForward,  value: 0)
        sendOnce(msgId: mBackward, value: 0)
        sendOnce(msgId: mLeft,     value: 0)
        sendOnce(msgId: mRight,    value: 0)
        lastSpeedCmd = 0
        lastTurnCmd = 0
    }
}

// MARK: - BluetoothServiceDelegate

extension ViewController: BluetoothServiceDelegate {
    func didReceiveIPAddress(_ ipAddress: String) {
        self.addToTextBrowser(text: "IP Address: \(ipAddress)")
    }
    
    func didReceiveMessage(_ message: String) {
        self.addToTextBrowser(text: message)
    }
    
    func didUpdateRobotArmedState(_ isArmed: Bool) {
        self.isArmedRemote = isArmed
        self.updateToggleAppearance()
    }
    
    func didReceiveTelemetry(_ telemetry: RobotTelemetry) {
        DispatchQueue.main.async {
            self.telemetryView.update(telemetry: telemetry)
            // Toggle state'leri telemetri ile senkron tut
            if self.isArmedRemote != telemetry.armed {
                self.isArmedRemote = telemetry.armed
            }
            if self.isAutoMode != telemetry.autoMode {
                self.isAutoMode = telemetry.autoMode
            }
            if self.isPositionHold != telemetry.positionHold {
                self.isPositionHold = telemetry.positionHold
            }
            self.updateToggleAppearance()
        }
    }
}

import UIKit
import CoreBluetooth

class ViewController: UIViewController {
    
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
    
    // Use force unwrapped optional since these images should definitely exist
    let backgroundForward = UIImage(named: "Images/forward.png")
    let backgroundBackward = UIImage(named: "Images/backward.png")
    let backgroundLeft = UIImage(named: "Images/left.png")
    let backgroundRight = UIImage(named: "Images/right.png")
    
    let bluetoothService = BluetoothService.shared
    lazy var pairingFlow = PairingFlow(bluetoothService: self.bluetoothService)
    
    let speedConstant = 0x32
    
    override func viewDidLoad() {
        super.viewDidLoad()
        
        bluetoothService.delegate = self
        
        setupBluetoothService()
        configureButtons()
        configureTextBrowser()
    }
    
    private func setupBluetoothService() {
        self.bluetoothService.flowController = self.pairingFlow
    }
    
    private func configureButtons() {
        // Configure direction buttons
        configureDirectionButton(rightButton, image: backgroundRight, downAction: #selector(buttonRightHoldDown), releaseAction: #selector(buttonRightHoldRelease))
        configureDirectionButton(leftButton, image: backgroundLeft, downAction: #selector(buttonLeftHoldDown), releaseAction: #selector(buttonLeftHoldRelease))
        configureDirectionButton(forwardButton, image: backgroundForward, downAction: #selector(buttonForwardHoldDown), releaseAction: #selector(buttonForwardHoldRelease))
        configureDirectionButton(backwardButton, image: backgroundBackward, downAction: #selector(buttonBackwardHoldDown), releaseAction: #selector(buttonBackwardHoldRelease))
        
        // Configure other buttons
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
        button.addTarget(self, action: releaseAction, for: .touchUpOutside) // Also handle touch up outside
    }
    
    private func configureTextBrowser() {
        textBrowser.setContentOffset(.zero, animated: true)
        textBrowser.isEditable = false
        textBrowser.layer.cornerRadius = 8
        textBrowser.clipsToBounds = true
    }
    
    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        checkBluetoothState()
    }
    
    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        
        // Ensure the content view fills the entire screen
        view.frame = UIScreen.main.bounds
        
        // Force layout if needed
        view.layoutIfNeeded()
    }
    
    // MARK: - Bluetooth State Management
    
    private func checkBluetoothState() {
        if self.bluetoothService.bluetoothState != .poweredOn {
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) { self.checkBluetoothState() }
        }
        
        self.statusLabel.text = "Status: bluetooth is \(bluetoothService.bluetoothState == .poweredOn ? "ON" : "OFF")"
    }
    
    // MARK: - UI Helpers
    
    private func addToTextBrowser(text: String) {
        DispatchQueue.main.async {
            self.textBrowser.text += text + "\n"
            
            // Scroll to bottom to show the latest message
            let bottom = NSMakeRange(self.textBrowser.text.count - 1, 1)
            self.textBrowser.scrollRangeToVisible(bottom)
        }
    }
    
    // MARK: - Button Actions
    @IBAction func connectButtonPressed(_ sender: Any) {
        self.statusLabel.text = "Status: bluetooth is \(bluetoothService.bluetoothState == .poweredOn ? "ON" : "OFF")"
        
        guard self.bluetoothService.bluetoothState == .poweredOn else { return }
        
        if(self.connectButton.currentTitle == "Disconnect") {
            self.pairingFlow.cancel()
            self.connectButton.setTitle("Connect", for: .normal)
            self.textBrowser.text = nil
        } else {
            self.addToTextBrowser(text: "Status: waiting for peripheral...")
            self.pairingFlow.waitForPeripheral { // start flow
                self.addToTextBrowser(text: "Status: connecting...")
                self.pairingFlow.pair { result in // continue with next step
                    
                    if let periperalName = self.bluetoothService.peripheral?.name {
                        self.addToTextBrowser(text: "Status: connected to: \(periperalName)")
                        self.connectButton.setTitle("Disconnect", for: .normal)
                        
                        // Add the delayed data request here
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
                        self.addToTextBrowser(text: "Status: connected to peripheral")
                        self.connectButton.setTitle("Disconnect", for: .normal)
                    }
                }
            }
        }
    }
    
    @IBAction func buttonSpeakPressed(_ sender: Any) {
        guard let text = self.editSpeak.text, !text.isEmpty else {
            return
        }
        
        let data = Data(text.utf8)
        self.bluetoothService.sendCommand(msgId: mSpeak, data: data)
        
        // Clear the text field after sending
        self.editSpeak.text = ""
    }
    
    // MARK: - Direction Button Actions
    
    @objc func buttonRightHoldDown() {
        let data = Data([UInt8(speedConstant)])
        self.bluetoothService.sendCommand(msgId: mRight, data: data)
    }
    
    @objc func buttonRightHoldRelease() {
        let data = Data([0x00])
        self.bluetoothService.sendCommand(msgId: mRight, data: data)
    }
    
    @objc func buttonLeftHoldDown() {
        let data = Data([UInt8(speedConstant)])
        self.bluetoothService.sendCommand(msgId: mLeft, data: data)
    }
    
    @objc func buttonLeftHoldRelease() {
        let data = Data([0x00])
        self.bluetoothService.sendCommand(msgId: mLeft, data: data)
    }
    
    @objc func buttonForwardHoldDown() {
        let data = Data([UInt8(speedConstant)])
        self.bluetoothService.sendCommand(msgId: mForward, data: data)
    }
    
    @objc func buttonForwardHoldRelease() {
        let data = Data([0x00])
        self.bluetoothService.sendCommand(msgId: mForward, data: data)
    }
    
    @objc func buttonBackwardHoldDown() {
        let data = Data([UInt8(speedConstant)])
        self.bluetoothService.sendCommand(msgId: mBackward, data: data)
    }
    
    @objc func buttonBackwardHoldRelease() {
        let data = Data([0x00])
        self.bluetoothService.sendCommand(msgId: mBackward, data: data)
    }
    
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
    
    // MARK: - Exit Dialog
    
    func showMessageExitApp() {
        let exitAppAlert = UIAlertController(title: "Exit From App",
                                           message: "Are you sure you want to exit?",
                                           preferredStyle: .alert)
        
        let resetApp = UIAlertAction(title: "Close Now", style: .destructive) { _ in
            self.pairingFlow.cancel()
            // home button pressed programmatically - to throw app to background
            UIControl().sendAction(#selector(URLSessionTask.suspend), to: UIApplication.shared, for: nil)
            // terminating app in background
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

extension ViewController: BluetoothServiceDelegate {
    func didReceiveIPAddress(_ ipAddress: String) {
        self.addToTextBrowser(text: "IP Address: \(ipAddress)")
    }
    
    func didReceiveMessage(_ message: String) {
        self.addToTextBrowser(text: message)
    }
    
    func didUpdateRobotArmedState(_ isArmed: Bool) {
        //self.addToTextBrowser(text: "Robot state: \(isArmed ? "Armed" : "Disarmed")")
    }
}

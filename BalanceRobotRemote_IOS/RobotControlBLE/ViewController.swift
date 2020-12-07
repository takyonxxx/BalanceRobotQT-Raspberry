//
//  ViewController.swift
//  RobotControlBLE
//
//  Created by TÜRKAY BİLİYOR on 29.03.2019.
//  Copyright © 2019 TÜRKAY BİLİYOR. All rights reserved.
//

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
    
    let backgroundForward = UIImage(named: "Images/forward.png")
    let backgroundBackward = UIImage(named: "Images/backward.png")
    let backgroundLeft = UIImage(named: "Images/left.png")
    let backgroundRight = UIImage(named: "Images/right.png")
    
    let bluetoothService = BluetoothService.shared
    lazy var pairingFlow = PairingFlow(bluetoothService: self.bluetoothService)
    
    let speedConstant = 0x32
    
    override func viewDidLoad() {
        super.viewDidLoad()
        // Do any additional setup after loading the view.
        self.bluetoothService.flowController = self.pairingFlow
        
        self.rightButton.setTitle("",for: .normal)
        self.rightButton.showsTouchWhenHighlighted = true
        self.rightButton.setBackgroundImage(backgroundRight, for: .normal)
        //self.rightButton.frame = CGRect(x: 0, y: 0, width: 100, height: 50)
        self.rightButton.addTarget(self, action:#selector(buttonRightHoldDown), for: .touchDown)
        self.rightButton.addTarget(self, action:#selector(buttonRightHoldRelease), for: .touchUpInside)
        
        self.leftButton.setTitle("",for: .normal)
        self.leftButton.setBackgroundImage(backgroundLeft, for: .normal)
        self.leftButton.addTarget(self, action:#selector(buttonLeftHoldDown), for: .touchDown)
        self.leftButton.addTarget(self, action:#selector(buttonLeftHoldRelease), for: .touchUpInside)
        
        self.forwardButton.setTitle("",for: .normal)
        self.forwardButton.setBackgroundImage(backgroundForward, for: .normal)
        self.forwardButton.addTarget(self, action:#selector(buttonForwardHoldDown), for: .touchDown)
        self.forwardButton.addTarget(self, action:#selector(buttonForwardHoldRelease), for: .touchUpInside)
        
        self.backwardButton.setTitle("",for: .normal)
        self.backwardButton.setBackgroundImage(backgroundBackward, for: .normal)
        self.backwardButton.addTarget(self, action:#selector(buttonBackwardHoldDown), for: .touchDown)
        self.backwardButton.addTarget(self, action:#selector(buttonBackwardHoldRelease), for: .touchUpInside)
        
        self.textBrowser.setContentOffset(.zero, animated: true)
        self.textBrowser.isEditable = false
    }
    
    override func viewWillAppear(_ animated: Bool) {
        self.checkBluetoothState()
    }
    
    // TODO: probably you should modify current implementation of BluetoothService to notify you about this change
    private func checkBluetoothState()
    {
        if self.bluetoothService.bluetoothState != .poweredOn {
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) { self.checkBluetoothState() }
        }
        
        self.statusLabel.text = "Status: bluetooth is \(bluetoothService.bluetoothState == .poweredOn ? "ON" : "OFF")"
    }
    
    private func addToTextBrowser(text : String)
    {
        self.textBrowser.text += text + "\n"
    }
    
    @IBAction func connectButtonPressed(_ sender: Any) {
        self.statusLabel.text = "Status: bluetooth is \(bluetoothService.bluetoothState == .poweredOn ? "ON" : "OFF")"
        
        guard self.bluetoothService.bluetoothState == .poweredOn else { return }
        
        if(self.connectButton.currentTitle == "Disconnect")
        {
            self.pairingFlow.cancel()
            self.connectButton.setTitle("Connect", for: .normal)
            self.textBrowser.text = nil
        }
        else
        {
            self.addToTextBrowser(text :  "Status: waiting for peripheral...")
            self.pairingFlow.waitForPeripheral { // start flow
                self.addToTextBrowser (text : "Status: connecting...")
                self.pairingFlow.pair { result in // continue with next step
                    
                    if let periperalName = self.bluetoothService.peripheral?.name {
                        self.addToTextBrowser(text :"Status: connected to: \(periperalName)")
                        self.connectButton.setTitle("Disconnect", for: .normal)
                        
                    } else if result{
                        self.addToTextBrowser(text :"Status: connected to peripheral")
                        self.connectButton.setTitle("Disconnect", for: .normal)
                    }
                }
            }
        }
    }
    
    @IBAction func buttonSpeakPressed(_ sender: Any) {
        let string = self.editSpeak.text
        let data = Data(string!.utf8)
        self.bluetoothService.sendCommand(msgId: mSpeak, data: data)
    }
    
    
    @objc func buttonRightHoldDown()
    {
        let data = Data([0x32]) // 50
        self.bluetoothService.sendCommand(msgId: mRight, data: data)
    }
    @objc func buttonRightHoldRelease()
    {
        let data = Data([0x00]) // 0
        self.bluetoothService.sendCommand(msgId: mRight, data: data)
    }
    
    @objc func buttonLeftHoldDown()
    {
        let data = Data([0x32]) // 50
        self.bluetoothService.sendCommand(msgId: mLeft, data: data)
    }
    @objc func buttonLeftHoldRelease()
    {
        let data = Data([0x00]) // 0
        self.bluetoothService.sendCommand(msgId: mLeft, data: data)
    }
    
    @objc func buttonForwardHoldDown()
    {
        let data = Data([0x32]) // 50
        self.bluetoothService.sendCommand(msgId: mForward, data: data)
    }
    @objc func buttonForwardHoldRelease()
    {
        let data = Data([0x00]) // 0
        self.bluetoothService.sendCommand(msgId: mForward, data: data)
    }
    
    @objc func buttonBackwardHoldDown()
    {
        let data = Data([0x32]) // 50
        self.bluetoothService.sendCommand(msgId: mBackward, data: data)
    }
    @objc func buttonBackwardHoldRelease()
    {
        let data = Data([0x00]) // 0
        self.bluetoothService.sendCommand(msgId: mBackward, data: data)
    }
    
    @IBAction func buttonSettingsClicked(_ sender: Any)
    {
        PIDSettings.shared.show(animated: true)
        
        let payload =  Data(_: [0x00])
        self.bluetoothService.requestData(msgId: mPP, data: payload)
        self.bluetoothService.requestData(msgId: mPI, data: payload)
        self.bluetoothService.requestData(msgId: mPD, data: payload)
        self.bluetoothService.requestData(msgId: mAC, data: payload)
        self.bluetoothService.requestData(msgId: mSD, data: payload)
    }
    
    func showMessageExitApp(){
        let exitAppAlert = UIAlertController(title: "Exit From App",
                                             message: "Are you sure you want to exit?",
                                             preferredStyle: .alert)
        
        let resetApp = UIAlertAction(title: "Close Now", style: .destructive) {
            (alert) -> Void in
            self.pairingFlow.cancel()
            // home button pressed programmatically - to thorw app to background
            UIControl().sendAction(#selector(URLSessionTask.suspend), to: UIApplication.shared, for: nil)
            // terminaing app in background
            DispatchQueue.main.asyncAfter(deadline: .now() + .seconds(1), execute: {
                exit(EXIT_SUCCESS)
            })
        }
        
        let laterAction = UIAlertAction(title: "Later", style: .cancel) {
            (alert) -> Void in
            self.dismiss(animated: true, completion: nil)
        }
        
        exitAppAlert.addAction(resetApp)
        exitAppAlert.addAction(laterAction)
        present(exitAppAlert, animated: true, completion: nil)
        
    }
    
    @IBAction func clickedExitButton(_ sender: Any)
    {
        showMessageExitApp()
    }
}



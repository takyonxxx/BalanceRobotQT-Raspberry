//
//  BluetoothPairingService.swift
//  RobotControlBLE
//
//  Created by TÜRKAY BİLİYOR on 29.03.2019.
//  Copyright © 2019 TÜRKAY BİLİYOR. All rights reserved.
//

import Foundation
import CoreBluetooth

class PairingFlow: FlowController {
    
    let timeout = 15.0
    var waitForPeripheralHandler: () -> Void = { }
    var pairingHandler: (Bool) -> Void = { _ in }
    var pairingWorkitem: DispatchWorkItem?
    var pairing = false
    
    // MARK: 1. Pairing steps
    
    func waitForPeripheral(completion: @escaping () -> Void) {
        self.pairing = false
        self.pairingHandler = { _ in }
        
        self.bluetoothService.startScan()
        self.waitForPeripheralHandler = completion
    }
    
    func pair(completion: @escaping (Bool) -> Void) {
        
        guard self.bluetoothService.centralManager.state == .poweredOn else {
            print("bluetooth is off")
            self.pairingFailed()
            return
        }
        
        guard let peripheral = self.bluetoothService.peripheral else {
            print("peripheral not found")
            self.pairingFailed()
            return
        }
        
        self.pairing = true
        
        print("pairing...")
        
        self.pairingHandler = completion        
        self.bluetoothService.centralManager.connect(peripheral)
        self.pairingHandler(true)
    }
    
    func cancel() {
        self.bluetoothService.stopScan()
        self.bluetoothService.disconnect()
        self.pairingWorkitem?.cancel()
        
        self.pairing = false
        self.pairingHandler = { _ in }
        self.waitForPeripheralHandler = { }
    }
    
    // MARK: 3. State handling
    
    override func discoveredPeripheral() {
        self.bluetoothService.stopScan()
        self.waitForPeripheralHandler()
    }
    
    func connetionStatus(status : Bool) {
        self.pairingHandler(status)
    }
    
    override func readyToWrite() {
        guard self.pairing else { return }
        
        self.bluetoothService.getTxValue()
    }
    
    override func readyToRead() {
        guard self.pairing else { return }
        
        self.bluetoothService.getRxValue()
    }
    
    override func received(response: Data) {
        print("received data: \(String(bytes: response, encoding: String.Encoding.ascii) ?? "")")
        // TODO: validate response to confirm that pairing is sucessful
        self.pairingHandler(true)
        self.cancel()
    }
    
    override func disconnected(failure: Bool) {
        self.pairingFailed()
    }
    
    private func pairingFailed() {
        self.pairingHandler(false)
        self.cancel()
    }
}

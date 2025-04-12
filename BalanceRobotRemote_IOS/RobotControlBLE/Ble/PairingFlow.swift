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
    var scanning = false // Add this to track scan state
    
    // MARK: 1. Pairing steps
    
    func waitForPeripheral(completion: @escaping () -> Void) {
        self.pairing = false
        self.pairingHandler = { _ in }
        self.scanning = true
        
        self.bluetoothService.startScan()
        self.waitForPeripheralHandler = completion
        
        // Set a timeout for scanning
        DispatchQueue.main.asyncAfter(deadline: .now() + timeout) { [weak self] in
            guard let self = self, self.scanning else { return }
            
            // If still scanning after timeout, stop scan
            self.bluetoothService.stopScan()
            self.scanning = false
            print("Scanning timed out")
        }
    }
    
    func pair(completion: @escaping (Bool) -> Void) {
        guard self.bluetoothService.centralManager.state == .poweredOn else {
            print("bluetooth is off")
            self.pairingFailed()
            completion(false)
            return
        }
        
        guard let peripheral = self.bluetoothService.peripheral else {
            print("peripheral not found")
            self.pairingFailed()
            completion(false)
            return
        }
        
        self.pairing = true
        print("pairing...")
        
        // Use the connect method with completion handler
        self.bluetoothService.connect { success in
            if success {
                print("Connection and characteristic discovery complete!")
                // Important: Call the completion handler on the main thread
                DispatchQueue.main.async {
                    completion(true)
                }
            } else {
                print("Connection or characteristic discovery failed")
                self.pairingFailed()
                DispatchQueue.main.async {
                    completion(false)
                }
            }
        }
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

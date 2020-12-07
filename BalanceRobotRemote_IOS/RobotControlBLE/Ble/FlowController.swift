//
//  BluetoothBase.swift
//  RobotControlBLE
//
//  Created by TÜRKAY BİLİYOR on 29.03.2019.
//  Copyright © 2019 TÜRKAY BİLİYOR. All rights reserved.
//

import Foundation
import CoreBluetooth

class FlowController {
    
    var bluetoothService = BluetoothService()
   
    init(bluetoothService: BluetoothService) {
        self.bluetoothService = bluetoothService
    }

    func bluetoothOn() {
    }
    
    func bluetoothOff() {
    }
    
    func scanStarted() {
    }
    
    func scanStopped() {
    }
    
    func connected(peripheral: CBPeripheral) {
    }
    
    func disconnected(failure: Bool) {
    }
    
    func discoveredPeripheral() {
    }
    
    func readyToWrite() {
    }
    
    func readyToRead() {
    }
    
    func received(response: Data) {
    }
    
    // TODO: add other events if needed
}

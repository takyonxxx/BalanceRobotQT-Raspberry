//
//  BluetoothConnectionHandler.swift
//  RobotControlBLE
//
//  Created by TÜRKAY BİLİYOR on 29.03.2019.
//  Copyright © 2019 TÜRKAY BİLİYOR. All rights reserved.
//

import Foundation
import CoreBluetooth

extension BluetoothService: CBCentralManagerDelegate {
    
    //define your remote device name (beginning of, start of name)
    var expectedNamePrefix: String { return "BlueZ" } // 1.
    var expectedNamePrefix2: String { return "rasp" } // 1.
    
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state != .poweredOn {
            print("bluetooth is OFF (\(central.state.rawValue))")
            self.stopScan()
            self.disconnect()
            self.flowController?.bluetoothOff() // 2.
        } else {
            print("bluetooth is ON")
            self.flowController?.bluetoothOn() // 2.
        }
    }
    
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String : Any], rssi RSSI: NSNumber) {
        print("discovered peripheral: \(peripheral.name!)")
        
        if ((peripheral.name?.starts(with: self.expectedNamePrefix)) == nil &&
                (peripheral.name?.starts(with: self.expectedNamePrefix2)) == nil)
        {
            return
        }
        
        self.peripheral = peripheral
        self.flowController?.discoveredPeripheral()
    }
    
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        
        print("Connected to peripheral \(peripheral.identifier.uuidString)")
        
        peripheral.delegate = self
        peripheral.discoverServices(nil)
        self.flowController?.connected(peripheral: peripheral) // 2.
    }
    
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        print("peripheral disconnected")
        self.txCharacteristic = nil
        self.rxCharacteristic = nil
        self.flowController?.disconnected(failure: false) // 2.
    }
    
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        print("failed to connect: \(error.debugDescription)")
        self.txCharacteristic = nil
        self.rxCharacteristic = nil
        self.flowController?.disconnected(failure: true) // 2.
    }
}

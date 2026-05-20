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
    
    // Pi tarafında advertise edilen ad: "Balance Robot"
    // (gattserver.cpp -> setLocalName("Balance Robot"))
    // Geriye dönük uyumluluk için "rasp" da kabul edilir.
    var expectedNamePrefixes: [String] {
        return ["balance", "rasp"]
    }
    
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state != .poweredOn {
            print("bluetooth is OFF (\(central.state.rawValue))")
            self.stopScan()
            self.disconnect()
            self.flowController?.bluetoothOff()
        } else {
            print("bluetooth is ON")
            self.flowController?.bluetoothOn()
        }
        // UI'lara bilgi ver — Control sekmesi Connect butonunu enable etmek için bunu dinler
        NotificationCenter.default.post(name: BluetoothService.stateDidChange, object: nil)
    }
    
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String : Any], rssi RSSI: NSNumber) {
        // Ad bazen advertisementData üzerinden gelir, peripheral.name nil olabilir.
        let advertisedName = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        let candidateName = (advertisedName ?? peripheral.name ?? "").lowercased()
        
        print("discovered peripheral: '\(candidateName)' rssi=\(RSSI)")
        
        let matches = expectedNamePrefixes.contains { candidateName.contains($0) }
        guard matches else { return }
        
        // Eğer hedef cihazı bulduysak yakala
        self.peripheral = peripheral
        peripheral.delegate = self
        
        self.centralManager.stopScan()
        print("scan stopped, target peripheral selected")
        
        if let flow = self.flowController as? PairingFlow {
            flow.scanning = false
            DispatchQueue.main.async {
                flow.waitForPeripheralHandler()
            }
        }
    }
    
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        print("Connected to peripheral \(peripheral.identifier.uuidString)")
        peripheral.delegate = self
        peripheral.discoverServices(nil)
        self.flowController?.connected(peripheral: peripheral)
        NotificationCenter.default.post(name: BluetoothService.connectionDidChange, object: nil)
    }
    
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        if let err = error {
            print("peripheral disconnected with error: \(err.localizedDescription)")
        } else {
            print("peripheral disconnected")
        }
        self.peripheral = nil
        self.txCharacteristic = nil
        self.rxCharacteristic = nil
        self.flowController?.disconnected(failure: false)
        NotificationCenter.default.post(name: BluetoothService.connectionDidChange, object: nil)
    }
    
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        print("failed to connect: \(error.debugDescription)")
        self.peripheral = nil
        self.txCharacteristic = nil
        self.rxCharacteristic = nil
        self.flowController?.disconnected(failure: true)
        NotificationCenter.default.post(name: BluetoothService.connectionDidChange, object: nil)
    }
}

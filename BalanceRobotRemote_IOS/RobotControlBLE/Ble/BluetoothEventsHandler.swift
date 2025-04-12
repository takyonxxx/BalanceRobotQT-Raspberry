//
//  BluetoothEventsHandler.swift
//  RobotControlBLE
//
//  Created by TÜRKAY BİLİYOR on 29.03.2019.
//  Copyright © 2019 TÜRKAY BİLİYOR. All rights reserved.
//

import Foundation
import CoreBluetooth

extension BluetoothService: CBPeripheralDelegate {
    
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error = error {
            print("Error discovering services: \(error.localizedDescription)")
            return
        }
        
        guard let services = peripheral.services else {
            print("No services found")
            return
        }
        
        print("Services discovered: \(services.count)")
        for service in services {
            print("Service UUID: \(service.uuid.uuidString)")
            peripheral.discoverCharacteristics(nil, for: service)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error = error {
            print("Error discovering characteristics: \(error.localizedDescription)")
            return
        }
        
        guard let characteristics = service.characteristics else {
            print("No characteristics found for service: \(service.uuid.uuidString)")
            return
        }
        
        print("Discovered \(characteristics.count) characteristics for service \(service.uuid.uuidString):")
        for characteristic in characteristics {
            let uuid = characteristic.uuid.uuidString
            let properties = describeProperties(characteristic.properties)
            
            // Compare case-insensitively
            if uuid.caseInsensitiveCompare(self.BLERxUuid) == .orderedSame {
                self.rxCharacteristic = characteristic
                print("✓ RX characteristic found with properties: \(properties)")
                
                // Setup notifications if needed
                if characteristic.properties.contains(.notify) {
                    peripheral.setNotifyValue(true, for: characteristic)
                }
            }
            else if uuid.caseInsensitiveCompare(self.BLETxUuid) == .orderedSame {
                self.txCharacteristic = characteristic
                print("✓ TX characteristic found with properties: \(properties)")
                
                // Setup notifications
                peripheral.setNotifyValue(true, for: characteristic)
            }
        }
        
        // After discovery, check what we found
        if self.rxCharacteristic == nil {
            print("⚠️ RX characteristic not found in service \(service.uuid.uuidString)")
        }
        if self.txCharacteristic == nil {
            print("⚠️ TX characteristic not found in service \(service.uuid.uuidString)")
        }
        
        // After checking what we found, call the callback if both characteristics are present
        if self.rxCharacteristic != nil && self.txCharacteristic != nil {
            DispatchQueue.main.async {
                self.characteristicsDiscoveredCallback?()
            }
        }
    }

    // Helper function to describe characteristic properties
    func describeProperties(_ properties: CBCharacteristicProperties) -> String {
        var desc: [String] = []
        if properties.contains(.read) { desc.append("Read") }
        if properties.contains(.write) { desc.append("Write") }
        if properties.contains(.writeWithoutResponse) { desc.append("WriteWithoutResponse") }
        if properties.contains(.notify) { desc.append("Notify") }
        if properties.contains(.indicate) { desc.append("Indicate") }
        if properties.contains(.broadcast) { desc.append("Broadcast") }
        return desc.isEmpty ? "None" : desc.joined(separator: ", ")
    }
    
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if error != nil {
            print("Error receiving data: \(error!.localizedDescription)")
            return
        }
        
        guard let message = characteristic.value else {
            print("didUpdateValueFor \(characteristic.uuid.uuidString) with no data")
            return
        }
        
        let dataEmpty = Data()
        var parsedPack = MessagePack(header: 0, len: 0, rw: 0, command: 0, data: dataEmpty)
        if(!messageService.parse(dataReceived: message, messagePack: &parsedPack)) {
            print("Failed to parse message")
            return
        }
        
        print("Parsed command: 0x\(String(format: "%02X", parsedPack.command))")
        print("Parsed data size: \(parsedPack.data.count)")
        
        if parsedPack.command == mData {
            // Handle IP address string from mData command
            if parsedPack.data.count > 0 {
                if let ipString = String(data: parsedPack.data, encoding: .utf8) {
                    print("Received IP address: \(ipString)")
                } else if let ipString = String(data: parsedPack.data, encoding: .ascii) {
                    // Try ASCII encoding as fallback
                    print("Received IP address (ASCII): \(ipString)")
                } else {
                    // If string conversion fails, show hex representation
                    let hexData = parsedPack.data.map { String(format: "%02X", $0) }.joined(separator: " ")
                    print("Received data that couldn't be converted to string: \(hexData)")
                }
            } else {
                print("Received mData command with empty data")
            }
        } else if parsedPack.command == mArmed {
            if parsedPack.data.count > 0 {
                let value = parsedPack.data[0]
                print("Robot armed state: \(value != 0 ? "Armed" : "Disarmed")")
            }
        } else if parsedPack.data.count > 0 {
            let value = parsedPack.data[0]
            
            switch parsedPack.command {
                case mPP:
                    PIDSettings.shared.setPValue(value: Float(value))
                case mPI:
                    PIDSettings.shared.setIValue(value: Float(value))
                case mPD:
                    PIDSettings.shared.setDValue(value: Float(value))
                case mSD:
                    PIDSettings.shared.setSDValue(value: Float(value))
                case mAC:
                    PIDSettings.shared.setACValue(value: Float(value))
                default:
                    print("Unhandled command with data: 0x\(String(format: "%02X", parsedPack.command))")
            }
        } else {
            print("Command received with no data payload: 0x\(String(format: "%02X", parsedPack.command))")
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if error != nil {
            print("error while writing value to \(characteristic.uuid.uuidString): \(error.debugDescription)")
        }
    }
    
    private func hexEncodedString(_ data: Data?) -> String {
        let format = "0x%02hhX "
        return data?.map { String(format: format, $0) }.joined() ?? ""
    }
}

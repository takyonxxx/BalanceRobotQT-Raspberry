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
        guard let services = peripheral.services else { return }
        
        print("services discovered")
        for service in services {
            let serviceUuid = service.uuid.uuidString
            print("discovered service: \(serviceUuid)")
            
            if serviceUuid == self.BLEServiceUuid {
                peripheral.discoverCharacteristics(nil, for: service)
            }
        }
    }
    
    
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard let characteristics = service.characteristics else { return }
        
        print("characteristics discovered")
        for characteristic in characteristics {
            
            let characteristicUuid = characteristic.uuid.uuidString
            
            if characteristicUuid == self.BLERxUuid {
                peripheral.setNotifyValue(true, for: characteristic)
                
                self.rxCharacteristic = characteristic
                print("discovered rx characteristic: \(characteristicUuid) | read=\(characteristic.properties.contains(.notify)) | write=\(characteristic.properties.contains(.write))")
                
            }
            else if characteristicUuid == self.BLETxUuid {
                peripheral.setNotifyValue(true, for: characteristic)
                
                self.txCharacteristic = characteristic
                print("discovered tx characteristic: \(characteristicUuid) | read=\(characteristic.properties.contains(.notify)) | write=\(characteristic.properties.contains(.write))")
            }
        }
    }   
    
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        
        if let message = characteristic.value {
            print("didUpdateValueFor \(characteristic.uuid.uuidString) = count: \(message.count) | \(self.hexEncodedString(message))")
            
            let dataEmpty = Data()
            var parsedPack = MessagePack(header: 0, len: 0, rw: 0, command: 0, data: dataEmpty)
            if(!messageService.parse(dataReceived: message, messagePack: &parsedPack)){return}
            
            let value = parsedPack.data[0]
            
            switch parsedPack.command
            {
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
                /*case mSpeak:
                 let value = String(decoding: parsedPack.data, as: UTF8.self)*/
                
            default:
                print("Undefined Command")
            }
        } else {
            print("didUpdateValueFor \(characteristic.uuid.uuidString) with no data")
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if error != nil {
            print("error while writing value to \(characteristic.uuid.uuidString): \(error.debugDescription)")
        } else {
            print("didWriteValueFor \(characteristic.uuid.uuidString)")
        }
    }
    
    private func hexEncodedString(_ data: Data?) -> String {
        let format = "0x%02hhX "
        return data?.map { String(format: format, $0) }.joined() ?? ""
    }
}

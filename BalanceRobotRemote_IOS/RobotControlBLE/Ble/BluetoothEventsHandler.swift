//
//  BluetoothEventsHandler.swift
//  RobotControlBLE
//
//  Created by TÜRKAY BİLİYOR on 29.03.2019.
//  Copyright © 2019 TÜRKAY BİLİYOR. All rights reserved.
//

import Foundation
import CoreBluetooth

protocol BluetoothServiceDelegate: AnyObject {
    func didReceiveIPAddress(_ ipAddress: String)
    func didReceiveMessage(_ message: String)
    func didUpdateRobotArmedState(_ isArmed: Bool)
    func didReceiveTelemetry(_ telemetry: RobotTelemetry)
    /// Pi'den gelen mPP/mPI/mPD/mAC/mSD/mAutoMode/mPositionHold cevapları.
    /// Settings ekranı slider/switch'leri buradan günceller.
    func didReceiveSettingValue(command: Byte, rawValue: UInt8)
}

// Geriye uyumluluk: bu metodu implement etmeyen delegate'ler için varsayılan no-op.
extension BluetoothServiceDelegate {
    func didReceiveSettingValue(command: Byte, rawValue: UInt8) { }
}

// Robot canlı telemetri verisi
public struct RobotTelemetry {
    public var angle: Float = 0       // °
    public var gyroRate: Float = 0    // °/s
    public var targetAngle: Float = 0 // °
    public var trim: Float = 0        // °
    public var pwmL: Int16 = 0
    public var pwmR: Int16 = 0
    public var armed: Bool = false
    public var fallen: Bool = false
    public var autoMode: Bool = false
    public var positionHold: Bool = false
}

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
            
            if uuid.caseInsensitiveCompare(self.BLERxUuid) == .orderedSame {
                self.rxCharacteristic = characteristic
                print("✓ RX characteristic found with properties: \(properties)")
                
                if characteristic.properties.contains(.notify) {
                    peripheral.setNotifyValue(true, for: characteristic)
                }
            }
            else if uuid.caseInsensitiveCompare(self.BLETxUuid) == .orderedSame {
                self.txCharacteristic = characteristic
                print("✓ TX characteristic found with properties: \(properties)")
                
                peripheral.setNotifyValue(true, for: characteristic)
            }
        }
        
        if self.rxCharacteristic == nil {
            print("⚠️ RX characteristic not found in service \(service.uuid.uuidString)")
        }
        if self.txCharacteristic == nil {
            print("⚠️ TX characteristic not found in service \(service.uuid.uuidString)")
        }
        
        if self.rxCharacteristic != nil && self.txCharacteristic != nil {
            DispatchQueue.main.async {
                self.characteristicsDiscoveredCallback?()
            }
        }
    }

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
        
        // Çok sık çıkan telemetri mesajını ayrı işleyelim
        if parsedPack.command == mTelemetry {
            if let tel = parseTelemetry(parsedPack.data) {
                delegate?.didReceiveTelemetry(tel)
            }
            return
        }
        
        if parsedPack.command == mData {
            // IP adresi
            if parsedPack.data.count > 0 {
                if let ipString = String(data: parsedPack.data, encoding: .utf8) {
                    delegate?.didReceiveIPAddress(ipString)
                } else if let ipString = String(data: parsedPack.data, encoding: .ascii) {
                    delegate?.didReceiveIPAddress(ipString)
                } else {
                    let hexData = parsedPack.data.map { String(format: "%02X", $0) }.joined(separator: " ")
                    delegate?.didReceiveMessage("Received binary data: \(hexData)")
                }
            }
        } else if parsedPack.command == mArmed {
            if parsedPack.data.count > 0 {
                let value = parsedPack.data[0]
                let isArmed = value != 0
                delegate?.didUpdateRobotArmedState(isArmed)
            }
        } else if parsedPack.data.count > 0 {
            let value = parsedPack.data[0]
            
            switch parsedPack.command {
                case mPP, mPI, mPD, mSD, mAC, mSpdKp, mSpdKi, mSpdMaxTilt, mSpdMaxVel, mAutoMode, mPositionHold:
                    // Eski PIDSettings modal'ı (görünmez) güncel kalsın
                    switch parsedPack.command {
                        case mPP: PIDSettings.shared.setPValue(value: Float(value))
                        case mPI: PIDSettings.shared.setIValue(value: Float(value))
                        case mPD: PIDSettings.shared.setDValue(value: Float(value))
                        case mSD: PIDSettings.shared.setSDValue(value: Float(value))
                        case mAC: PIDSettings.shared.setACValue(value: Float(value))
                        default: break
                    }
                    // Yeni Settings sekmesini de bilgilendir
                    delegate?.didReceiveSettingValue(command: parsedPack.command, rawValue: value)
                default:
                    print("Unhandled command with data: 0x\(String(format: "%02X", parsedPack.command))")
                    delegate?.didReceiveMessage("Unhandled command: 0x\(String(format: "%02X", parsedPack.command))")
            }
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
    
    // -------- Telemetri parsing --------
    // Bkz. balancerobot.cpp::onTelemetryTick - paket düzeni:
    //  [0..1] angle  (int16 BE, *100)
    //  [2..3] gyro   (int16 BE, *10)
    //  [4..5] target (int16 BE, *100)
    //  [6..7] trim   (int16 BE, *100)
    //  [8..9] pwmL   (int16 BE)
    // [10..11] pwmR  (int16 BE)
    //  [12]   flags
    //  [13]   reserved
    private func parseTelemetry(_ data: Data) -> RobotTelemetry? {
        guard data.count >= 13 else { return nil }
        
        func i16(_ idx: Int) -> Int16 {
            let hi = Int(data[idx])
            let lo = Int(data[idx + 1])
            let raw = (hi << 8) | lo
            return Int16(truncatingIfNeeded: raw)
        }
        
        var t = RobotTelemetry()
        t.angle       = Float(i16(0))  / 100.0
        t.gyroRate    = Float(i16(2))  / 10.0
        t.targetAngle = Float(i16(4))  / 100.0
        t.trim        = Float(i16(6))  / 100.0
        t.pwmL        = i16(8)
        t.pwmR        = i16(10)
        let flags     = data[12]
        t.armed        = (flags & 0x01) != 0
        t.fallen       = (flags & 0x02) != 0
        t.autoMode     = (flags & 0x04) != 0
        t.positionHold = (flags & 0x08) != 0
        return t
    }
}

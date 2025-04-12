//
//  MessageService.swift
//  RobotControlBLE
//
//  Created by TÜRKAY BİLİYOR on 2.04.2019.
//  Copyright © 2019 TÜRKAY BİLİYOR. All rights reserved.
//

//  MessageService.swift
//  RobotControlBLE
//
//  Created by TÜRKAY BİLİYOR on 2.04.2019.
//  Copyright © 2019 TÜRKAY BİLİYOR. All rights reserved.
//

import Foundation

typealias Byte = UInt8
let bufferSize = 1024
let messageFixedSize = 6
let maxPayload = 1024

// Message constants - updated to match C++ implementation
let mHeader:Byte     = Byte(0xb0) // Fix header
let mWrite:Byte      = Byte(0x01) // Write request
let mRead:Byte       = Byte(0x02) // Read request
let mArmed:Byte      = Byte(0x03) // Armed state - new
let mDisArmed:Byte   = Byte(0x04) // Disarmed state - new
let mForward:Byte    = Byte(0xa0)
let mBackward:Byte   = Byte(0xa1)
let mLeft:Byte       = Byte(0xb0)
let mRight:Byte      = Byte(0xb1)
let mPP:Byte         = Byte(0xc0) // Proportional
let mPI:Byte         = Byte(0xc1) // Integral control
let mPD:Byte         = Byte(0xc2) // Derivative constant
let mAC:Byte         = Byte(0xd0) // Angle correction
let mSD:Byte         = Byte(0xd1) // Speed diff constant wheel
let mSpeak:Byte      = Byte(0xe0) // Speak
let mData:Byte       = Byte(0xe1) // Data - new

public struct MessagePack {
    var header: Byte
    var len: Byte
    var rw: Byte
    var command: Byte
    var data: Data
    var checkSum: [Byte] // Added checksum field to match C++ implementation
    
    init(header: Byte, len: Byte, rw: Byte, command: Byte, data: Data) {
        self.header = header
        self.len = len
        self.rw = rw
        self.command = command
        self.data = data
        self.checkSum = [0, 0] // Initialize checksum
    }
}

class MessageService {
    public var position = 0
    public var data: [UInt8] = []
    
    // Helper function to print data for debugging
    func printData(data: Data) {
        for i in 0 ..< data.count {
            print(data[i])
        }
    }
    
    func returnDataValue(intValue: inout Int) -> Data {
        let data = Data(bytes: &intValue, count: MemoryLayout.size(ofValue: intValue))
        return data
    }
    
    // Updated to match C++ implementation with better safety
    func create_pack(readwrite: Byte, command: Byte, dataSend: Data) -> Data {
        var result = Data(capacity: bufferSize)
        
        // Limit data length to prevent buffer overflow
        let dataLength = min(dataSend.count, maxPayload)
        let m_len = Byte(dataLength)
        
        // Add header fields
        result.append(mHeader)
        result.append(m_len)
        result.append(readwrite)
        result.append(command)
        
        // Add data with bounds checking
        if dataLength > 0 {
            result.append(dataSend.prefix(dataLength))
        }
        
        // For future implementation: Calculate and append checksum
        // Currently just placeholder (not actually used in communication)
        // let checksum = calculateChecksum(data: result)
        // result.append(checksum[0])
        // result.append(checksum[1])
        
        return result
    }
    
    // Updated to match C++ implementation with better safety and error handling
    func parse(dataReceived: Data, messagePack: inout MessagePack) -> Bool {
        // Safety checks
        if dataReceived.count < 4 {
            print("Error: Received data too small")
            return false
        }
        
        if dataReceived[0] != mHeader {
            print("Error: Invalid header")
            return false
        }
        
        // Extract message fields
        messagePack.header = dataReceived[0]
        messagePack.len = dataReceived[1]
        messagePack.rw = dataReceived[2]
        messagePack.command = dataReceived[3]
        
        // Safety check for data length
        if messagePack.len > maxPayload || Int(messagePack.len) > (dataReceived.count - 4) {
            print("Error: Invalid data length")
            return false
        }
        
        // Clear existing data and copy new data
        messagePack.data = Data()
        
        // Copy data with bounds checking
        let dataStartIndex = 4
        let dataEndIndex = min(dataStartIndex + Int(messagePack.len), dataReceived.count)
        
        if dataEndIndex > dataStartIndex {
            messagePack.data = dataReceived.subdata(in: dataStartIndex..<dataEndIndex)
        }
        
        return true
    }
    
    // For future implementation: Calculate checksum (not currently used)
    private func calculateChecksum(data: Data) -> [Byte] {
        // Implement checksum algorithm here if needed in the future
        // This is just a placeholder
        return [0, 0]
    }
}

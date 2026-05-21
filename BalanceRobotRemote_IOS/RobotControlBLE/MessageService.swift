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

// Mesaj sabitleri - Pi tarafı (message.h) ile bire bir eşleşmeli
let mHeader:Byte     = Byte(0xb0)
let mWrite:Byte      = Byte(0x01)
let mRead:Byte       = Byte(0x02)
let mArmed:Byte      = Byte(0x03)
let mDisArmed:Byte   = Byte(0x04)
let mForward:Byte    = Byte(0xa0)
let mBackward:Byte   = Byte(0xa1)
let mLeft:Byte       = Byte(0xb0)
let mRight:Byte      = Byte(0xb1)
let mPP:Byte         = Byte(0xc0)
let mPI:Byte         = Byte(0xc1)
let mPD:Byte         = Byte(0xc2)
let mSpdKp:Byte      = Byte(0xc3)
let mSpdKi:Byte      = Byte(0xc4)
let mSpdMaxTilt:Byte = Byte(0xc5)
let mSpdMaxVel:Byte  = Byte(0xc6)
let mAC:Byte         = Byte(0xd0)
let mSD:Byte         = Byte(0xd1)
let mSpeak:Byte      = Byte(0xe0)
let mData:Byte       = Byte(0xe1)

// v2 yeni komutlar
let mTelemetry:Byte    = Byte(0xf0)
let mAutoMode:Byte     = Byte(0xf1)
let mTrimFine:Byte     = Byte(0xf2)
let mPositionHold:Byte = Byte(0xf3)
let mResetTrim:Byte    = Byte(0xf4)

public struct MessagePack {
    var header: Byte
    var len: Byte
    var rw: Byte
    var command: Byte
    var data: Data
    var checkSum: [Byte]
    
    init(header: Byte, len: Byte, rw: Byte, command: Byte, data: Data) {
        self.header = header
        self.len = len
        self.rw = rw
        self.command = command
        self.data = data
        self.checkSum = [0, 0]
    }
}

class MessageService {
    public var position = 0
    public var data: [UInt8] = []
    
    func printData(data: Data) {
        for i in 0 ..< data.count {
            print(data[i])
        }
    }
    
    func returnDataValue(intValue: inout Int) -> Data {
        let data = Data(bytes: &intValue, count: MemoryLayout.size(ofValue: intValue))
        return data
    }
    
    func create_pack(readwrite: Byte, command: Byte, dataSend: Data) -> Data {
        var result = Data(capacity: bufferSize)
        
        let dataLength = min(dataSend.count, maxPayload)
        let m_len = Byte(dataLength)
        
        result.append(mHeader)
        result.append(m_len)
        result.append(readwrite)
        result.append(command)
        
        if dataLength > 0 {
            result.append(dataSend.prefix(dataLength))
        }
        
        return result
    }
    
    func parse(dataReceived: Data, messagePack: inout MessagePack) -> Bool {
        if dataReceived.count < 4 {
            print("Error: Received data too small")
            return false
        }
        
        if dataReceived[0] != mHeader {
            print("Error: Invalid header")
            return false
        }
        
        messagePack.header = dataReceived[0]
        messagePack.len = dataReceived[1]
        messagePack.rw = dataReceived[2]
        messagePack.command = dataReceived[3]
        
        if messagePack.len > maxPayload || Int(messagePack.len) > (dataReceived.count - 4) {
            print("Error: Invalid data length")
            return false
        }
        
        messagePack.data = Data()
        let dataStartIndex = 4
        let dataEndIndex = min(dataStartIndex + Int(messagePack.len), dataReceived.count)
        
        if dataEndIndex > dataStartIndex {
            messagePack.data = dataReceived.subdata(in: dataStartIndex..<dataEndIndex)
        }
        
        return true
    }
}

// MARK: - Yardımcılar
extension Data {
    /// Big-endian signed 16-bit oluştur
    static func bePackInt16(_ value: Int16) -> Data {
        var d = Data(count: 2)
        d[0] = UInt8(truncatingIfNeeded: Int(value) >> 8)
        d[1] = UInt8(truncatingIfNeeded: Int(value))
        return d
    }
}

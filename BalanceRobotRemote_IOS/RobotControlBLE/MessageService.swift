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

let mHeader:Byte     = Byte(0xb0) //Fix header
let mWrite:Byte      = Byte(0x01) //Write request
let mRead:Byte       = Byte(0x02) //Read request
let mForward:Byte    = Byte(0xa0)
let mBackward:Byte   = Byte(0xa1)
let mLeft:Byte       = Byte(0xb0)
let mRight:Byte      = Byte(0xb1)
let mPP:Byte         = Byte(0xc0) //Proportional
let mPI:Byte         = Byte(0xc1) //Integral control
let mPD:Byte         = Byte(0xc2) //Derivative constant
let mAC:Byte         = Byte(0xd0) //angle correction
let mSD:Byte         = Byte(0xd1) //speed diff constant wheel
let mSpeak:Byte      = Byte(0xe0) //speak

public struct MessagePack{
    var header : Byte
    var len : Byte
    var rw : Byte
    var command : Byte
    var data :  Data
    init(header: Byte, len: Byte, rw: Byte, command: Byte, data: Data){
        self.header = header
        self.len = len
        self.rw = rw
        self.command = command
        self.data = data
    }
}

class MessageService
{
    public var position = 0
    public var data: [UInt8] = []
    
    
    func printData(data: Data) {
        for i in 0 ..< data.count
        {
            print(data[i])
        }
    }
    
    func returnDataValue(intValue: inout Int) -> Data {
        let data = Data(bytes: &intValue, count: MemoryLayout.size(ofValue: intValue))
        return data
    }
    
    func create_pack (readwrite : Byte, command : Byte, dataSend : Data) -> Data
    {
        var result = Data(capacity: bufferSize)
        
        let m_header = mHeader
        let m_len = Byte(dataSend.count)
        let m_rw = readwrite
        let m_command = command
        
        result.append(m_header)
        result.append(m_len)
        result.append(m_rw)
        result.append(m_command)
        
        for i in 0 ..< dataSend.count
        {
            result.append(dataSend[i])
        }
        
        return result
    }
    
    func parse(dataReceived: Data, messagePack: inout MessagePack) -> Bool
    {
        var data_index: Int = 0;
        var received_index: Int = -1;
        
        if (dataReceived[0] != mHeader) {return false}
        
        messagePack.header = dataReceived[0];
        messagePack.len = dataReceived[1];
        messagePack.rw = dataReceived[2];
        messagePack.command = dataReceived[3];
        
        data_index = 0;
        received_index = 3;
        
        while(data_index<(messagePack.len))
        {
            received_index += 1;
            if (received_index == dataReceived.count) {break}
            var valueData = dataReceived[received_index]
            messagePack.data.append(&valueData, count: 1)
            data_index += 1;
        }
        
        return true;
    }
}

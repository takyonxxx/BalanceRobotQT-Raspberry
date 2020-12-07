//
//  BluetoothService.swift
//  RobotControlBLE
//
//  Created by TÜRKAY BİLİYOR on 29.03.2019.
//  Copyright © 2019 TÜRKAY BİLİYOR. All rights reserved.
//

import UIKit
import CoreBluetooth

class BluetoothService: NSObject { // 1.
    static let shared = BluetoothService()
    
    let BLEServiceUuid  = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
    let BLERxUuid       = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
    let BLETxUuid       = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
    
    var centralManager: CBCentralManager!
    var peripheral: CBPeripheral?
    var txCharacteristic:CBCharacteristic? = nil
    var rxCharacteristic:CBCharacteristic? = nil
    let messageService = MessageService()
    
    var bluetoothState: CBManagerState {
        return self.centralManager.state
    }
    var flowController: FlowController? // 3.
    
    override init() {
        super.init()
        self.centralManager = CBCentralManager(delegate: self, queue: nil)
    }
    
    func sendData(message : Data)
    {
        if self.txCharacteristic != nil
        {
            self.peripheral?.writeValue(message, for: self.txCharacteristic!, type: CBCharacteristicWriteType.withResponse)
        }
    }
    
    func startScan() {
        self.peripheral = nil
        guard self.centralManager.state == .poweredOn else { return }
        
        let services:[CBUUID] = [CBUUID(string: BLEServiceUuid)]
        self.centralManager.scanForPeripherals(withServices: services, options: nil)
        self.flowController?.scanStarted() // 5.
        print("scan started")
    }
    
    func stopScan() {
        self.centralManager.stopScan()
        self.flowController?.scanStopped() // 5.
        print("scan stopped\n")
    }
    
    func connect() {
        guard self.centralManager.state == .poweredOn else { return }
        guard let peripheral = self.peripheral else { return }
        self.centralManager.connect(peripheral)
    }
    
    func disconnect() {
        guard let peripheral = self.peripheral else { return }
        self.centralManager.cancelPeripheralConnection(peripheral)
    }
    
    public func sendCommand(msgId: Byte, data: Data )
    {
        let message = createMessage(msgId: msgId, rw: mWrite, payload: data)
        //messageService.printData(data: message)
        self.sendData(message: message)
    }
    
    public func requestData(msgId: Byte, data: Data )
    {
        let message = createMessage(msgId: msgId, rw: mRead, payload: data)
        //messageService.printMutablePointer(data: message)
        self.sendData(message: message)
    }
    
    public func createMessage(msgId: Byte, rw: Byte, payload: Data) -> Data
    {
        let pbuffer = messageService.create_pack(readwrite: rw, command: msgId, dataSend: payload)
        return pbuffer;
    }
}

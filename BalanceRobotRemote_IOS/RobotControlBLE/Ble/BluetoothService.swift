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
    
    
    let BLEServiceUuid  =   "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
    let BLERxUuid       =   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
    let BLETxUuid       =   "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
    
    var centralManager: CBCentralManager!
    var peripheral: CBPeripheral?
    var txCharacteristic:CBCharacteristic? = nil
    var rxCharacteristic:CBCharacteristic? = nil
    let messageService = MessageService()
    
    weak var delegate: BluetoothServiceDelegate?
    
    var bluetoothState: CBManagerState {
        return self.centralManager.state
    }
    var flowController: FlowController? // 3.
    
    override init() {
        super.init()
        self.centralManager = CBCentralManager(delegate: self, queue: nil)
    }
    
    var characteristicsDiscoveredCallback: (() -> Void)?
    
    func sendData(message: Data) {
        if let rxChar = self.rxCharacteristic {
            // First check what properties are available
            if rxChar.properties.contains(.write) {
                self.peripheral?.writeValue(message, for: rxChar, type: .withResponse)
            }
            else if rxChar.properties.contains(.writeWithoutResponse) {
                self.peripheral?.writeValue(message, for: rxChar, type: .withoutResponse)
            }
            else {
                print("❌ Characteristic doesn't support writing!")
            }
        } else {
            print("❌ Cannot send data - RX characteristic not found")
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
    
    func connect(completion: @escaping (Bool) -> Void) {
        guard self.centralManager.state == .poweredOn else {
            completion(false)
            return
        }
        guard let peripheral = self.peripheral else {
            completion(false)
            return
        }
        
        // Set the callback
        characteristicsDiscoveredCallback = {
            completion(true)
        }
        
        // Connect to the peripheral
        self.centralManager.connect(peripheral)
        
        // Set a timeout
        DispatchQueue.main.asyncAfter(deadline: .now() + 5) {
            if self.rxCharacteristic == nil || self.txCharacteristic == nil {
                self.characteristicsDiscoveredCallback = nil
                completion(false)
            }
        }
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

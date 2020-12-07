//
//  BluetoothCommands.swift
//  RobotControlBLE
//
//  Created by TÜRKAY BİLİYOR on 29.03.2019.
//  Copyright © 2019 TÜRKAY BİLİYOR. All rights reserved.
//

import Foundation
import CoreBluetooth

extension BluetoothService {
    
    func getRxValue() {
        self.peripheral?.readValue(for: self.rxCharacteristic!)
    }
    
    func getTxValue() {
        self.peripheral?.readValue(for: self.txCharacteristic!)
    }
    
    
    // TODO: add other methods to expose high level requests to peripheral
}

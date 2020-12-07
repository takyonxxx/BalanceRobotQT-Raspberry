//
//  CustomAlert.swift
//  RobotControlBLE
//
//  Created by TÜRKAY BİLİYOR on 29.03.2019.
//  Copyright © 2019 TÜRKAY BİLİYOR. All rights reserved.
//

import UIKit

class PIDSettings: UIView, Modal {
    
    var backgroundView = UIView()
    var dialogView = UIView()
    
    var exitButton = UIButton()
    
    var pValueLabel = UILabel() // Proportional
    var pSlider = UISlider()
    
    var iValueLabel = UILabel() // Integral
    var iSlider = UISlider()
    
    var dValueLabel = UILabel() // Derivative
    var dSlider = UISlider()
    
    var sdValueLabel = UILabel() // Speed diff constant
    var sdSlider = UISlider()
    
    var acValueLabel = UILabel() // Angle Correction
    var acSlider = UISlider()
    
    let itemWidth = 30
    let itemHeight = 30
    let itemSpace = 30
    
    static let shared = PIDSettings()
    let messageService = MessageService()
    
    convenience init() {
        self.init(frame: UIScreen.main.bounds)
    }
    
    override init(frame: CGRect) {
        super.init(frame: frame)
        initialize(title: "Pid Settings")
    }
    
    required init?(coder aDecoder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }
    
    @objc func changePValue(_ sender: UISlider)
    {
        let formatted = String(format: "%.0f", sender.value)
        var valueInt = UInt8(sender.value)
        let data = Data(bytes: &valueInt, count: MemoryLayout.size(ofValue: valueInt))
        BluetoothService.shared.sendCommand(msgId: mPP, data: data)
        pValueLabel.text = formatted
    }
    
    @objc func changeIValue(_ sender: UISlider)
    {
        let formatted = String(format: "%.1f", sender.value / 10.0)
        var valueInt = UInt8(sender.value)
        let data = Data(bytes: &valueInt, count: MemoryLayout.size(ofValue: valueInt))
        BluetoothService.shared.sendCommand(msgId: mPI, data: data)
        iValueLabel.text = formatted
    }
    
    @objc func changeDValue(_ sender: UISlider)
    {
        let formatted = String(format: "%.1f", sender.value / 10.0)
        var valueInt = UInt8(sender.value)
        let data = Data(bytes: &valueInt, count: MemoryLayout.size(ofValue: valueInt))
        BluetoothService.shared.sendCommand(msgId: mPD, data: data)
        dValueLabel.text = formatted
    }
    
    @objc func changeSDValue(_ sender: UISlider)
    {
        let formatted = String(format: "%.1f", sender.value / 10.0)
        var valueInt = UInt8(sender.value)
        let data = Data(bytes: &valueInt, count: MemoryLayout.size(ofValue: valueInt))
        BluetoothService.shared.sendCommand(msgId: mSD, data: data)
        sdValueLabel.text = formatted
    }
    
    @objc func changeACValue(_ sender: UISlider)
    {
        let formatted = String(format: "%.1f", sender.value / 10.0)
        var valueInt = UInt8(sender.value)
        let data = Data(bytes: &valueInt, count: MemoryLayout.size(ofValue: valueInt))
        BluetoothService.shared.sendCommand(msgId: mAC, data: data)
        acValueLabel.text = formatted
    }
    
    @objc func setPValue(value: Float)
    {
        let formatted = String(format: "%.0f", value)
        pValueLabel.text = formatted
        pSlider.setValue(Float(value), animated: false)
    }
    
    @objc func setIValue(value: Float)
    {
        let formatted = String(format: "%.1f", value / 10.0)
        iValueLabel.text = formatted
        iSlider.setValue(Float(value), animated: false)
    }
    
    @objc func setDValue(value: Float)
    {
        let formatted = String(format: "%.1f", value / 10.0)
        dValueLabel.text = formatted
        dSlider.setValue(Float(value), animated: false)
    }
    
    @objc func setSDValue(value: Float)
    {
        let formatted = String(format: "%.1f", value / 10.0)
        sdValueLabel.text = formatted
        sdSlider.setValue(Float(value), animated: false)
    }
    
    @objc func setACValue(value: Float)
    {
        let formatted = String(format: "%.1f", value / 10.0)
        acValueLabel.text = formatted
        acSlider.setValue(Float(value), animated: false)
    }
    
    @objc func exitAction(sender: UIButton!) {
        self.dismiss(animated: true)
    }
    
    func initialize(title:String){
        
        dialogView.clipsToBounds = true
        
        backgroundView.frame = frame
        backgroundView.backgroundColor = UIColor.black
        backgroundView.alpha = 0.6
        backgroundView.addGestureRecognizer(UITapGestureRecognizer(target: self, action: #selector(didTappedOnBackgroundView)))
        addSubview(backgroundView)
        
        let dialogViewWidth = frame.width-64
        
        let titleLabel = UILabel(frame: CGRect(x: 8, y: 8, width: Int(dialogViewWidth - 16), height: itemHeight))
        titleLabel.text = title
        titleLabel.textAlignment = .center
        dialogView.addSubview(titleLabel)
        
        let separatorLineView = UIView()
        separatorLineView.frame.origin = CGPoint(x: 0, y: titleLabel.frame.height + 8)
        separatorLineView.frame.size = CGSize(width: dialogViewWidth, height: 1)
        separatorLineView.backgroundColor = UIColor.groupTableViewBackground
        dialogView.addSubview(separatorLineView)
        
        /////P ITEMS
        let ypPos = Int(titleLabel.frame.height + separatorLineView.frame.height) + Int(itemSpace)
    
        let pLabel = UILabel(frame: CGRect(x: 8, y: ypPos, width: itemWidth, height: itemHeight))
        pLabel.text = "P"
        pLabel.textAlignment = .center
        dialogView.addSubview(pLabel)
        
        pSlider = UISlider()
        pSlider.frame.origin = CGPoint(x: pLabel.frame.width + 8, y: CGFloat(ypPos))
        pSlider.frame.size = CGSize(width: dialogViewWidth - 90, height: pLabel.frame.height)
        
        pSlider.minimumTrackTintColor = .green
        pSlider.maximumTrackTintColor = .red
        pSlider.thumbTintColor = .black
        
        pSlider.maximumValue = 100
        pSlider.minimumValue = 0
        setPValue(value : 0)
        
        pSlider.addTarget(self, action: #selector(self.changePValue(_:)), for: .valueChanged)
        
        dialogView.addSubview(pSlider)
        
        pValueLabel = UILabel(frame: CGRect(x: Int(pLabel.frame.width + pSlider.frame.width),
                                            y: ypPos, width: 2 * itemWidth , height: Int(pLabel.frame.height)))
        
        pValueLabel.text = "0"
        pValueLabel.textAlignment = .center
        dialogView.addSubview(pValueLabel)
        
        /////I ITEMS
        
        let yiPos = 2 * (Int(titleLabel.frame.height + separatorLineView.frame.height) + Int(itemSpace))
        
        let iLabel = UILabel(frame: CGRect(x: 8, y: yiPos, width: itemWidth, height: itemHeight))
        iLabel.text = "I"
        iLabel.textAlignment = .center
        dialogView.addSubview(iLabel)
        
        iSlider = UISlider()
        iSlider.frame.origin = CGPoint(x: iLabel.frame.width + 8, y: CGFloat(yiPos))
        iSlider.frame.size = CGSize(width: dialogViewWidth - 90, height: iLabel.frame.height)
        
        iSlider.minimumTrackTintColor = .green
        iSlider.maximumTrackTintColor = .red
        iSlider.thumbTintColor = .black
        
        iSlider.maximumValue = 100
        iSlider.minimumValue = 0
        setIValue(value : 0)
        
        iSlider.addTarget(self, action: #selector(self.changeIValue(_:)), for: .valueChanged)
        
        dialogView.addSubview(iSlider)
        
        iValueLabel = UILabel(frame: CGRect(x: Int(iLabel.frame.width + iSlider.frame.width ),
                                            y: yiPos, width: 2 * itemWidth, height: Int(iLabel.frame.height)))
        
        iValueLabel.text = "0"
        iValueLabel.textAlignment = .center
        dialogView.addSubview(iValueLabel)
        
        /////D ITEMS
        
        let ydPos = 3 * (Int(titleLabel.frame.height + separatorLineView.frame.height) + Int(itemSpace))
        
        let dLabel = UILabel(frame: CGRect(x: 8, y: ydPos, width: itemWidth, height: itemHeight))
        dLabel.text = "D"
        dLabel.textAlignment = .center
        dialogView.addSubview(dLabel)
        
        dSlider = UISlider()
        dSlider.frame.origin = CGPoint(x: dLabel.frame.width + 8, y: CGFloat(ydPos))
        dSlider.frame.size = CGSize(width: dialogViewWidth - 90, height: dLabel.frame.height)
        
        dSlider.minimumTrackTintColor = .green
        dSlider.maximumTrackTintColor = .red
        dSlider.thumbTintColor = .black
        
        dSlider.maximumValue = 100
        dSlider.minimumValue = 0
        setDValue(value : 0)
        
        dSlider.addTarget(self, action: #selector(self.changeDValue(_:)), for: .valueChanged)
        
        dialogView.addSubview(dSlider)
        
        dValueLabel = UILabel(frame: CGRect(x: Int(dLabel.frame.width + iSlider.frame.width ),
                                            y: ydPos, width: 2 * itemWidth, height: Int(dLabel.frame.height)))
        
        dValueLabel.text = "0"
        dValueLabel.textAlignment = .center
        dialogView.addSubview(dValueLabel)
        
        /////SD ITEMS
        
        let ysdPos = 4 * (Int(titleLabel.frame.height + separatorLineView.frame.height) + Int(itemSpace))
        
        let sdLabel = UILabel(frame: CGRect(x: 8, y: ysdPos, width: itemWidth, height: itemHeight))
        sdLabel.text = "SD"
        sdLabel.textAlignment = .center
        dialogView.addSubview(sdLabel)
        
        sdSlider = UISlider()
        sdSlider.frame.origin = CGPoint(x: sdLabel.frame.width + 8, y: CGFloat(ysdPos))
        sdSlider.frame.size = CGSize(width: dialogViewWidth - 90, height: sdLabel.frame.height)
        
        sdSlider.minimumTrackTintColor = .green
        sdSlider.maximumTrackTintColor = .red
        sdSlider.thumbTintColor = .black
        
        sdSlider.maximumValue = 100
        sdSlider.minimumValue = 0
        setSDValue(value : 0)
        
        sdSlider.addTarget(self, action: #selector(self.changeSDValue(_:)), for: .valueChanged)
        
        dialogView.addSubview(sdSlider)
        
        sdValueLabel = UILabel(frame: CGRect(x: Int(sdLabel.frame.width + sdSlider.frame.width ),
                                            y: ysdPos, width: 2 * itemWidth, height: Int(sdLabel.frame.height)))
        
        sdValueLabel.text = "0"
        sdValueLabel.textAlignment = .center
        dialogView.addSubview(sdValueLabel)
        
        /////AC ITEMS
        
        let yacPos = 5 * (Int(titleLabel.frame.height + separatorLineView.frame.height) + Int(itemSpace))
        
        let acLabel = UILabel(frame: CGRect(x: 8, y: yacPos, width: itemWidth, height: itemHeight))
        acLabel.text = "AC"
        acLabel.textAlignment = .center
        dialogView.addSubview(acLabel)
        
        acSlider = UISlider()
        acSlider.frame.origin = CGPoint(x: acLabel.frame.width + 8, y: CGFloat(yacPos))
        acSlider.frame.size = CGSize(width: dialogViewWidth - 90, height: acLabel.frame.height)
        
        acSlider.minimumTrackTintColor = .green
        acSlider.maximumTrackTintColor = .red
        acSlider.thumbTintColor = .black
        
        acSlider.maximumValue = 100
        acSlider.minimumValue = 0
        setACValue(value : 0)
        
        acSlider.addTarget(self, action: #selector(self.changeACValue(_:)), for: .valueChanged)
        
        dialogView.addSubview(acSlider)
        
        acValueLabel = UILabel(frame: CGRect(x: Int(acLabel.frame.width + acSlider.frame.width ),
                                             y: yacPos, width: 2 * itemWidth, height: Int(acLabel.frame.height)))
        
        acValueLabel.text = "0"
        acValueLabel.textAlignment = .center
        dialogView.addSubview(acValueLabel)
        
        ///////Exit Button
        
        let yButtonPos = 6 * (Int(titleLabel.frame.height + separatorLineView.frame.height) + Int(itemSpace))
        
        exitButton = UIButton(frame: CGRect(x: Int(dialogViewWidth / 2 - 50), y: yButtonPos, width: 100, height: 35))
        exitButton.backgroundColor = UIColor.gray
        exitButton.setTitle("Close", for: .normal)
        exitButton.addTarget(self, action: #selector(exitAction), for: .touchUpInside)
        
        dialogView.addSubview(exitButton)
    
        ///////Dialog
        
        let dialogViewHeight = titleLabel.frame.height + 8 + separatorLineView.frame.height + 400
        
        dialogView.frame.origin = CGPoint(x: 32, y: frame.height)
        dialogView.frame.size = CGSize(width: frame.width-64, height: dialogViewHeight)
        dialogView.backgroundColor = UIColor.white
        dialogView.layer.cornerRadius = 6
        addSubview(dialogView)
    }
    
    @objc func didTappedOnBackgroundView(){
        dismiss(animated: true)
    }
    
}

//
//  TelemetryView.swift
//  RobotControlBLE
//
//  Canlı telemetri göstergesi: açı, gyro, PWM, durum ışıkları.
//

import UIKit

class TelemetryView: UIView {
    
    private let stack = UIStackView()
    
    let angleLabel    = UILabel()
    let gyroLabel     = UILabel()
    let pwmLabel      = UILabel()
    let stateLabel    = UILabel()
    let tiltContainer = UIView()
    let tiltIndicator = UIView()
    
    override init(frame: CGRect) {
        super.init(frame: frame)
        setup()
    }
    
    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setup()
    }
    
    private func setup() {
        // Telemetri paneli — ekran zeminine göre belirginleşen orta ton
        backgroundColor = .compatPanelBackground
        layer.cornerRadius = 12
        layer.shadowColor = UIColor.black.cgColor
        layer.shadowOpacity = 0.20
        layer.shadowOffset = CGSize(width: 0, height: 2)
        layer.shadowRadius = 6
        
        stack.axis = .vertical
        stack.alignment = .fill
        stack.spacing = 3
        stack.distribution = .fill
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(stack)
        
        // monospacedSystemFont iOS 13+ — fallback to Menlo on older versions
        let monoFont: UIFont
        if #available(iOS 13.0, *) {
            monoFont = .monospacedSystemFont(ofSize: 12, weight: .medium)
        } else {
            monoFont = UIFont(name: "Menlo", size: 12) ?? .systemFont(ofSize: 12)
        }
        
        let labels = [angleLabel, gyroLabel, pwmLabel, stateLabel]
        for lab in labels {
            lab.font = monoFont
            lab.textColor = .compatPrimaryText
            stack.addArrangedSubview(lab)
        }
        
        // Tilt bar — koyu zeminde, ekrana göre daha açık bir kutu
        tiltContainer.backgroundColor = UIColor(red: 0.10, green: 0.11, blue: 0.13, alpha: 1.0)
        tiltContainer.layer.cornerRadius = 6
        tiltContainer.translatesAutoresizingMaskIntoConstraints = false
        addSubview(tiltContainer)
        
        tiltIndicator.backgroundColor = .compatSystemGreen
        tiltIndicator.layer.cornerRadius = 4
        tiltContainer.addSubview(tiltIndicator)
        
        // Mid line
        let midLine = UIView()
        midLine.backgroundColor = .compatSeparator
        midLine.translatesAutoresizingMaskIntoConstraints = false
        tiltContainer.addSubview(midLine)
        NSLayoutConstraint.activate([
            midLine.centerXAnchor.constraint(equalTo: tiltContainer.centerXAnchor),
            midLine.topAnchor.constraint(equalTo: tiltContainer.topAnchor, constant: 2),
            midLine.bottomAnchor.constraint(equalTo: tiltContainer.bottomAnchor, constant: -2),
            midLine.widthAnchor.constraint(equalToConstant: 1),
        ])
        
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: topAnchor, constant: 6),
            stack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            stack.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),
            
            tiltContainer.topAnchor.constraint(equalTo: stack.bottomAnchor, constant: 6),
            tiltContainer.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            tiltContainer.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),
            tiltContainer.heightAnchor.constraint(equalToConstant: 10),
            tiltContainer.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -6),
        ])
        
        update(telemetry: RobotTelemetry())
    }
    
    override func layoutSubviews() {
        super.layoutSubviews()
        // tiltIndicator pozisyonu update() içinde frame ile veriliyor
    }
    
    func update(telemetry t: RobotTelemetry) {
        // 4 lines: angle+gyro, target+trim, PWM, status
        angleLabel.text = String(format: "Angle %+6.2f°  Gyro %+5.1f°/s", t.angle, t.gyroRate)
        gyroLabel.text  = String(format: "Target %+5.2f°   Trim %+5.2f°", t.targetAngle, t.trim)
        pwmLabel.text   = String(format: "PWM  L %+4d   R %+4d", t.pwmL, t.pwmR)
        
        var parts: [String] = []
        parts.append(t.armed ? "● ARMED" : "○ DISARMED")
        if t.fallen       { parts.append("⚠ FALLEN") }
        if t.autoMode     { parts.append("AUTO") }
        if t.positionHold { parts.append("HOLD") }
        stateLabel.text = parts.joined(separator: "  ")
        
        // Tilt indicator: map -15..+15° to bar width
        let maxAngle: Float = 15.0
        let clamped = max(-maxAngle, min(maxAngle, t.angle))
        let frac = (clamped + maxAngle) / (2 * maxAngle)
        let w = tiltContainer.bounds.width
        let h = tiltContainer.bounds.height
        let indicatorW: CGFloat = 8
        let x = CGFloat(frac) * (w - indicatorW)
        tiltIndicator.frame = CGRect(x: x, y: 1, width: indicatorW, height: h - 2)
        
        if abs(t.angle) > 10 {
            tiltIndicator.backgroundColor = .compatSystemRed
        } else if abs(t.angle) > 5 {
            tiltIndicator.backgroundColor = .compatSystemOrange
        } else {
            tiltIndicator.backgroundColor = .compatSystemGreen
        }
        
        if t.fallen {
            stateLabel.textColor = UIColor(red: 1.00, green: 0.42, blue: 0.42, alpha: 1.0)
        } else if t.armed {
            stateLabel.textColor = UIColor(red: 0.40, green: 0.95, blue: 0.50, alpha: 1.0)
        } else {
            stateLabel.textColor = UIColor(white: 0.65, alpha: 1.0)
        }
    }
}

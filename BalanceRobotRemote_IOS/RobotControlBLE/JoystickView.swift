//
//  JoystickView.swift
//  RobotControlBLE
//
//  Virtual analog joystick (touch-drag). Emits normalized (-1...1)
//  x and y values, recenters on touch release.
//

import UIKit

protocol JoystickViewDelegate: AnyObject {
    /// x: -1 (sol) .. +1 (sağ), y: -1 (geri) .. +1 (ileri)
    func joystick(_ view: JoystickView, didMoveTo x: Float, y: Float)
    func joystickDidRelease(_ view: JoystickView)
}

class JoystickView: UIView {
    weak var delegate: JoystickViewDelegate?
    
    private let baseView   = UIView()
    private let thumbView  = UIView()
    
    /// Tracking
    private var isTouching = false
    private var radius: CGFloat { return min(bounds.width, bounds.height) / 2.0 - 4 }
    private var thumbRadius: CGFloat { return radius * 0.4 }
    
    override init(frame: CGRect) {
        super.init(frame: frame)
        setup()
    }
    
    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setup()
    }
    
    private func setup() {
        backgroundColor = .clear
        isMultipleTouchEnabled = false
        
        baseView.backgroundColor = UIColor.compatSystemGray5
        baseView.layer.borderColor = UIColor.compatSystemGray3.cgColor
        baseView.layer.borderWidth = 1
        addSubview(baseView)
        
        thumbView.backgroundColor = UIColor.compatSystemBlue
        thumbView.layer.shadowColor = UIColor.black.cgColor
        thumbView.layer.shadowOpacity = 0.25
        thumbView.layer.shadowOffset = CGSize(width: 0, height: 2)
        thumbView.layer.shadowRadius = 4
        addSubview(thumbView)
    }
    
    override func layoutSubviews() {
        super.layoutSubviews()
        let r = radius
        baseView.frame = CGRect(x: bounds.midX - r, y: bounds.midY - r, width: r*2, height: r*2)
        baseView.layer.cornerRadius = r
        recenterThumb()
    }
    
    private func recenterThumb() {
        let tr = thumbRadius
        thumbView.frame = CGRect(x: bounds.midX - tr, y: bounds.midY - tr, width: tr*2, height: tr*2)
        thumbView.layer.cornerRadius = tr
    }
    
    private func moveThumb(to p: CGPoint) {
        let center = CGPoint(x: bounds.midX, y: bounds.midY)
        var dx = p.x - center.x
        var dy = p.y - center.y
        let dist = hypot(dx, dy)
        let r = radius
        if dist > r {
            dx = dx * r / dist
            dy = dy * r / dist
        }
        let tr = thumbRadius
        thumbView.frame = CGRect(x: center.x + dx - tr, y: center.y + dy - tr, width: tr*2, height: tr*2)
        
        let nx = Float(dx / r)         // -1..1
        let ny = Float(-dy / r)        // ekran y aşağıya doğru artar → ters çevir
                                       // yukarı çek → ny > 0
        // DEBUG: iOS'tan gerçekte hangi y değerinin gittiğini doğrulamak için
        NSLog("[JS] dy=\(dy) ny=\(ny) (yukarı=pozitif olmalı)")
        delegate?.joystick(self, didMoveTo: nx, y: ny)
    }
    
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let t = touches.first else { return }
        isTouching = true
        moveThumb(to: t.location(in: self))
    }
    
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard isTouching, let t = touches.first else { return }
        moveThumb(to: t.location(in: self))
    }
    
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        endTouch()
    }
    
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        endTouch()
    }
    
    private func endTouch() {
        isTouching = false
        UIView.animate(withDuration: 0.15) {
            self.recenterThumb()
        }
        delegate?.joystickDidRelease(self)
    }
}

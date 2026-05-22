//
//  TelemetryView.swift
//  RobotControlBLE
//
//  Canlı telemetri paneli: metric tile grid + durum rozetleri + tilt çubuğu.
//

import UIKit

// MARK: - MetricTile
/// Tek bir metrik (Angle/Gyro/PWM/...) için kart.
/// Üstte küçük başlık, altta büyük değer + birim.
private final class MetricTile: UIView {
    private let titleLabel = UILabel()
    private let valueLabel = UILabel()
    private let unitLabel  = UILabel()

    init(title: String, unit: String) {
        super.init(frame: .zero)
        backgroundColor = UIColor(red: 0.11, green: 0.12, blue: 0.14, alpha: 1.0)
        layer.cornerRadius = 8
        layer.borderWidth = 0.5
        layer.borderColor = UIColor(white: 1.0, alpha: 0.06).cgColor

        titleLabel.attributedText = NSAttributedString(
            string: title.uppercased(),
            attributes: [.kern: 0.8,
                         .foregroundColor: UIColor.compatSecondaryText,
                         .font: UIFont.systemFont(ofSize: 9, weight: .semibold)]
        )
        titleLabel.translatesAutoresizingMaskIntoConstraints = false
        addSubview(titleLabel)

        // Tabular figures: sayılar yer değiştirirken sütun kaymıyor
        let valFont: UIFont
        if #available(iOS 13.0, *) {
            valFont = .monospacedDigitSystemFont(ofSize: 18, weight: .semibold)
        } else {
            valFont = UIFont(name: "Menlo-Bold", size: 17) ?? .boldSystemFont(ofSize: 17)
        }
        valueLabel.font = valFont
        valueLabel.textColor = .compatPrimaryText
        valueLabel.text = "—"
        valueLabel.adjustsFontSizeToFitWidth = true
        valueLabel.minimumScaleFactor = 0.7
        valueLabel.translatesAutoresizingMaskIntoConstraints = false
        addSubview(valueLabel)

        unitLabel.text = unit
        unitLabel.font = .systemFont(ofSize: 11, weight: .regular)
        unitLabel.textColor = .compatSecondaryText
        unitLabel.translatesAutoresizingMaskIntoConstraints = false
        addSubview(unitLabel)

        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: topAnchor, constant: 6),
            titleLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 8),
            titleLabel.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -8),

            valueLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 8),
            valueLabel.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -6),

            unitLabel.leadingAnchor.constraint(equalTo: valueLabel.trailingAnchor, constant: 3),
            unitLabel.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -6),
            unitLabel.firstBaselineAnchor.constraint(equalTo: valueLabel.firstBaselineAnchor),
        ])
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func setValue(_ text: String, color: UIColor = .compatPrimaryText) {
        valueLabel.text = text
        valueLabel.textColor = color
    }
}

// MARK: - StatusBadge
/// Renkli pill rozet (ARMED / FALLEN / AUTO / HOLD).
private final class StatusBadge: UIView {
    private let label = UILabel()
    private let dot   = UIView()
    private var fillColor: UIColor = .compatSystemGray

    init(text: String) {
        super.init(frame: .zero)
        layer.cornerRadius = 4
        layer.borderWidth = 1
        translatesAutoresizingMaskIntoConstraints = false

        dot.layer.cornerRadius = 4
        dot.translatesAutoresizingMaskIntoConstraints = false
        addSubview(dot)

        label.text = text
        label.font = .systemFont(ofSize: 13, weight: .semibold)
        label.translatesAutoresizingMaskIntoConstraints = false
        addSubview(label)

        NSLayoutConstraint.activate([
            heightAnchor.constraint(equalToConstant: 28),
            dot.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 8),
            dot.centerYAnchor.constraint(equalTo: centerYAnchor),
            dot.widthAnchor.constraint(equalToConstant: 8),
            dot.heightAnchor.constraint(equalToConstant: 8),
            label.leadingAnchor.constraint(equalTo: dot.trailingAnchor, constant: 7),
            label.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),
            label.centerYAnchor.constraint(equalTo: centerYAnchor),
        ])

        setActive(false, color: .compatSystemGray)
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func setActive(_ active: Bool, color: UIColor) {
        fillColor = color
        if active {
            backgroundColor = color.withAlphaComponent(0.18)
            layer.borderColor = color.withAlphaComponent(0.55).cgColor
            label.textColor = color
            dot.backgroundColor = color
        } else {
            backgroundColor = UIColor(white: 1.0, alpha: 0.03)
            layer.borderColor = UIColor(white: 1.0, alpha: 0.08).cgColor
            label.textColor = UIColor(white: 0.45, alpha: 1.0)
            dot.backgroundColor = UIColor(white: 0.35, alpha: 1.0)
        }
    }
}

// MARK: - TelemetryView
class TelemetryView: UIView {

    // Metric tiles
    private let angleTile  = MetricTile(title: "Angle",  unit: "°")
    private let gyroTile   = MetricTile(title: "Gyro",   unit: "°/s")
    private let targetTile = MetricTile(title: "Target", unit: "°")
    private let trimTile   = MetricTile(title: "Trim",   unit: "°")
    private let pwmLTile   = MetricTile(title: "PWM L",  unit: "")
    private let pwmRTile   = MetricTile(title: "PWM R",  unit: "")

    // Status badges
    private let armedBadge  = StatusBadge(text: "ARMED")
    private let fallenBadge = StatusBadge(text: "FALLEN")
    private let autoBadge   = StatusBadge(text: "AUTO")
    private let holdBadge   = StatusBadge(text: "HOLD")

    // Tilt bar
    private let tiltContainer = UIView()
    private let tiltIndicator = UIView()
    private let tiltMinLabel  = UILabel()
    private let tiltMaxLabel  = UILabel()

    override init(frame: CGRect) {
        super.init(frame: frame)
        setup()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setup()
    }

    private func setup() {
        backgroundColor = .compatPanelBackground
        layer.cornerRadius = 14
        layer.shadowColor = UIColor.black.cgColor
        layer.shadowOpacity = 0.25
        layer.shadowOffset = CGSize(width: 0, height: 3)
        layer.shadowRadius = 8

        // ─── Tile grid: 3 sütun × 2 satır ───
        let row1 = UIStackView(arrangedSubviews: [angleTile, gyroTile, targetTile])
        let row2 = UIStackView(arrangedSubviews: [trimTile, pwmLTile, pwmRTile])
        for row in [row1, row2] {
            row.axis = .horizontal
            row.distribution = .fillEqually
            row.spacing = 6
            row.translatesAutoresizingMaskIntoConstraints = false
        }

        let gridStack = UIStackView(arrangedSubviews: [row1, row2])
        gridStack.axis = .vertical
        gridStack.spacing = 6
        gridStack.distribution = .fillEqually
        gridStack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(gridStack)

        // ─── Status badges ───
        let badgeStack = UIStackView(arrangedSubviews: [armedBadge, fallenBadge, autoBadge, holdBadge])
        badgeStack.axis = .horizontal
        badgeStack.spacing = 6
        badgeStack.distribution = .fillProportionally
        badgeStack.alignment = .center
        badgeStack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(badgeStack)

        // ─── Tilt bar ───
        tiltContainer.backgroundColor = UIColor(red: 0.08, green: 0.09, blue: 0.11, alpha: 1.0)
        tiltContainer.layer.cornerRadius = 5
        tiltContainer.layer.borderWidth = 0.5
        tiltContainer.layer.borderColor = UIColor(white: 1.0, alpha: 0.05).cgColor
        tiltContainer.translatesAutoresizingMaskIntoConstraints = false
        addSubview(tiltContainer)

        // Mid line — sıfır referansı, biraz daha belirgin
        let midLine = UIView()
        midLine.backgroundColor = UIColor(white: 0.45, alpha: 0.6)
        midLine.translatesAutoresizingMaskIntoConstraints = false
        tiltContainer.addSubview(midLine)
        NSLayoutConstraint.activate([
            midLine.centerXAnchor.constraint(equalTo: tiltContainer.centerXAnchor),
            midLine.topAnchor.constraint(equalTo: tiltContainer.topAnchor, constant: 1),
            midLine.bottomAnchor.constraint(equalTo: tiltContainer.bottomAnchor, constant: -1),
            midLine.widthAnchor.constraint(equalToConstant: 1),
        ])

        tiltIndicator.backgroundColor = .compatSystemGreen
        tiltIndicator.layer.cornerRadius = 2
        tiltContainer.addSubview(tiltIndicator)

        // Kenar etiketleri
        for (lab, txt) in [(tiltMinLabel, "−15°"), (tiltMaxLabel, "+15°")] {
            lab.text = txt
            lab.font = .systemFont(ofSize: 9, weight: .regular)
            lab.textColor = .compatSecondaryText
            lab.translatesAutoresizingMaskIntoConstraints = false
            addSubview(lab)
        }

        // ─── Layout ───
        NSLayoutConstraint.activate([
            gridStack.topAnchor.constraint(equalTo: topAnchor, constant: 10),
            gridStack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            gridStack.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),

            // Her tile yaklaşık 54pt yüksek (2 satır → 114pt + 6 boşluk)
            row1.heightAnchor.constraint(equalToConstant: 50),
            row2.heightAnchor.constraint(equalToConstant: 50),

            badgeStack.topAnchor.constraint(equalTo: gridStack.bottomAnchor, constant: 10),
            badgeStack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            badgeStack.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -10),

            tiltMinLabel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            tiltMinLabel.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -8),

            tiltMaxLabel.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),
            tiltMaxLabel.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -8),

            tiltContainer.leadingAnchor.constraint(equalTo: tiltMinLabel.trailingAnchor, constant: 8),
            tiltContainer.trailingAnchor.constraint(equalTo: tiltMaxLabel.leadingAnchor, constant: -8),
            tiltContainer.centerYAnchor.constraint(equalTo: tiltMinLabel.centerYAnchor),
            tiltContainer.heightAnchor.constraint(equalToConstant: 8),
        ])

        update(telemetry: RobotTelemetry())
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        // tiltIndicator pozisyonu update() içinde frame ile veriliyor; bounds
        // değişince yeniden yerleştirmek için son telemetriyi sakla diyebilirdik
        // ama update() her telemetri tick'inde çağrılıyor, sorun olmaz.
    }

    // MARK: - Public
    func update(telemetry t: RobotTelemetry) {
        // Sayısal değerler
        angleTile.setValue (String(format: "%+.2f", t.angle),       color: angleColor(t.angle))
        gyroTile.setValue  (String(format: "%+.1f", t.gyroRate),    color: gyroColor(t.gyroRate))
        targetTile.setValue(String(format: "%+.2f", t.targetAngle))
        trimTile.setValue  (String(format: "%+.2f", t.trim))
        pwmLTile.setValue  (String(format: "%+d",  t.pwmL),         color: pwmColor(t.pwmL))
        pwmRTile.setValue  (String(format: "%+d",  t.pwmR),         color: pwmColor(t.pwmR))

        // Durum rozetleri
        armedBadge.setActive(t.armed,         color: t.armed ? .compatSystemGreen : .compatSystemGray)
        fallenBadge.setActive(t.fallen,       color: .compatSystemRed)
        autoBadge.setActive(t.autoMode,       color: .compatAccent)
        holdBadge.setActive(t.positionHold,   color: .compatSystemOrange)

        // ARMED rozeti: aktifken yeşil, pasifken yine de "DISARMED" demek istesek
        // metin değiştiremiyoruz (badge label sabit). O yüzden DISARMED durumu pasif
        // ARMED rozetiyle yeterince net.

        // Tilt indicator
        let maxAngle: Float = 15.0
        let clamped = max(-maxAngle, min(maxAngle, t.angle))
        let frac = (clamped + maxAngle) / (2 * maxAngle)
        let w = tiltContainer.bounds.width
        let h = tiltContainer.bounds.height
        let indicatorW: CGFloat = 10
        if w > indicatorW {
            let x = CGFloat(frac) * (w - indicatorW)
            tiltIndicator.frame = CGRect(x: x, y: 1, width: indicatorW, height: h - 2)
        }
        tiltIndicator.backgroundColor = angleColor(t.angle)
    }

    // MARK: - Renk mantığı
    private func angleColor(_ angle: Float) -> UIColor {
        let a = abs(angle)
        if a > 10 { return .compatSystemRed }
        if a > 5  { return .compatSystemOrange }
        return .compatPrimaryText   // normal: nötr — ARMED rozeti zaten yeşil
    }

    private func gyroColor(_ rate: Float) -> UIColor {
        let r = abs(rate)
        if r > 90 { return .compatSystemOrange }
        return .compatPrimaryText
    }

    private func pwmColor(_ pwm: Int16) -> UIColor {
        let v = abs(Int(pwm))
        if v > 200 { return .compatSystemOrange }
        return .compatPrimaryText
    }
}

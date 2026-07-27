//
//  RobotCommandExecutor.swift
//  RobotControlBLE
//
//  Claude'un tool çağrılarını BLE komutlarına çevirir.
//  Güvenlik sınırları burada uygulanır: hız yüzdesi joystick tavanlarına
//  göre ölçeklenir, hareket süresi 5 saniye ile sınırlanır ve süre dolunca
//  komut otomatik olarak sıfırlanır.
//

import Foundation

final class RobotCommandExecutor {

    static let shared = RobotCommandExecutor()
    private init() {}

    private let ble = BluetoothService.shared
    private var moveStopTimer: Timer?

    // MARK: - Tool tanımları (Claude API "tools" alanı)

    static let toolDefinitions: [[String: Any]] = [
        [
            "name": "move_robot",
            "description": "Move the balance robot. Direction forward/backward drives it, left/right turns it in place. The command is automatically stopped after duration_seconds. Use this when the user asks the robot to go somewhere or turn.",
            "input_schema": [
                "type": "object",
                "properties": [
                    "direction": ["type": "string", "enum": ["forward", "backward", "left", "right"],
                                  "description": "Movement direction"],
                    "speed_percent": ["type": "integer", "minimum": 10, "maximum": 100,
                                      "description": "Speed as percent of the configured maximum. Default 50."],
                    "duration_seconds": ["type": "number", "minimum": 0.3, "maximum": 5,
                                         "description": "How long to apply the command. Default 1.5."]
                ],
                "required": ["direction"]
            ]
        ],
        [
            "name": "stop_robot",
            "description": "Immediately zero all motion commands (the robot keeps balancing). Use for 'dur', 'stop'.",
            "input_schema": ["type": "object", "properties": [String: Any]()]
        ],
        [
            "name": "set_armed",
            "description": "Arm (start balancing) or disarm (motors off) the robot.",
            "input_schema": [
                "type": "object",
                "properties": ["armed": ["type": "boolean"]],
                "required": ["armed"]
            ]
        ],
        [
            "name": "start_pid_learning",
            "description": "Start the on-robot PID auto-tuning (learning) mode. The robot must be balancing on a flat hard floor with space around it; it will wobble on purpose for a few minutes while it optimizes its balance PID gains. Use for 'PID ogrenmeyi baslat', 'kendini ayarla', 'stabilizasyonu ogren'.",
            "input_schema": ["type": "object", "properties": [String: Any]()]
        ],
        [
            "name": "stop_pid_learning",
            "description": "Stop the PID auto-tuning mode early. The best gains found so far are kept.",
            "input_schema": ["type": "object", "properties": [String: Any]()]
        ],
        [
            "name": "get_robot_status",
            "description": "Read the robot's live telemetry: pitch angle, target angle, trim, motor PWM values, armed/fallen state, PID learning state. Use when the user asks how the robot is doing.",
            "input_schema": ["type": "object", "properties": [String: Any]()]
        ],
        [
            "name": "reset_trim",
            "description": "Reset the balance trim integrator to zero. Use if the robot keeps creeping in one direction while standing.",
            "input_schema": ["type": "object", "properties": [String: Any]()]
        ]
    ]

    // MARK: - Yürütme

    var isConnected: Bool {
        return ble.peripheral != nil && ble.rxCharacteristic != nil
    }

    /// Bir tool çağrısını çalıştır, Claude'a dönecek tool_result metnini üret.
    func execute(name: String, input: [String: Any]) -> String {
        // get_robot_status bağlantı yokken de "not connected" dönebilsin.
        guard isConnected || name == "get_robot_status" else {
            return "ERROR: robot is not connected over Bluetooth. Ask the user to connect from the Control tab."
        }

        switch name {
        case "move_robot":
            return executeMove(input)
        case "stop_robot":
            stopAllMotion()
            return "All motion commands zeroed. Robot keeps balancing."
        case "set_armed":
            let armed = (input["armed"] as? Bool) ?? true
            sendByte(msgId: armed ? mArmed : mDisArmed, value: 0)
            return armed ? "Robot armed (balancing)." : "Robot disarmed (motors off)."
        case "start_pid_learning":
            sendByte(msgId: mPidLearn, value: 1)
            return "PID learning started. It runs for a few minutes; progress lines will appear in the app. The robot will wobble on purpose - that is the tuner injecting test pushes."
        case "stop_pid_learning":
            sendByte(msgId: mPidLearn, value: 0)
            return "PID learning stop requested. Best gains found so far will be committed."
        case "get_robot_status":
            return statusText()
        case "reset_trim":
            sendByte(msgId: mResetTrim, value: 0)
            return "Trim reset to zero."
        default:
            return "ERROR: unknown tool \(name)"
        }
    }

    // MARK: - Hareket

    private func executeMove(_ input: [String: Any]) -> String {
        guard let dir = input["direction"] as? String else {
            return "ERROR: direction missing"
        }
        let pct = min(max((input["speed_percent"] as? Int) ?? 50, 10), 100)
        var duration = (input["duration_seconds"] as? Double)
                    ?? (input["duration_seconds"] as? Int).map(Double.init)
                    ?? 1.5
        duration = min(max(duration, 0.3), 5.0)

        let fwdMax  = AppSettings.shared.forwardSpeed
        let turnMax = AppSettings.shared.turnSpeed

        var msgId: Byte
        var pwm: Int
        switch dir {
        case "forward":  msgId = mForward;  pwm = Int(Float(pct) / 100.0 * fwdMax)
        case "backward": msgId = mBackward; pwm = Int(Float(pct) / 100.0 * fwdMax)
        case "left":     msgId = mLeft;     pwm = Int(Float(pct) / 100.0 * turnMax)
        case "right":    msgId = mRight;    pwm = Int(Float(pct) / 100.0 * turnMax)
        default: return "ERROR: invalid direction \(dir)"
        }
        pwm = min(max(pwm, 0), 255)

        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            self.moveStopTimer?.invalidate()
            self.sendByte(msgId: msgId, value: UInt8(pwm))
            self.moveStopTimer = Timer.scheduledTimer(withTimeInterval: duration,
                                                      repeats: false) { [weak self] _ in
                self?.stopAllMotion()
            }
        }
        return "Moving \(dir) at \(pct)% (PWM \(pwm)) for \(String(format: "%.1f", duration)) s, then auto-stop."
    }

    func stopAllMotion() {
        moveStopTimer?.invalidate()
        moveStopTimer = nil
        sendByte(msgId: mForward,  value: 0)
        sendByte(msgId: mBackward, value: 0)
        sendByte(msgId: mLeft,     value: 0)
        sendByte(msgId: mRight,    value: 0)
    }

    // MARK: - Durum

    func statusText() -> String {
        guard isConnected else { return "Robot is NOT connected over Bluetooth." }
        guard let t = ble.lastTelemetry else {
            return "Robot is connected but no telemetry received yet."
        }
        return String(format:
            "angle=%.2f deg, target=%.2f deg, trim=%.2f deg, gyro=%.1f deg/s, " +
            "pwmL=%d, pwmR=%d, armed=%@, fallen=%@, autoMode=%@, pidLearning=%@",
            t.angle, t.targetAngle, t.trim, t.gyroRate,
            t.pwmL, t.pwmR,
            t.armed ? "yes" : "no",
            t.fallen ? "YES" : "no",
            t.autoMode ? "on" : "off",
            t.pidLearning ? "ACTIVE" : "off")
    }

    // MARK: - BLE helper

    private func sendByte(msgId: Byte, value: UInt8) {
        // Claude API cevapları URLSession arka plan kuyruğunda gelir;
        // BLE yazmalarını uygulamanın geri kalanıyla aynı thread'de
        // (main) yapalım.
        let work = { [weak self] in
            guard let self = self else { return }
            var v = value
            let data = Data(bytes: &v, count: 1)
            self.ble.sendCommand(msgId: msgId, data: data)
        }
        if Thread.isMainThread { work() } else { DispatchQueue.main.async(execute: work) }
    }
}

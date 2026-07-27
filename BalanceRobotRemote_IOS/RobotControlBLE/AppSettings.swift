//
//  AppSettings.swift
//  RobotControlBLE
//
//  App-side persistent settings (iOS only — robot has its own).
//  Lives in UserDefaults so values survive across launches.
//

import Foundation

final class AppSettings {
    static let shared = AppSettings()
    
    private let defaults = UserDefaults.standard
    
    // Keys
    private let kForwardSpeed = "joystick.forwardSpeed"
    private let kTurnSpeed    = "joystick.turnSpeed"
    
    // Defaults (current hard-coded values)
    private let defaultForwardSpeed: Float = 180
    private let defaultTurnSpeed:    Float = 60
    
    // Public ranges
    let forwardSpeedRange: ClosedRange<Float> = 50...255
    let turnSpeedRange:    ClosedRange<Float> = 10...200
    
    /// Maximum forward/backward PWM applied at full joystick deflection.
    var forwardSpeed: Float {
        get {
            if defaults.object(forKey: kForwardSpeed) == nil { return defaultForwardSpeed }
            return defaults.float(forKey: kForwardSpeed)
        }
        set {
            defaults.set(newValue, forKey: kForwardSpeed)
            NotificationCenter.default.post(name: AppSettings.didChange, object: nil)
        }
    }
    
    /// Maximum left/right PWM applied at full joystick deflection.
    var turnSpeed: Float {
        get {
            if defaults.object(forKey: kTurnSpeed) == nil { return defaultTurnSpeed }
            return defaults.float(forKey: kTurnSpeed)
        }
        set {
            defaults.set(newValue, forKey: kTurnSpeed)
            NotificationCenter.default.post(name: AppSettings.didChange, object: nil)
        }
    }
    
    // MARK: - Claude asistanı
    
    private let kClaudeApiKey   = "claude.apiKey"
    private let kClaudeModel    = "claude.model"
    private let kSpeakReplies   = "claude.speakReplies"
    
    /// Anthropic API anahtarı (sk-ant-...). Assistant sekmesinden girilir.
    var claudeApiKey: String {
        get { defaults.string(forKey: kClaudeApiKey) ?? "" }
        set { defaults.set(newValue, forKey: kClaudeApiKey) }
    }
    
    /// Kullanılacak Claude modeli.
    var claudeModel: String {
        get { defaults.string(forKey: kClaudeModel) ?? "claude-sonnet-4-6" }
        set { defaults.set(newValue, forKey: kClaudeModel) }
    }
    
    /// Claude cevapları sesli okunsun mu (AVSpeechSynthesizer).
    var speakReplies: Bool {
        get {
            if defaults.object(forKey: kSpeakReplies) == nil { return true }
            return defaults.bool(forKey: kSpeakReplies)
        }
        set { defaults.set(newValue, forKey: kSpeakReplies) }
    }
    
    static let didChange = Notification.Name("AppSettings.didChange")
}

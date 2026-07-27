//
//  LocalCommandParser.swift
//  RobotControlBLE
//
//  Çevrimdışı, API'siz sesli komut çözümleyici.
//  Claude API anahtarı girilmemişse asistan sekmesi bu sınıfı kullanır:
//  anahtar kelime eşleştirmesiyle Türkçe/İngilizce robot komutlarını
//  RobotCommandExecutor'a yönlendirir. Ücretsizdir, internet gerektirmez.
//
//  Sohbet/soru-cevap yeteneği YOKTUR - yalnızca robot komutları.
//

import Foundation

final class LocalCommandParser {

    static let shared = LocalCommandParser()
    private init() {}

    /// Metni çözümle, gerekiyorsa robotu sür, konuşulacak cevabı döndür.
    func handle(_ rawText: String) -> String {
        let text = rawText.lowercased(with: Locale(identifier: "tr_TR"))

        // ---- Durdurma (en yüksek öncelik) ----
        if containsAny(text, ["dur", "stop", "kes", "bekle"]) &&
           !containsAny(text, ["durum", "durdur"]) {
            _ = RobotCommandExecutor.shared.execute(name: "stop_robot", input: [:])
            return "Durdum."
        }

        // ---- PID öğrenme ----
        if text.contains("pid") || text.contains("öğren") || text.contains("learn") {
            if containsAny(text, ["durdur", "bitir", "iptal", "stop", "kapat"]) {
                return RobotCommandExecutor.shared.execute(name: "stop_pid_learning", input: [:])
            }
            if containsAny(text, ["başlat", "başla", "start", "aç"]) {
                return RobotCommandExecutor.shared.execute(name: "start_pid_learning", input: [:])
            }
        }

        // ---- Trim sıfırlama ----
        if text.contains("trim") &&
           containsAny(text, ["sıfırla", "resetle", "reset", "temizle"]) {
            return RobotCommandExecutor.shared.execute(name: "reset_trim", input: [:])
        }

        // ---- Durum sorgusu ----
        if containsAny(text, ["durum", "nasıl", "status", "telemetri", "açı", "denge"]) {
            return RobotCommandExecutor.shared.execute(name: "get_robot_status", input: [:])
        }

        // ---- Arm / Disarm ----
        if containsAny(text, ["disarm", "motorları kapat", "motoru kapat", "devre dışı"]) {
            return RobotCommandExecutor.shared.execute(name: "set_armed", input: ["armed": false])
        }
        if containsAny(text, ["arm", "hazırlan", "dengele", "motorları aç", "motoru aç"]) {
            return RobotCommandExecutor.shared.execute(name: "set_armed", input: ["armed": true])
        }

        // ---- Hareket ----
        var direction: String?
        if containsAny(text, ["ileri", "öne", "forward"])            { direction = "forward" }
        else if containsAny(text, ["geri", "arkaya", "back"])        { direction = "backward" }
        else if containsAny(text, ["sol", "left"])                   { direction = "left" }
        else if containsAny(text, ["sağ", "right"])                  { direction = "right" }

        if let dir = direction {
            var input: [String: Any] = ["direction": dir]
            if let secs = extractSeconds(from: text) {
                input["duration_seconds"] = secs
            }
            if let spd = extractSpeed(from: text) {
                input["speed_percent"] = spd
            }
            return RobotCommandExecutor.shared.execute(name: "move_robot", input: input)
        }

        // ---- Yardım / anlaşılamadı ----
        return "Anlayamadım. Komutlar: ileri git, geri git, sola dön, sağa dön, "
             + "dur, durum ne, PID öğrenmeyi başlat, trim sıfırla, motorları kapat. "
             + "Süre için \"iki saniye ileri\", hız için \"hızlı\" veya \"yavaş\" diyebilirsin."
    }

    // MARK: - Yardımcılar

    private func containsAny(_ text: String, _ keys: [String]) -> Bool {
        for k in keys where text.contains(k) { return true }
        return false
    }

    /// "2 saniye", "iki saniye", "0.5 sn" gibi kalıplardan süre çıkarır.
    private func extractSeconds(from text: String) -> Double? {
        guard containsAny(text, ["saniye", "sn", "second", "sec"]) else { return nil }
        if let num = firstNumber(in: text) {
            return min(max(num, 0.3), 5.0)
        }
        return nil
    }

    /// "hızlı", "yavaş", "%60", "yüzde 60" gibi kalıplardan hız çıkarır.
    private func extractSpeed(from text: String) -> Int? {
        if text.contains("%") || text.contains("yüzde") || text.contains("percent") {
            if let num = firstNumber(in: text) {
                return min(max(Int(num), 10), 100)
            }
        }
        if containsAny(text, ["çok hızlı", "tam gaz", "full"]) { return 90 }
        if containsAny(text, ["hızlı", "fast"])                { return 75 }
        if containsAny(text, ["yavaş", "slow", "nazik"])       { return 30 }
        return nil
    }

    /// Metindeki ilk sayıyı bulur - rakam ("2", "0.5") veya Türkçe sayı sözcüğü.
    private func firstNumber(in text: String) -> Double? {
        // Rakamlar (ondalık nokta/virgül destekli)
        let pattern = "[0-9]+([.,][0-9]+)?"
        if let range = text.range(of: pattern, options: .regularExpression) {
            let numStr = text[range].replacingOccurrences(of: ",", with: ".")
            if let v = Double(numStr) { return v }
        }
        // Türkçe / İngilizce sayı sözcükleri
        let words: [(String, Double)] = [
            ("yarım", 0.5), ("buçuk", 1.5),
            ("bir", 1), ("iki", 2), ("üç", 3), ("dört", 4), ("beş", 5),
            ("altı", 6), ("yedi", 7), ("sekiz", 8), ("dokuz", 9), ("on", 10),
            ("one", 1), ("two", 2), ("three", 3), ("four", 4), ("five", 5)
        ]
        for (w, v) in words where text.contains(w) { return v }
        return nil
    }
}

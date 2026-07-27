//
//  GeminiService.swift
//  RobotControlBLE
//
//  Google Gemini API istemcisi + function-calling ajan döngüsü.
//  Claude'un ÜCRETSİZ KOTALI alternatifi: aistudio.google.com'dan alınan
//  API anahtarı kredi kartı gerektirmez, günlük ücretsiz kota ile çalışır.
//
//  Akış ClaudeService ile aynıdır:
//    1. Kullanıcı metni geçmişe eklenir, generateContent'e gönderilir.
//    2. Model functionCall parçaları döndürürse RobotCommandExecutor
//       çalıştırır, functionResponse geri beslenir (maks 4 tur).
//    3. Nihai metin UI'a döner.
//
//  Tool tanımları RobotCommandExecutor.toolDefinitions'tan (Claude
//  formatı) otomatik dönüştürülür - tek kaynak, iki sağlayıcı.
//

import Foundation

final class GeminiService {

    static let shared = GeminiService()
    private init() {}

    enum GeminiError: LocalizedError {
        case noApiKey
        case badResponse(String)

        var errorDescription: String? {
            switch self {
            case .noApiKey:
                return "Gemini API key is not set. Enter it in the Assistant tab."
            case .badResponse(let msg):
                return "Gemini API error: \(msg)"
            }
        }
    }

    private let maxToolRounds = 4

    /// Konuşma geçmişi - Gemini "contents" dizisiyle aynı yapı.
    private var history: [[String: Any]] = []
    private let historyLimit = 24

    func resetConversation() {
        history.removeAll()
    }

    // MARK: - Ana giriş noktası

    func send(userText: String,
              onToolActivity: ((String) -> Void)? = nil,
              completion: @escaping (Result<String, Error>) -> Void) {

        let apiKey = AppSettings.shared.geminiApiKey
            .trimmingCharacters(in: .whitespacesAndNewlines)
        guard !apiKey.isEmpty else {
            DispatchQueue.main.async { completion(.failure(GeminiError.noApiKey)) }
            return
        }

        history.append(["role": "user", "parts": [["text": userText]]])
        trimHistory()

        runRound(apiKey: apiKey, round: 0,
                 onToolActivity: onToolActivity, completion: completion)
    }

    // MARK: - Ajan döngüsü

    private func runRound(apiKey: String, round: Int,
                          onToolActivity: ((String) -> Void)?,
                          completion: @escaping (Result<String, Error>) -> Void) {

        let model = AppSettings.shared.geminiModel
        guard let url = URL(string:
            "https://generativelanguage.googleapis.com/v1beta/models/\(model):generateContent")
        else {
            DispatchQueue.main.async {
                completion(.failure(GeminiError.badResponse("bad model name")))
            }
            return
        }

        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.timeoutInterval = 60
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.setValue(apiKey, forHTTPHeaderField: "x-goog-api-key")

        let body: [String: Any] = [
            "system_instruction": ["parts": [["text": systemPrompt()]]],
            "contents": history,
            "tools": [["function_declarations": Self.functionDeclarations()]],
            "generationConfig": ["maxOutputTokens": 1024]
        ]

        do {
            request.httpBody = try JSONSerialization.data(withJSONObject: body)
        } catch {
            DispatchQueue.main.async { completion(.failure(error)) }
            return
        }

        URLSession.shared.dataTask(with: request) { [weak self] data, response, error in
            guard let self = self else { return }

            if let error = error {
                DispatchQueue.main.async { completion(.failure(error)) }
                return
            }
            guard let data = data,
                  let json = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else {
                DispatchQueue.main.async {
                    completion(.failure(GeminiError.badResponse("empty or non-JSON response")))
                }
                return
            }

            // Hata gövdesi: {"error": {"code":.., "message":.., "status":..}}
            if let errObj = json["error"] as? [String: Any] {
                var msg = (errObj["message"] as? String) ?? "unknown"
                if (errObj["code"] as? Int) == 429 {
                    msg = "Ücretsiz kota doldu, biraz bekleyip tekrar dene. (" + msg + ")"
                }
                DispatchQueue.main.async {
                    completion(.failure(GeminiError.badResponse(msg)))
                }
                return
            }

            guard let candidates = json["candidates"] as? [[String: Any]],
                  let content = candidates.first?["content"] as? [String: Any],
                  let parts = content["parts"] as? [[String: Any]] else {
                // Güvenlik filtresi vb. durumlarda candidates boş dönebilir.
                DispatchQueue.main.async {
                    completion(.failure(GeminiError.badResponse("missing candidates/parts")))
                }
                return
            }

            // Model turunu geçmişe olduğu gibi ekle (functionCall dahil).
            self.history.append(["role": "model", "parts": parts])

            // Metin + functionCall parçalarını ayıkla.
            var textParts: [String] = []
            var calls: [(name: String, args: [String: Any])] = []
            for part in parts {
                if let t = part["text"] as? String {
                    textParts.append(t)
                }
                if let fc = part["functionCall"] as? [String: Any],
                   let name = fc["name"] as? String {
                    let args = (fc["args"] as? [String: Any]) ?? [:]
                    calls.append((name, args))
                }
            }

            if !calls.isEmpty, round < self.maxToolRounds {
                var responseParts: [[String: Any]] = []
                for call in calls {
                    let result = RobotCommandExecutor.shared.execute(name: call.name,
                                                                     input: call.args)
                    DispatchQueue.main.async {
                        onToolActivity?("\(call.name): \(result)")
                    }
                    responseParts.append([
                        "functionResponse": [
                            "name": call.name,
                            "response": ["result": result]
                        ]
                    ])
                }
                self.history.append(["role": "user", "parts": responseParts])
                self.runRound(apiKey: apiKey, round: round + 1,
                              onToolActivity: onToolActivity, completion: completion)
                return
            }

            let finalText = textParts.joined(separator: "\n")
                .trimmingCharacters(in: .whitespacesAndNewlines)
            DispatchQueue.main.async {
                completion(.success(finalText.isEmpty ? "(no reply)" : finalText))
            }
        }.resume()
    }

    // MARK: - Tool tanımı dönüştürme (Claude formatı -> Gemini formatı)

    /// RobotCommandExecutor.toolDefinitions (name/description/input_schema)
    /// -> Gemini function_declarations (name/description/parameters).
    /// Parametresiz tool'larda "parameters" alanı tamamen atlanır.
    private static func functionDeclarations() -> [[String: Any]] {
        return RobotCommandExecutor.toolDefinitions.map { tool in
            var decl: [String: Any] = [
                "name": tool["name"] ?? "",
                "description": tool["description"] ?? ""
            ]
            if let schema = tool["input_schema"] as? [String: Any],
               let props = schema["properties"] as? [String: Any],
               !props.isEmpty {
                decl["parameters"] = schema
            }
            return decl
        }
    }

    // MARK: - Sistem promptu (ClaudeService ile aynı içerik)

    private func systemPrompt() -> String {
        let status = RobotCommandExecutor.shared.statusText()
        return """
        You are the voice assistant built into the remote-control app of a \
        two-wheeled self-balancing robot (Raspberry Pi 5, MPU6050 IMU, encoder \
        motors, cascade PID control). The user talks to you by voice, usually in \
        Turkish - always reply in the language the user used, and keep replies \
        SHORT (1-3 sentences) because they are read aloud.

        You can control the robot with the provided tools (move, stop, arm/disarm, \
        PID learning mode, trim reset, status). Use tools when the user asks for an \
        action; answer directly when they just ask a question. Never invent \
        telemetry - use get_robot_status. If a tool reports the robot is not \
        connected, tell the user to connect from the Control tab.

        Safety rules: prefer moderate speeds (<=60%) unless the user explicitly \
        asks for fast; movement commands auto-stop after their duration; if the \
        user sounds worried the robot is falling, call stop_robot first.

        Current robot status:
        \(status)
        """
    }

    // MARK: - Geçmiş kırpma

    /// Geçmişi sınırla; ilk eleman her zaman düz metinli bir user turu olmalı
    /// (yalnız functionResponse içeren bir user turuyla başlamak geçersizdir).
    private func trimHistory() {
        while history.count > historyLimit {
            history.removeFirst()
        }
        while let first = history.first {
            let role = first["role"] as? String
            let parts = (first["parts"] as? [[String: Any]]) ?? []
            let hasPlainText = parts.contains { $0["text"] is String }
            if role == "user" && hasPlainText { break }
            history.removeFirst()
        }
    }
}

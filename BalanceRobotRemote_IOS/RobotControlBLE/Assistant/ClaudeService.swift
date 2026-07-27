//
//  ClaudeService.swift
//  RobotControlBLE
//
//  Anthropic Messages API istemcisi + tool-use ajan döngüsü.
//
//  Akış:
//    1. Kullanıcı metni konuşma geçmişine eklenir ve API'ye gönderilir.
//    2. Claude tool_use blokları döndürürse RobotCommandExecutor çalıştırır,
//       tool_result'lar geçmişe eklenir ve API tekrar çağrılır (maks 4 tur).
//    3. Nihai metin cevabı UI'a döner.
//
//  API anahtarı AppSettings.claudeApiKey içinde saklanır (UserDefaults).
//

import Foundation

final class ClaudeService {

    static let shared = ClaudeService()
    private init() {}

    enum ClaudeError: LocalizedError {
        case noApiKey
        case badResponse(String)

        var errorDescription: String? {
            switch self {
            case .noApiKey:
                return "Anthropic API key is not set. Enter it in the Assistant tab."
            case .badResponse(let msg):
                return "Claude API error: \(msg)"
            }
        }
    }

    private let endpoint = URL(string: "https://api.anthropic.com/v1/messages")!
    private let maxToolRounds = 4

    /// Konuşma geçmişi — Messages API "messages" dizisiyle aynı yapı.
    /// content ya String ya da content-block dizisi olabilir; ikisini de
    /// [String: Any] sözlükleriyle taşıyoruz.
    private var history: [[String: Any]] = []
    private let historyLimit = 24   // eski turları kırp (token tasarrufu)

    func resetConversation() {
        history.removeAll()
    }

    // MARK: - Ana giriş noktası

    /// Kullanıcı mesajını gönder; tool döngüsünü işlet; nihai metni döndür.
    /// completion main queue'da çağrılır. onToolActivity, çalıştırılan her
    /// tool için UI'a kısa bilgi verir ("move_robot → Moving forward...").
    func send(userText: String,
              onToolActivity: ((String) -> Void)? = nil,
              completion: @escaping (Result<String, Error>) -> Void) {

        let apiKey = AppSettings.shared.claudeApiKey
            .trimmingCharacters(in: .whitespacesAndNewlines)
        guard !apiKey.isEmpty else {
            DispatchQueue.main.async { completion(.failure(ClaudeError.noApiKey)) }
            return
        }

        history.append(["role": "user", "content": userText])
        trimHistory()

        runRound(apiKey: apiKey, round: 0,
                 onToolActivity: onToolActivity, completion: completion)
    }

    // MARK: - Ajan döngüsü

    private func runRound(apiKey: String, round: Int,
                          onToolActivity: ((String) -> Void)?,
                          completion: @escaping (Result<String, Error>) -> Void) {

        var request = URLRequest(url: endpoint)
        request.httpMethod = "POST"
        request.timeoutInterval = 60
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.setValue(apiKey, forHTTPHeaderField: "x-api-key")
        request.setValue("2023-06-01", forHTTPHeaderField: "anthropic-version")

        let body: [String: Any] = [
            "model": AppSettings.shared.claudeModel,
            "max_tokens": 1024,
            "system": systemPrompt(),
            "tools": RobotCommandExecutor.toolDefinitions,
            "messages": history
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
                    completion(.failure(ClaudeError.badResponse("empty or non-JSON response")))
                }
                return
            }

            // API hata gövdesi: {"type":"error","error":{"message":...}}
            if let errObj = json["error"] as? [String: Any] {
                let msg = (errObj["message"] as? String) ?? "unknown"
                DispatchQueue.main.async {
                    completion(.failure(ClaudeError.badResponse(msg)))
                }
                return
            }

            guard let content = json["content"] as? [[String: Any]] else {
                DispatchQueue.main.async {
                    completion(.failure(ClaudeError.badResponse("missing content")))
                }
                return
            }

            // Asistan turunu geçmişe olduğu gibi ekle (tool_use blokları dahil).
            self.history.append(["role": "assistant", "content": content])

            // Metin + tool_use bloklarını ayıkla.
            var textParts: [String] = []
            var toolUses: [(id: String, name: String, input: [String: Any])] = []
            for block in content {
                let type = block["type"] as? String
                if type == "text", let t = block["text"] as? String {
                    textParts.append(t)
                } else if type == "tool_use",
                          let id = block["id"] as? String,
                          let name = block["name"] as? String {
                    let input = (block["input"] as? [String: Any]) ?? [:]
                    toolUses.append((id, name, input))
                }
            }

            let stopReason = json["stop_reason"] as? String

            if stopReason == "tool_use", !toolUses.isEmpty, round < self.maxToolRounds {
                // Tool'ları çalıştır (BLE main thread'de gönderilir; execute
                // içindeki dispatch bunu hallediyor) ve sonuçları geri besle.
                var resultBlocks: [[String: Any]] = []
                for tu in toolUses {
                    let result = RobotCommandExecutor.shared.execute(name: tu.name, input: tu.input)
                    DispatchQueue.main.async {
                        onToolActivity?("\(tu.name): \(result)")
                    }
                    resultBlocks.append([
                        "type": "tool_result",
                        "tool_use_id": tu.id,
                        "content": result
                    ])
                }
                self.history.append(["role": "user", "content": resultBlocks])
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

    // MARK: - Sistem promptu

    private func systemPrompt() -> String {
        let status = RobotCommandExecutor.shared.statusText()
        return """
        You are Claude, the voice assistant built into the remote-control app of a \
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

        Current robot status: \(status)
        """
    }

    private func trimHistory() {
        // İlk mesajın DÜZ METİN içerikli bir "user" mesajı olması gerekir.
        // (tool_result içeren bir user mesajıyla başlarsak, referans verdiği
        // tool_use bloğu silinmiş olur ve API hata döndürür.)
        while history.count > historyLimit {
            history.removeFirst()
        }
        while let first = history.first,
              (first["role"] as? String) != "user" || !(first["content"] is String) {
            if history.isEmpty { break }
            history.removeFirst()
        }
    }
}

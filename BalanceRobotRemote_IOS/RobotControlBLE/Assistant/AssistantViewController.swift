//
//  AssistantViewController.swift
//  RobotControlBLE
//
//  Claude sesli asistan sekmesi.
//
//  Kullanım: mikrofon butonuna bas → konuş → tekrar bas (veya sustuğunda
//  durdur) → transkript Claude'a gider → cevap yazılır ve sesli okunur.
//  Claude gerekli görürse tool çağrılarıyla robotu hareket ettirir,
//  PID öğrenme modunu başlatır/durdurur, telemetri okur.
//

import UIKit
import AVFoundation

class AssistantViewController: UIViewController {

    // MARK: - UI
    private let chatView     = UITextView()
    private let statusLabel  = UILabel()
    private let micButton    = UIButton(type: .system)
    private let textField    = UITextField()
    private let sendButton   = UIButton(type: .system)
    private let speakSwitchRow = UIStackView()
    private let speakSwitch  = UISwitch()
    private let keyButton    = UIButton(type: .system)

    // MARK: - State
    private let recognizer = SpeechRecognizer()
    private let synthesizer = AVSpeechSynthesizer()
    private var busy = false {
        didSet { updateControls() }
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .compatScreenBackground
        title = "Claude"
        buildUI()
        wireRecognizer()

        NotificationCenter.default.addObserver(
            self, selector: #selector(onPidLearnStatus(_:)),
            name: BluetoothService.pidLearnStatus, object: nil)

        appendLine("system", "Merhaba! Mikrofona basıp konuşabilir veya yazabilirsin. " +
                             "Robotu sesli komutlarla sürebilir, PID öğrenme modunu " +
                             "başlatabilir ya da herhangi bir soru sorabilirsin.")
        if AppSettings.shared.claudeApiKey.isEmpty {
            appendLine("system", "⚠️ Önce Anthropic API anahtarını gir (sağ üstteki anahtar butonu).")
        }
    }

    deinit {
        NotificationCenter.default.removeObserver(self)
        recognizer.cancel()
    }

    // MARK: - UI build

    private func buildUI() {
        // Chat log
        chatView.isEditable = false
        chatView.backgroundColor = .compatPanelBackground
        chatView.layer.cornerRadius = 12
        chatView.textColor = .compatPrimaryText
        chatView.font = .systemFont(ofSize: 15)
        chatView.textContainerInset = UIEdgeInsets(top: 10, left: 8, bottom: 10, right: 8)
        chatView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(chatView)

        // Status
        statusLabel.font = .systemFont(ofSize: 13, weight: .medium)
        statusLabel.textColor = .compatSecondaryText
        statusLabel.textAlignment = .center
        statusLabel.text = "Hazır"
        statusLabel.numberOfLines = 2
        statusLabel.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(statusLabel)

        // Mic button — büyük yuvarlak
        if #available(iOS 13.0, *) {
            micButton.setImage(UIImage(systemName: "mic.fill"), for: .normal)
            micButton.tintColor = .white
        } else {
            micButton.setTitle("MIC", for: .normal)
            micButton.setTitleColor(.white, for: .normal)
        }
        micButton.backgroundColor = .compatSystemBlue
        micButton.layer.cornerRadius = 36
        micButton.translatesAutoresizingMaskIntoConstraints = false
        micButton.addTarget(self, action: #selector(micTapped), for: .touchUpInside)
        view.addSubview(micButton)

        // Text input + send (yazarak da sorulabilsin)
        textField.borderStyle = .roundedRect
        textField.placeholder = "Yaz veya mikrofonu kullan…"
        textField.returnKeyType = .send
        textField.delegate = self
        textField.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(textField)

        sendButton.setTitle("Gönder", for: .normal)
        sendButton.titleLabel?.font = .systemFont(ofSize: 15, weight: .semibold)
        sendButton.setTitleColor(.white, for: .normal)
        sendButton.backgroundColor = .compatSystemGreen
        sendButton.layer.cornerRadius = 8
        sendButton.translatesAutoresizingMaskIntoConstraints = false
        sendButton.addTarget(self, action: #selector(sendTapped), for: .touchUpInside)
        view.addSubview(sendButton)

        // Speak toggle + API key butonu
        let speakLabel = UILabel()
        speakLabel.text = "Cevapları sesli oku"
        speakLabel.font = .systemFont(ofSize: 14)
        speakLabel.textColor = .compatPrimaryText
        speakSwitch.isOn = AppSettings.shared.speakReplies
        speakSwitch.addTarget(self, action: #selector(speakSwitchChanged), for: .valueChanged)

        if #available(iOS 13.0, *) {
            keyButton.setImage(UIImage(systemName: "key.fill"), for: .normal)
        } else {
            keyButton.setTitle("API Key", for: .normal)
        }
        keyButton.addTarget(self, action: #selector(apiKeyTapped), for: .touchUpInside)

        speakSwitchRow.axis = .horizontal
        speakSwitchRow.spacing = 8
        speakSwitchRow.alignment = .center
        speakSwitchRow.addArrangedSubview(speakLabel)
        speakSwitchRow.addArrangedSubview(speakSwitch)
        speakSwitchRow.addArrangedSubview(UIView())   // spacer
        speakSwitchRow.addArrangedSubview(keyButton)
        speakSwitchRow.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(speakSwitchRow)

        let g = view.safeAreaLayoutGuide
        NSLayoutConstraint.activate([
            speakSwitchRow.topAnchor.constraint(equalTo: g.topAnchor, constant: 8),
            speakSwitchRow.leadingAnchor.constraint(equalTo: g.leadingAnchor, constant: 16),
            speakSwitchRow.trailingAnchor.constraint(equalTo: g.trailingAnchor, constant: -16),

            chatView.topAnchor.constraint(equalTo: speakSwitchRow.bottomAnchor, constant: 8),
            chatView.leadingAnchor.constraint(equalTo: g.leadingAnchor, constant: 16),
            chatView.trailingAnchor.constraint(equalTo: g.trailingAnchor, constant: -16),

            statusLabel.topAnchor.constraint(equalTo: chatView.bottomAnchor, constant: 8),
            statusLabel.leadingAnchor.constraint(equalTo: g.leadingAnchor, constant: 16),
            statusLabel.trailingAnchor.constraint(equalTo: g.trailingAnchor, constant: -16),

            micButton.topAnchor.constraint(equalTo: statusLabel.bottomAnchor, constant: 8),
            micButton.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            micButton.widthAnchor.constraint(equalToConstant: 72),
            micButton.heightAnchor.constraint(equalToConstant: 72),

            textField.topAnchor.constraint(equalTo: micButton.bottomAnchor, constant: 10),
            textField.leadingAnchor.constraint(equalTo: g.leadingAnchor, constant: 16),
            textField.heightAnchor.constraint(equalToConstant: 40),
            textField.bottomAnchor.constraint(equalTo: g.bottomAnchor, constant: -10),

            sendButton.centerYAnchor.constraint(equalTo: textField.centerYAnchor),
            sendButton.leadingAnchor.constraint(equalTo: textField.trailingAnchor, constant: 8),
            sendButton.trailingAnchor.constraint(equalTo: g.trailingAnchor, constant: -16),
            sendButton.widthAnchor.constraint(equalToConstant: 78),
            sendButton.heightAnchor.constraint(equalToConstant: 40),
        ])
    }

    private func updateControls() {
        micButton.isEnabled = !busy
        sendButton.isEnabled = !busy
        micButton.alpha = busy ? 0.5 : 1.0
        if recognizer.isRecording {
            micButton.backgroundColor = .compatSystemRed
        } else {
            micButton.backgroundColor = .compatSystemBlue
        }
    }

    // MARK: - Chat log

    private func appendLine(_ who: String, _ text: String) {
        let prefix: String
        switch who {
        case "user":   prefix = "🧑 "
        case "claude": prefix = "🤖 "
        case "tool":   prefix = "⚙️ "
        default:       prefix = "ℹ️ "
        }
        let current = chatView.text ?? ""
        chatView.text = current + (current.isEmpty ? "" : "\n\n") + prefix + text
        let end = NSRange(location: max(chatView.text.count - 1, 0), length: 1)
        chatView.scrollRangeToVisible(end)
    }

    // MARK: - Mikrofon akışı

    private func wireRecognizer() {
        recognizer.onPartialResult = { [weak self] text in
            self?.statusLabel.text = "🎙 " + text
        }
        recognizer.onFinalResult = { [weak self] text in
            guard let self = self else { return }
            self.updateControls()
            let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
            if trimmed.isEmpty {
                self.statusLabel.text = "Bir şey duyamadım — tekrar dene"
                return
            }
            self.sendToClaude(trimmed)
        }
        recognizer.onError = { [weak self] error in
            self?.statusLabel.text = "Ses hatası: \(error.localizedDescription)"
            self?.updateControls()
        }
    }

    @objc private func micTapped() {
        if recognizer.isRecording {
            recognizer.stop()
            statusLabel.text = "İşleniyor…"
            updateControls()
            return
        }
        // Cevap okunuyorsa kes — kullanıcı konuşmak istiyor.
        if synthesizer.isSpeaking {
            synthesizer.stopSpeaking(at: .immediate)
        }
        SpeechRecognizer.requestPermissions { [weak self] granted in
            guard let self = self else { return }
            guard granted else {
                self.statusLabel.text = "Mikrofon / konuşma tanıma izni gerekli (iOS Ayarlar)"
                return
            }
            self.recognizer.start()
            self.statusLabel.text = "🎙 Dinliyorum… (bitirmek için tekrar dokun)"
            self.updateControls()
        }
    }

    // MARK: - Yazılı gönderim

    @objc private func sendTapped() {
        let text = (textField.text ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }
        textField.text = ""
        textField.resignFirstResponder()
        sendToClaude(text)
    }

    // MARK: - Claude

    private func sendToClaude(_ text: String) {
        guard !AppSettings.shared.claudeApiKey.isEmpty else {
            promptForApiKey()
            return
        }
        appendLine("user", text)
        statusLabel.text = "Claude düşünüyor…"
        busy = true

        ClaudeService.shared.send(userText: text, onToolActivity: { [weak self] info in
            self?.appendLine("tool", info)
        }, completion: { [weak self] result in
            guard let self = self else { return }
            self.busy = false
            switch result {
            case .success(let reply):
                self.statusLabel.text = "Hazır"
                self.appendLine("claude", reply)
                if AppSettings.shared.speakReplies {
                    self.speak(reply)
                }
            case .failure(let error):
                self.statusLabel.text = "Hata"
                self.appendLine("system", "Hata: \(error.localizedDescription)")
                if case ClaudeService.ClaudeError.noApiKey = error {
                    self.promptForApiKey()
                }
            }
        })
    }

    // MARK: - TTS

    private func speak(_ text: String) {
        // Çalma için audio session'ı playback'e al (kayıt measurement modunda
        // kalmış olabilir).
        try? AVAudioSession.sharedInstance().setCategory(.playback, options: [.duckOthers])
        try? AVAudioSession.sharedInstance().setActive(true)

        let utterance = AVSpeechUtterance(string: text)
        // Basit dil sezgisi: Türkçe karakter varsa tr-TR, yoksa en-US.
        let turkish = text.rangeOfCharacter(from: CharacterSet(charactersIn: "çğıöşüÇĞİÖŞÜ")) != nil
        utterance.voice = AVSpeechSynthesisVoice(language: turkish ? "tr-TR" : "en-US")
        utterance.rate = AVSpeechUtteranceDefaultSpeechRate
        synthesizer.speak(utterance)
    }

    // MARK: - API key

    private func promptForApiKey() {
        let alert = UIAlertController(
            title: "Anthropic API Key",
            message: "console.anthropic.com üzerinden aldığın anahtarı gir (sk-ant-…). Cihazda saklanır.",
            preferredStyle: .alert)
        alert.addTextField { tf in
            tf.placeholder = "sk-ant-…"
            tf.isSecureTextEntry = true
            tf.text = AppSettings.shared.claudeApiKey
        }
        alert.addAction(UIAlertAction(title: "Kaydet", style: .default) { _ in
            let key = alert.textFields?.first?.text ?? ""
            AppSettings.shared.claudeApiKey = key.trimmingCharacters(in: .whitespacesAndNewlines)
            if !AppSettings.shared.claudeApiKey.isEmpty {
                self.appendLine("system", "API anahtarı kaydedildi.")
            }
        })
        alert.addAction(UIAlertAction(title: "İptal", style: .cancel))
        present(alert, animated: true)
    }

    @objc private func apiKeyTapped() {
        promptForApiKey()
    }

    @objc private func speakSwitchChanged() {
        AppSettings.shared.speakReplies = speakSwitch.isOn
        if !speakSwitch.isOn && synthesizer.isSpeaking {
            synthesizer.stopSpeaking(at: .immediate)
        }
    }

    // MARK: - PID learn durum satırları

    @objc private func onPidLearnStatus(_ note: Notification) {
        guard let status = note.userInfo?["status"] as? String else { return }
        DispatchQueue.main.async { [weak self] in
            self?.appendLine("tool", "PID: " + status)
        }
    }
}

// MARK: - UITextFieldDelegate

extension AssistantViewController: UITextFieldDelegate {
    func textFieldShouldReturn(_ textField: UITextField) -> Bool {
        sendTapped()
        return true
    }
}

//
//  SpeechRecognizer.swift
//  RobotControlBLE
//
//  AVAudioEngine + SFSpeechRecognizer sarmalayıcısı.
//  Mikrofonu dinler, canlı (partial) transkript üretir ve durdurulunca
//  nihai metni geri döndürür. Türkçe (tr-TR) öncelikli; cihaz Türkçe
//  tanıma desteklemiyorsa sistem diline, o da yoksa en-US'e düşer.
//

import Foundation
import Speech
import AVFoundation

final class SpeechRecognizer: NSObject {

    enum RecognizerError: LocalizedError {
        case notAuthorized
        case micNotAuthorized
        case recognizerUnavailable

        var errorDescription: String? {
            switch self {
            case .notAuthorized:        return "Speech recognition permission denied. Enable it in iOS Settings."
            case .micNotAuthorized:     return "Microphone permission denied. Enable it in iOS Settings."
            case .recognizerUnavailable: return "Speech recognizer is not available on this device."
            }
        }
    }

    /// Canlı kısmi transkript (her güncellemede çağrılır, main queue).
    var onPartialResult: ((String) -> Void)?
    /// Dinleme bitti — nihai metin (main queue). Boş olabilir.
    var onFinalResult: ((String) -> Void)?
    /// Hata (main queue).
    var onError: ((Error) -> Void)?

    private(set) var isRecording = false

    private let audioEngine = AVAudioEngine()
    private var recognizer: SFSpeechRecognizer?
    private var request: SFSpeechAudioBufferRecognitionRequest?
    private var task: SFSpeechRecognitionTask?
    private var latestTranscript = ""

    override init() {
        // tr-TR → sistem dili → en-US sırasıyla dene
        let candidates = [Locale(identifier: "tr-TR"), Locale.current, Locale(identifier: "en-US")]
        for locale in candidates {
            if let r = SFSpeechRecognizer(locale: locale) {
                if recognizer == nil { recognizer = r }   // ilk geçerli aday
                if r.isAvailable {                        // kullanılabilir olanı tercih et
                    recognizer = r
                    break
                }
            }
        }
        super.init()
    }

    // MARK: - İzinler

    static func requestPermissions(_ completion: @escaping (Bool) -> Void) {
        SFSpeechRecognizer.requestAuthorization { authStatus in
            guard authStatus == .authorized else {
                DispatchQueue.main.async { completion(false) }
                return
            }
            AVAudioSession.sharedInstance().requestRecordPermission { granted in
                DispatchQueue.main.async { completion(granted) }
            }
        }
    }

    // MARK: - Kayıt

    func start() {
        guard !isRecording else { return }
        latestTranscript = ""

        guard SFSpeechRecognizer.authorizationStatus() == .authorized else {
            onError?(RecognizerError.notAuthorized); return
        }
        guard AVAudioSession.sharedInstance().recordPermission == .granted else {
            onError?(RecognizerError.micNotAuthorized); return
        }
        guard let recognizer = recognizer, recognizer.isAvailable else {
            onError?(RecognizerError.recognizerUnavailable); return
        }

        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playAndRecord, mode: .measurement,
                                    options: [.defaultToSpeaker, .allowBluetoothA2DP])
            try session.setActive(true, options: .notifyOthersOnDeactivation)

            let request = SFSpeechAudioBufferRecognitionRequest()
            request.shouldReportPartialResults = true
            self.request = request

            let inputNode = audioEngine.inputNode
            let format = inputNode.outputFormat(forBus: 0)
            inputNode.removeTap(onBus: 0)
            inputNode.installTap(onBus: 0, bufferSize: 1024, format: format) { [weak self] buffer, _ in
                self?.request?.append(buffer)
            }

            audioEngine.prepare()
            try audioEngine.start()
            isRecording = true

            task = recognizer.recognitionTask(with: request) { [weak self] result, error in
                guard let self = self else { return }
                if let result = result {
                    let text = result.bestTranscription.formattedString
                    self.latestTranscript = text
                    DispatchQueue.main.async { self.onPartialResult?(text) }
                    if result.isFinal {
                        self.cleanup()
                        DispatchQueue.main.async { self.onFinalResult?(text) }
                    }
                }
                if let error = error {
                    let wasRecording = self.isRecording
                    let transcript = self.latestTranscript
                    self.cleanup()
                    DispatchQueue.main.async {
                        // Kullanıcı stop'a bastıktan sonra tanıyıcının attığı
                        // "cancelled" hatasını yutup elimizdeki metni teslim et.
                        if !transcript.isEmpty {
                            self.onFinalResult?(transcript)
                        } else if wasRecording {
                            self.onError?(error)
                        }
                    }
                }
            }
        } catch {
            cleanup()
            onError?(error)
        }
    }

    /// Dinlemeyi bitir; nihai sonuç onFinalResult ile gelir.
    func stop() {
        guard isRecording else { return }
        audioEngine.stop()
        audioEngine.inputNode.removeTap(onBus: 0)
        request?.endAudio()
        isRecording = false
        // Tanıyıcı isFinal ya da error ile kapanışı tamamlar; 1.5 s içinde
        // hiçbir şey gelmezse eldeki transkriptle bitir.
        let snapshot = latestTranscript
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { [weak self] in
            guard let self = self, self.task != nil else { return }
            self.cleanup()
            self.onFinalResult?(self.latestTranscript.isEmpty ? snapshot : self.latestTranscript)
        }
    }

    func cancel() {
        cleanup()
    }

    private func cleanup() {
        if audioEngine.isRunning {
            audioEngine.stop()
            audioEngine.inputNode.removeTap(onBus: 0)
        }
        request?.endAudio()
        task?.cancel()
        task = nil
        request = nil
        isRecording = false
        try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
    }
}

#ifndef LLMCLIENT_H
#define LLMCLIENT_H

//
// LlmClient - Pi tarafında Gemini / Claude REST istemcisi (tool destekli).
//
// Sağlayıcı önceliği iOS uygulamasıyla aynıdır:
//   Claude anahtarı varsa Claude -> yoksa Gemini anahtarı varsa Gemini.
// Gemini ücretsiz kotayla çalışır (aistudio.google.com/apikey).
//
// Tool çağrıları executor callback'ine verilir (VoiceAssistant robot
// eylemlerini çalıştırıp sonucu string döndürür); maksimum 4 tur.
//

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <functional>

class LlmClient : public QObject
{
    Q_OBJECT

public:
    using ToolExecutor = std::function<QString(const QString &name,
                                               const QJsonObject &args)>;
    using ReplyHandler = std::function<void(bool ok, const QString &textOrError)>;

    explicit LlmClient(QObject *parent = nullptr);

    void configure(const QString &claudeKey, const QString &claudeModel,
                   const QString &geminiKey, const QString &geminiModel);

    bool hasProvider() const { return !claudeKey_.isEmpty() || !geminiKey_.isEmpty(); }
    QString providerName() const { return !claudeKey_.isEmpty() ? "Claude" : "Gemini"; }

    void setSystemPromptExtra(const QString &s) { systemExtra_ = s; }

    // Tek eşzamanlı istek; meşgulken çağrılırsa hemen hata döner.
    void ask(const QString &userText, ToolExecutor executor, ReplyHandler handler);

    void resetConversation();

private:
    // Ortak
    void finish(bool ok, const QString &msg);
    QString systemPrompt() const;
    static QJsonArray claudeToolDefinitions();
    static QJsonArray geminiFunctionDeclarations();

    // Claude
    void claudeRound(int round);
    // Gemini
    void geminiRound(int round);

    QNetworkAccessManager nam_;
    QString claudeKey_, claudeModel_;
    QString geminiKey_, geminiModel_;
    QString systemExtra_;

    // Aktif istek durumu
    bool         busy_ = false;
    ToolExecutor executor_;
    ReplyHandler handler_;

    // Konuşma geçmişleri (sağlayıcıya özgü format)
    QJsonArray claudeHistory_;
    QJsonArray geminiHistory_;
    static constexpr int kHistoryLimit  = 24;
    static constexpr int kMaxToolRounds = 4;
};

#endif // LLMCLIENT_H

#include "llmclient.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>

LlmClient::LlmClient(QObject *parent)
    : QObject(parent)
{
}

void LlmClient::configure(const QString &claudeKey, const QString &claudeModel,
                          const QString &geminiKey, const QString &geminiModel)
{
    claudeKey_   = claudeKey.trimmed();
    claudeModel_ = claudeModel.isEmpty() ? "claude-sonnet-4-6" : claudeModel;
    geminiKey_   = geminiKey.trimmed();
    geminiModel_ = geminiModel.isEmpty() ? "gemini-2.0-flash" : geminiModel;
}

void LlmClient::resetConversation()
{
    claudeHistory_ = QJsonArray();
    geminiHistory_ = QJsonArray();
}

void LlmClient::ask(const QString &userText, ToolExecutor executor, ReplyHandler handler)
{
    if (busy_) {
        handler(false, "Onceki soru hala isleniyor, bekle.");
        return;
    }
    if (!hasProvider()) {
        handler(false, "API anahtari tanimli degil.");
        return;
    }
    busy_     = true;
    executor_ = executor;
    handler_  = handler;

    if (!claudeKey_.isEmpty()) {
        claudeHistory_.append(QJsonObject{{"role", "user"}, {"content", userText}});
        while (claudeHistory_.size() > kHistoryLimit) claudeHistory_.removeFirst();
        // İlk eleman düz metinli user turu olmalı (tool_result ile başlanamaz).
        while (!claudeHistory_.isEmpty()) {
            QJsonObject first = claudeHistory_.first().toObject();
            if (first.value("role").toString() == "user" &&
                first.value("content").isString()) break;
            claudeHistory_.removeFirst();
        }
        claudeRound(0);
    } else {
        QJsonObject turn{{"role", "user"},
                         {"parts", QJsonArray{QJsonObject{{"text", userText}}}}};
        geminiHistory_.append(turn);
        while (geminiHistory_.size() > kHistoryLimit) geminiHistory_.removeFirst();
        while (!geminiHistory_.isEmpty()) {
            QJsonObject first = geminiHistory_.first().toObject();
            bool plainUser = false;
            if (first.value("role").toString() == "user") {
                for (const auto &p : first.value("parts").toArray())
                    if (p.toObject().contains("text")) { plainUser = true; break; }
            }
            if (plainUser) break;
            geminiHistory_.removeFirst();
        }
        geminiRound(0);
    }
}

void LlmClient::finish(bool ok, const QString &msg)
{
    busy_ = false;
    ReplyHandler h = handler_;
    handler_  = ReplyHandler();
    executor_ = ToolExecutor();
    if (h) h(ok, msg);
}

// ---------------------------------------------------------------- prompts

QString LlmClient::systemPrompt() const
{
    return QString(
        "You are the voice assistant running ON a two-wheeled self-balancing "
        "robot (Raspberry Pi 5, MPU6050 IMU, encoder motors, cascade PID "
        "control). The user speaks to the robot's microphone, usually in "
        "Turkish - always reply in the language the user used, and keep "
        "replies SHORT (1-2 sentences) because they are spoken aloud through "
        "a small speaker.\n\n"
        "You can control the robot with the provided tools (move, stop, "
        "arm/disarm, PID learning mode, trim reset, status). Use tools when "
        "the user asks for an action; answer directly when they just ask a "
        "question. Never invent telemetry - use get_robot_status.\n\n"
        "Safety rules: prefer moderate speeds (<=60%) unless the user "
        "explicitly asks for fast; movement commands auto-stop after their "
        "duration; if the user sounds worried the robot is falling, call "
        "stop_robot first.\n\n"
        "Current robot status:\n") + systemExtra_;
}

// Tool tanımları — iOS RobotCommandExecutor.toolDefinitions ile aynı küme.
QJsonArray LlmClient::claudeToolDefinitions()
{
    auto obj = [](const char *json) {
        return QJsonDocument::fromJson(json).object();
    };
    QJsonArray tools;
    tools.append(obj(R"({
        "name": "move_robot",
        "description": "Move the robot in a direction for a limited duration, then auto-stop.",
        "input_schema": {
            "type": "object",
            "properties": {
                "direction": {"type": "string", "enum": ["forward","backward","left","right"]},
                "speed_percent": {"type": "integer", "minimum": 10, "maximum": 100,
                                  "description": "Speed as percent of the configured maximum. Default 50."},
                "duration_seconds": {"type": "number", "minimum": 0.3, "maximum": 5,
                                     "description": "How long to move before auto-stop. Default 1.5."}
            },
            "required": ["direction"]
        }})"));
    tools.append(obj(R"({
        "name": "stop_robot",
        "description": "Immediately zero all motion commands.",
        "input_schema": {"type": "object", "properties": {}}})"));
    tools.append(obj(R"({
        "name": "set_armed",
        "description": "Arm (true) or disarm (false) the balance controller.",
        "input_schema": {
            "type": "object",
            "properties": {"armed": {"type": "boolean"}},
            "required": ["armed"]
        }})"));
    tools.append(obj(R"({
        "name": "start_pid_learning",
        "description": "Start the Twiddle PID auto-tune (learning) mode. Robot must be balancing on a flat floor.",
        "input_schema": {"type": "object", "properties": {}}})"));
    tools.append(obj(R"({
        "name": "stop_pid_learning",
        "description": "Stop the PID auto-tune mode; the best gains found so far are kept.",
        "input_schema": {"type": "object", "properties": {}}})"));
    tools.append(obj(R"({
        "name": "get_robot_status",
        "description": "Read live telemetry: pitch angle, PWM, armed/fallen/learning flags.",
        "input_schema": {"type": "object", "properties": {}}})"));
    tools.append(obj(R"({
        "name": "reset_trim",
        "description": "Zero the trim integrator.",
        "input_schema": {"type": "object", "properties": {}}})"));
    return tools;
}

QJsonArray LlmClient::geminiFunctionDeclarations()
{
    // Claude formatından dönüştür: input_schema -> parameters,
    // parametresiz tool'larda alan tamamen atlanır.
    QJsonArray decls;
    for (const auto &t : claudeToolDefinitions()) {
        QJsonObject tool = t.toObject();
        QJsonObject decl{{"name", tool.value("name")},
                         {"description", tool.value("description")}};
        QJsonObject schema = tool.value("input_schema").toObject();
        if (!schema.value("properties").toObject().isEmpty())
            decl.insert("parameters", schema);
        decls.append(decl);
    }
    return decls;
}

// ---------------------------------------------------------------- Claude

void LlmClient::claudeRound(int round)
{
    QNetworkRequest req(QUrl("https://api.anthropic.com/v1/messages"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("x-api-key", claudeKey_.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");

    QJsonObject body{
        {"model",      claudeModel_},
        {"max_tokens", 1024},
        {"system",     systemPrompt()},
        {"tools",      claudeToolDefinitions()},
        {"messages",   claudeHistory_}
    };

    QNetworkReply *reply = nam_.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply->setProperty("round", round);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int round = reply->property("round").toInt();

        const QByteArray data = reply->readAll();
        QJsonObject json = QJsonDocument::fromJson(data).object();

        if (json.contains("error")) {
            finish(false, "Claude API: " +
                   json.value("error").toObject().value("message").toString("unknown"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError && json.isEmpty()) {
            finish(false, "Ag hatasi: " + reply->errorString());
            return;
        }

        QJsonArray content = json.value("content").toArray();
        claudeHistory_.append(QJsonObject{{"role", "assistant"}, {"content", content}});

        QStringList textParts;
        struct Call { QString id, name; QJsonObject input; };
        QList<Call> calls;
        for (const auto &b : content) {
            QJsonObject block = b.toObject();
            const QString type = block.value("type").toString();
            if (type == "text")
                textParts << block.value("text").toString();
            else if (type == "tool_use")
                calls.append({block.value("id").toString(),
                              block.value("name").toString(),
                              block.value("input").toObject()});
        }

        if (json.value("stop_reason").toString() == "tool_use" &&
            !calls.isEmpty() && round < kMaxToolRounds && executor_) {
            QJsonArray results;
            for (const auto &c : calls) {
                const QString r = executor_(c.name, c.input);
                results.append(QJsonObject{{"type", "tool_result"},
                                           {"tool_use_id", c.id},
                                           {"content", r}});
            }
            claudeHistory_.append(QJsonObject{{"role", "user"}, {"content", results}});
            claudeRound(round + 1);
            return;
        }

        const QString final = textParts.join("\n").trimmed();
        finish(true, final.isEmpty() ? "(cevap yok)" : final);
    });
}

// ---------------------------------------------------------------- Gemini

void LlmClient::geminiRound(int round)
{
    const QUrl url(QString("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent")
                       .arg(geminiModel_));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("x-goog-api-key", geminiKey_.toUtf8());

    QJsonObject body{
        {"system_instruction",
             QJsonObject{{"parts", QJsonArray{QJsonObject{{"text", systemPrompt()}}}}}},
        {"contents", geminiHistory_},
        {"tools", QJsonArray{QJsonObject{{"function_declarations",
                                          geminiFunctionDeclarations()}}}},
        {"generationConfig", QJsonObject{{"maxOutputTokens", 1024}}}
    };

    QNetworkReply *reply = nam_.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    reply->setProperty("round", round);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int round = reply->property("round").toInt();

        const QByteArray data = reply->readAll();
        QJsonObject json = QJsonDocument::fromJson(data).object();

        if (json.contains("error")) {
            QJsonObject err = json.value("error").toObject();
            QString msg = err.value("message").toString("unknown");
            if (err.value("code").toInt() == 429)
                msg = "Ucretsiz kota doldu, biraz bekleyip tekrar dene.";
            finish(false, "Gemini API: " + msg);
            return;
        }
        if (reply->error() != QNetworkReply::NoError && json.isEmpty()) {
            finish(false, "Ag hatasi: " + reply->errorString());
            return;
        }

        QJsonArray candidates = json.value("candidates").toArray();
        QJsonArray parts = candidates.isEmpty()
            ? QJsonArray()
            : candidates.first().toObject().value("content").toObject()
                  .value("parts").toArray();
        if (parts.isEmpty()) {
            finish(false, "Gemini bos cevap dondurdu.");
            return;
        }

        geminiHistory_.append(QJsonObject{{"role", "model"}, {"parts", parts}});

        QStringList textParts;
        struct Call { QString name; QJsonObject args; };
        QList<Call> calls;
        for (const auto &p : parts) {
            QJsonObject part = p.toObject();
            if (part.contains("text"))
                textParts << part.value("text").toString();
            if (part.contains("functionCall")) {
                QJsonObject fc = part.value("functionCall").toObject();
                calls.append({fc.value("name").toString(),
                              fc.value("args").toObject()});
            }
        }

        if (!calls.isEmpty() && round < kMaxToolRounds && executor_) {
            QJsonArray responseParts;
            for (const auto &c : calls) {
                const QString r = executor_(c.name, c.args);
                responseParts.append(QJsonObject{
                    {"functionResponse",
                     QJsonObject{{"name", c.name},
                                 {"response", QJsonObject{{"result", r}}}}}});
            }
            geminiHistory_.append(QJsonObject{{"role", "user"},
                                              {"parts", responseParts}});
            geminiRound(round + 1);
            return;
        }

        const QString final = textParts.join("\n").trimmed();
        finish(true, final.isEmpty() ? "(cevap yok)" : final);
    });
}

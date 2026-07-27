#ifndef BALANCEROBOT_H
#define BALANCEROBOT_H

#include "robotcontrol.h"
#include <gattserver.h>
#include <QTimer>
#include <QThread>

class VoiceAssistant;

class BalanceRobot : public QObject
{
    Q_OBJECT

public:
    explicit BalanceRobot(QObject *parent = nullptr);
    ~BalanceRobot();
    void init();
    static BalanceRobot* getInstance();
    static BalanceRobot *theInstance_;

private slots:
    void onConnectionStatedChanged(bool state);
    void onDataReceived(QByteArray data);
    void onTelemetryTick();

private:
    void createMessage(uint8_t msgId, uint8_t rw, QByteArray payload, QByteArray *result);
    bool parseMessage(QByteArray *data, uint8_t &command, QByteArray &value, uint8_t &rw);
    void requestData(uint8_t command);
    void sendData(uint8_t command, uint8_t value);
    void sendInt16(uint8_t command, int16_t value);
    void sendString(uint8_t command, QString value);
    void sendBytes(uint8_t command, const QByteArray &payload);

    RobotControl *robotControl{};
    GattServer   *gattServer{};
    VoiceAssistant *voiceAssistant{};
    QThread         assistantThread;

    Message      message;
    QStringList  keyList{};
    QString      device, ip, mac, mask;
    bool         mIpSend{false};

    QTimer       telemetryTimer;
    bool         clientConnected{false};
};

#endif // BALANCEROBOT_H

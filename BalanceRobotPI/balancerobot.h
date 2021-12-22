#ifndef BALANCEROBOT_H
#define BALANCEROBOT_H

#include "constants.h"

#include "robotcontrol.h"
#include <gattserver.h>
#include "alsatranslator.h"
#include "networkrequest.h"
#include "speaker.h"

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
    void onCommandReceived(QString command);

private:

    NetworkRequest *networkRequest{};    

    void createMessage(uint8_t msgId, uint8_t rw, QByteArray payload, QByteArray *result);
    bool parseMessage(QByteArray *data, uint8_t &command, QByteArray &value, uint8_t &rw);
    void requestData(uint8_t command);
    void sendData(uint8_t command, uint8_t value);
    void sendString(uint8_t command, QString value);    

    RobotControl *robotControl{};
    AlsaTranslator *translator{};
    GattServer *gattServer{};  
    Speaker *speaker{};

    Message message;

    std::string currentSound{};

    QString soundText{};
    QStringList keyList{};    

private slots:
    void recievedResponse(QString);

};

#endif // BALANCEROBOT_H

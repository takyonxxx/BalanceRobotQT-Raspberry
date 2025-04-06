#include "balancerobot.h"
#include "constants.h"

BalanceRobot *BalanceRobot::theInstance_= nullptr;

BalanceRobot* BalanceRobot::getInstance()
{
    if (theInstance_ == nullptr)
    {
        theInstance_ = new BalanceRobot();
    }
    return theInstance_;
}

BalanceRobot::BalanceRobot(QObject *parent) : QObject(parent)
{   
    init();
}

BalanceRobot::~BalanceRobot()
{
    robotControl->stop();
    delete robotControl;
    delete gattServer;
}

//slots

void BalanceRobot::onConnectionStatedChanged(bool state)
{
}

void BalanceRobot::createMessage(uint8_t msgId, uint8_t rw, QByteArray payload, QByteArray *result)
{
    // Safety check
    if (!result) {
        qDebug() << "Error: Result pointer is null in createMessage";
        return;
    }

    // Clear the result array first to prevent appending to existing data
    result->clear();

    // Make sure payload size doesn't exceed buffer
    if (payload.size() > MaxPayload) {
        qDebug() << "Warning: Payload size exceeds maximum, truncating";
        payload = payload.left(MaxPayload);
    }

    // Create buffer with zero initialization
    uint8_t buffer[MaxPayload + 8] = {0};
    uint8_t command = msgId;

    // Create the packet
    int len = message.create_pack(rw, command, payload, buffer);

    // Safety check on length
    if (len <= 0 || len > (MaxPayload + 8)) {
        qDebug() << "Error: Invalid message length in createMessage:" << len;
        return;
    }

    // Copy buffer to result QByteArray
    for (int i = 0; i < len; i++) {
        result->append(static_cast<char>(buffer[i]));
    }
}

bool BalanceRobot::parseMessage(QByteArray *data, uint8_t &command, QByteArray &value, uint8_t &rw)
{
    // Safety checks
    if (!data || data->isEmpty()) {
        qDebug() << "Error: Invalid data in parseMessage";
        return false;
    }

    // Clear the output value array
    value.clear();

    // Check minimum packet size
    if (data->size() < 4) {
        qDebug() << "Error: Packet too small:" << data->size();
        return false;
    }

    // Log raw data for debugging
    QString hexDump;
    for (int i = 0; i < data->size(); i++) {
        hexDump += QString("%1 ").arg((unsigned char)data->at(i), 2, 16, QChar('0'));
    }

    // Initialize the message struct
    MessagePack parsedMessage = {0};

    // Parse the message
    uint8_t* dataToParse = reinterpret_cast<uint8_t*>(data->data());

    if (message.parse(dataToParse, static_cast<uint8_t>(data->size()), &parsedMessage)) {
        command = parsedMessage.command;
        rw = parsedMessage.rw;

        // Safety check on parsed length
        if (parsedMessage.len > MaxPayload) {
            qDebug() << "Error: Invalid parsed length:" << parsedMessage.len;
            return false;
        }

        // Copy data safely
        for (int i = 0; i < parsedMessage.len; i++) {
            value.append(static_cast<char>(parsedMessage.data[i]));
        }

        return true;
    }

    qDebug() << "Failed to parse message";
    return false;
}

void BalanceRobot::onDataReceived(QByteArray data)
{
    // Convert the raw data to a debug-friendly hex string
    QString hexDump;
    for (int i = 0; i < data.size(); i++) {
        hexDump += QString("%1 ").arg((unsigned char)data.at(i), 2, 16, QChar('0'));
    }

    try {
        // Handle the specific pattern that was causing crashes
        if (data.size() == 4 &&
            (unsigned char)data.at(0) == mHeader &&
            (unsigned char)data.at(1) == 0x00 &&
            (unsigned char)data.at(2) == mRead) {

            uint8_t cmd = (unsigned char)data.at(3);

            // Safer approach: Use a separate try/catch for this critical section
            try {
                // Process the read request directly based on the command
                if (robotControl != nullptr) {  // Null pointer check
                    switch (cmd) {
                    case mPP:
                        sendData(mPP, (int)robotControl->getAggKp());
                        break;
                    case mPI:
                        sendData(mPI, (int)(10*robotControl->getAggKi()));
                        break;
                    case mPD:
                        sendData(mPD, (int)(10*robotControl->getAggKd()));
                        break;
                    case mAC:
                        sendData(mAC, (int)(10*robotControl->getAggAC()));
                        break;
                    case mSD:
                        sendData(mSD, (int)(10*robotControl->getAggSD()));
                        break;
                    default:
                        qDebug() << "Unknown command code in direct read request: 0x" << QString::number(cmd, 16);
                        break;
                    }
                } else {
                    qDebug() << "WARNING: robotControl is null when processing direct read";
                }
            } catch (const std::exception &e) {
                qDebug() << "Exception when processing direct read request:" << e.what();
            } catch (...) {
                qDebug() << "Unknown exception when processing direct read request";
            }
            return; // Return after handling the special case, whether successful or not
        }

        // Normal message parsing for other message formats
        uint8_t parsedCommand = 0;
        uint8_t rw = 0;
        QByteArray parsedValue;

        if (!parseMessage(&data, parsedCommand, parsedValue, rw)) {
            qDebug() << "Failed to parse message, skipping processing";
            return;
        }

        bool ok = true;
        int value = 0;

        // Only attempt to convert to int if we have data and need an integer value
        if (!parsedValue.isEmpty() &&
            (parsedCommand == mPP || parsedCommand == mPI || parsedCommand == mPD ||
             parsedCommand == mAC || parsedCommand == mSD || parsedCommand == mForward ||
             parsedCommand == mBackward || parsedCommand == mLeft || parsedCommand == mRight)) {

            // Convert to hex string then to int
            value = parsedValue.toHex().toInt(&ok, 16);

            if (!ok) {
                qDebug() << "Failed to convert value to integer:" << parsedValue.toHex();
                return;
            }
        }

        // Process based on read/write flag
        if (rw == mRead) {
            switch (parsedCommand) {
            case mPP:
                sendData(mPP, (int)robotControl->getAggKp());
                break;
            case mPI:
                sendData(mPI, (int)(10*robotControl->getAggKi()));
                break;
            case mPD:
                sendData(mPD, (int)(10*robotControl->getAggKd()));
                break;
            case mAC:
                sendData(mAC, (int)(10*robotControl->getAggAC()));
                break;
            case mSD:
                sendData(mSD, (int)(10*robotControl->getAggSD()));
                break;
            default:
                qDebug() << "Unknown command in read operation:" << parsedCommand;
                break;
            }
        } else if (rw == mWrite) {
            switch (parsedCommand) {
            case mPP:
                robotControl->setAggKp(value);
                break;
            case mPI:
                robotControl->setAggKi(static_cast<float>(value / 10.0));
                break;
            case mPD:
                robotControl->setAggKd(static_cast<float>(value / 10.0));
                break;
            case mAC:
                robotControl->setAggAC(static_cast<float>(value / 10.0));
                break;
            case mSD:
                robotControl->setAggSD(static_cast<float>(value / 10.0));
                break;
            case mSpeak:
            {
                auto soundText = QString(parsedValue.data());
                qDebug() << "Speak command received:" << soundText;
                // Add your speak command processing here
            }
            break;
            case mForward:
                robotControl->setNeedSpeed(-1*value);
                break;
            case mBackward:
                robotControl->setNeedSpeed(value);
                break;
            case mLeft:
                robotControl->setNeedTurnL(value);
                break;
            case mRight:
                robotControl->setNeedTurnR(value);
                break;
            default:
                qDebug() << "Unknown command in write operation:" << parsedCommand;
                break;
            }

            robotControl->saveSettings();
        }

        // Send status info back
        auto pidInfo = QString("P:")
                       + QString::number(robotControl->getAggKp(), 'f', 1)
                       + QString(" ")
                       + QString("I:")
                       + QString::number(robotControl->getAggKi(), 'f', 1)
                       + QString(" ")
                       + QString("D:")
                       + QString::number(robotControl->getAggKd(), 'f', 1)
                       + QString(" ")
                       + QString("SD:")
                       + QString::number(robotControl->getAggSD(), 'f', 1)
                       + QString(" ")
                       + QString("AC:")
                       + QString::number(robotControl->getAggAC(), 'f', 1)
                       + QString(" ")
                       + QString("Speed:")
                       + QString::number(robotControl->getNeedSpeed())
                       + QString(" ")
                       + QString("TurnL:")
                       + QString::number(robotControl->getNeedTurnL())
                       + QString(" ")
                       + QString("TurnR:")
                       + QString::number(robotControl->getNeedTurnR())
                       + QString(" ");

        qDebug() << pidInfo;
        sendString(mData, pidInfo);
    } catch (const std::exception &e) {
        qDebug() << "Exception in onDataReceived:" << e.what();
    } catch (...) {
        qDebug() << "Unknown exception in onDataReceived";
    }
}

void BalanceRobot::requestData(uint8_t command)
{
    QByteArray payload;
    QByteArray sendData;
    createMessage(command, mRead, payload, &sendData);
    gattServer->writeValue(sendData);
}

void BalanceRobot::sendData(uint8_t command, uint8_t value)
{
    // Create a properly sized QByteArray first
    QByteArray payload(1, 0);  // Initialize with size 1, value 0

    // Now set the value
    payload[0] = value;

    QByteArray sendData;
    createMessage(command, mWrite, payload, &sendData);
    gattServer->writeValue(sendData);
}

void BalanceRobot::sendString(uint8_t command, QString value)
{
    QByteArray sendData;
    QByteArray bytedata;
    bytedata = value.toLocal8Bit();
    createMessage(command, mWrite, bytedata, &sendData);
    gattServer->writeValue(sendData);
}

void BalanceRobot::init()
{
    QString device, ip, mac, mask;
    int conn_try = 0;
    while(ip.size() == 0)
    {
        if(conn_try > 5)
            break;
        getDeviceInfo(device, ip, mac, mask);
        conn_try++;
        QThread::msleep(250);
    }

    qDebug() << ip << mac;

    // speaker = Speaker::getInstance();

    gattServer = GattServer::getInstance();
    if (gattServer)
    {
        qDebug() << "Starting gatt service";
        QObject::connect(gattServer, &GattServer::connectionState, this, &BalanceRobot::onConnectionStatedChanged);
        QObject::connect(gattServer, &GattServer::dataReceived, this, &BalanceRobot::onDataReceived);
        gattServer->startBleService();
    }

    robotControl = RobotControl::getInstance();
    robotControl->start();
}

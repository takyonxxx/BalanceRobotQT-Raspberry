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
    translator->stop();
    delete robotControl;
    delete speaker;
    delete gattServer;
    delete translator;    
}

//slots

void BalanceRobot::onConnectionStatedChanged(bool state)
{
    speaker->setLanguageCode(EN);
    if(state)
    {
        soundText = ("Bluetooth connection is succesfull.");
        speaker->speak(soundText);
    }
    else
    {
        soundText = ("Bluetooth connection lost.");
        speaker->speak(soundText);
    }
    speaker->setLanguageCode(TR);
}

void BalanceRobot::createMessage(uint8_t msgId, uint8_t rw, QByteArray payload, QByteArray *result)
{
    uint8_t buffer[MaxPayload+8] = {'\0'};
    uint8_t command = msgId;

    int len = message.create_pack(rw , command , payload, buffer);

    for (int i = 0; i < len; i++)
    {
        result->append(buffer[i]);
    }
}

bool BalanceRobot::parseMessage(QByteArray *data, uint8_t &command, QByteArray &value,  uint8_t &rw)
{
    MessagePack parsedMessage;

    uint8_t* dataToParse = reinterpret_cast<uint8_t*>(data->data());
    QByteArray returnValue;

    if(message.parse(dataToParse, (uint8_t)data->length(), &parsedMessage))
    {
        command = parsedMessage.command;
        rw = parsedMessage.rw;

        for(int i = 0; i< parsedMessage.len; i++)
        {
            value.append(parsedMessage.data[i]);
        }

        return true;
    }

    return false;
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
    QByteArray payload;
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



void BalanceRobot::onCommandReceived(QString command)
{    
    if(networkRequest && !command.isEmpty())
    {
        qDebug() << "Voice received: " << command;
        if(command.contains("dur"))
        {
            translator->stop();
            auto soundText = QString("Çevirici durduruldu.");
            speaker->speak(soundText);
            return;
        }
        networkRequest->sendRequest(command);
    }
}

void BalanceRobot::onDataReceived(QByteArray data)
{
    uint8_t parsedCommand;
    uint8_t rw;
    QByteArray parsedValue;
    auto parsed = parseMessage(&data, parsedCommand, parsedValue, rw);

    if(!parsed)return;

    bool ok;
    int value =  parsedValue.toHex().toInt(&ok, 16);

    if(rw == mRead)
    {
        switch (parsedCommand)
        {
        case mPP: //Proportional
        {
            sendData(mPP, (int)robotControl->getAggKp());
            break;
        }
        case mPI: //Integral control
        {
            sendData(mPI, (int)10*robotControl->getAggKi());
            break;
        }
        case mPD: //Derivative constant
        {
            sendData(mPD, (int)10*robotControl->getAggKd());
            break;
        }
        case mAC://angle correction
        {
            sendData(mAC, (int)10*robotControl->getAggAC());
            break;
        }
        case mSD://speed diff constant wheel
        {
            sendData(mSD, (int)10*robotControl->getAggSD());
            break;
        }

        default:
            break;
        }
    }
    else if(rw == mWrite)
    {
        switch (parsedCommand)
        {
        case mPP:
        {
            robotControl->setAggKp(value);
            break;
        }
        case mPI:
        {
            robotControl->setAggKi(static_cast<float>(value / 10.0));
            break;
        }
        case mPD:
        {
            robotControl->setAggKd(static_cast<float>(value / 10.0));
            break;
        }
        case mAC:
        {
            robotControl->setAggAC(static_cast<float>(value / 10.0));
            break;
        }
        case mSD:
        {
            robotControl->setAggSD(static_cast<float>(value / 10.0));
            break;
        }
        case mSpeak:
        {
            soundText = QString(parsedValue.data());

            if (soundText.contains("start"))
                translator->start();
            else if  (soundText.contains("stop"))
                translator->stop();

            auto thread = QThread::create([this]{
                speaker->speak(soundText);
            });
            connect(thread,  &QThread::finished,  this,  [=]()
            {
            });
            thread->start();
            break;
        }
        case mForward:
        {
            robotControl->setNeedSpeed(-1*value);
            break;
        }
        case mBackward:
        {
            robotControl->setNeedSpeed(value);
            break;
        }
        case mLeft:
        {
            robotControl->setNeedTurnL(value);
            break;
        }
        case mRight:
        {
            robotControl->setNeedTurnR(value);
            break;
        }
        default:
            break;
        }

        robotControl->saveSettings();
    }

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
}

void BalanceRobot::recievedResponse(QString result)
{
    if (!result.isEmpty())
    {
        if (result.contains("no result"))
        {
             qDebug() << "No response.";
             translator->record();
             return;
        }

        soundText = result;

        qDebug() << "Response received: " << soundText;

        auto thread = QThread::create([this]{
            speaker->speak(soundText);
        });
        connect(thread,  &QThread::finished,  this,  [=]()
        {
            translator->record();
        });
        thread->start();
    }
}

void BalanceRobot::init()
{      
    speaker = Speaker::getInstance();
    speaker->setLanguageCode(TR);

//    execCommand((char*)"aplay r2d2.wav");

    auto soundText = QString("Robot başlıyor.");
    speaker->speak(soundText);

    qDebug() << "Speaker started.";

    QString device, ip, mac, mask;
    int conn_try = 0;
    while(ip.size() == 0)
    {
        if(conn_try > 5)
            break;
        getDeviceInfo(device, ip, mac, mask);
        auto soundText = QString("İnternet bağlantısı kontrol ediliyor.");
        speaker->speak(soundText);
        conn_try++;
        QThread::msleep(250);
    }

    qDebug() << "Ip check ok.";

    gattServer = GattServer::getInstance();
    if (gattServer)
    {
        qDebug() << "Starting gatt service";
        QObject::connect(gattServer, &GattServer::connectionState, this, &BalanceRobot::onConnectionStatedChanged);
        QObject::connect(gattServer, &GattServer::dataReceived, this, &BalanceRobot::onDataReceived);
        gattServer->startBleService();
    }

    translator = new AlsaTranslator(this);
    translator->setRecordDuration(2000);
    translator->setLanguageCode(TR);
    QObject::connect(translator, &AlsaTranslator::commandChanged, this, &BalanceRobot::onCommandReceived);

    networkRequest = NetworkRequest::getInstance();
    connect(networkRequest, &NetworkRequest::sendResponse, this, &BalanceRobot::recievedResponse);

    if (ip.size() > 0)
    {
        qDebug() << "ip: " <<  ip << " mac:" << mac;
        soundText = QString("Aypi adresi " + ip.replace(".", ", ") + ".");
        speaker->speak(soundText);
    }
    else
    {
        auto soundText = QString("İnternet bağlantısı kurulamadı.");
        speaker->speak(soundText);
    }

    robotControl = RobotControl::getInstance();
    robotControl->start();

    // translator->start();
}

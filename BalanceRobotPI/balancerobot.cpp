#include "balancerobot.h"
#include "constants.h"

BalanceRobot *BalanceRobot::theInstance_ = nullptr;

BalanceRobot* BalanceRobot::getInstance()
{
    if (theInstance_ == nullptr)
        theInstance_ = new BalanceRobot();
    return theInstance_;
}

BalanceRobot::BalanceRobot(QObject *parent) : QObject(parent)
{
    init();
}

BalanceRobot::~BalanceRobot()
{
    if (robotControl) {
        robotControl->stop();
        delete robotControl;
    }
    if (gattServer) delete gattServer;
}

void BalanceRobot::onConnectionStatedChanged(bool state)
{
    clientConnected = state;
    if (state) {
        // Bağlandığında telemetri timer'ını başlat
        if (!telemetryTimer.isActive()) telemetryTimer.start(100); // 10 Hz BLE
        mIpSend = false; // Bir sonraki read komutunda IP yine gönderilsin
    } else {
        telemetryTimer.stop();
    }
}

// ---------------- Message helpers ----------------

void BalanceRobot::createMessage(uint8_t msgId, uint8_t rw, QByteArray payload, QByteArray *result)
{
    if (!result) return;
    result->clear();
    if (payload.size() > MaxPayload) payload = payload.left(MaxPayload);

    uint8_t buffer[MaxPayload + 8] = {0};
    int len = message.create_pack(rw, msgId, payload, buffer);
    if (len <= 0 || len > (int)(MaxPayload + 8)) return;

    for (int i = 0; i < len; i++)
        result->append(static_cast<char>(buffer[i]));
}

bool BalanceRobot::parseMessage(QByteArray *data, uint8_t &command, QByteArray &value, uint8_t &rw)
{
    if (!data || data->isEmpty() || data->size() < 4) return false;
    value.clear();

    MessagePack parsed = {0};
    if (!message.parse(reinterpret_cast<uint8_t*>(data->data()),
                       static_cast<uint8_t>(data->size()), &parsed))
        return false;

    command = parsed.command;
    rw      = parsed.rw;
    if (parsed.len > MaxPayload) return false;
    for (int i = 0; i < parsed.len; i++)
        value.append(static_cast<char>(parsed.data[i]));
    return true;
}

void BalanceRobot::sendData(uint8_t command, uint8_t value)
{
    QByteArray payload(1, 0);
    payload[0] = value;
    QByteArray out;
    createMessage(command, mWrite, payload, &out);
    if (gattServer) gattServer->writeValue(out);
}

void BalanceRobot::sendInt16(uint8_t command, int16_t value)
{
    QByteArray payload;
    payload.append((char)((value >> 8) & 0xFF));
    payload.append((char)(value & 0xFF));
    QByteArray out;
    createMessage(command, mWrite, payload, &out);
    if (gattServer) gattServer->writeValue(out);
}

void BalanceRobot::sendString(uint8_t command, QString value)
{
    QByteArray out;
    QByteArray bytedata = value.toLocal8Bit();
    createMessage(command, mWrite, bytedata, &out);
    if (gattServer) gattServer->writeValue(out);
}

void BalanceRobot::sendBytes(uint8_t command, const QByteArray &payload)
{
    QByteArray out;
    createMessage(command, mWrite, payload, &out);
    if (gattServer) gattServer->writeValue(out);
}

// ---------------- Telemetry tick ----------------

void BalanceRobot::onTelemetryTick()
{
    if (!robotControl || !gattServer) return;

    auto t = robotControl->getTelemetry();

    // Telemetri paketi düzeni (toplam 14 byte):
    //  [0..1] angle  (int16, 0.01° çözünürlük)
    //  [2..3] gyroRate (int16, 0.1°/s)
    //  [4..5] target (int16, 0.01°)
    //  [6..7] trim   (int16, 0.01°)
    //  [8..9] pwmL   (int16)
    // [10..11] pwmR  (int16)
    //   [12]  flags: bit0=armed, bit1=fallen, bit2=autoMode, bit3=posHold
    //   [13]  reserved

    auto pack16 = [](int v) -> uint16_t {
        if (v > 32767)  v = 32767;
        if (v < -32768) v = -32768;
        return (uint16_t)((int16_t)v);
    };

    int16_t angle100  = (int16_t)std::round(t.angle * 100.0f);
    int16_t gyro10    = (int16_t)std::round(t.gyroRate * 10.0f);
    int16_t target100 = (int16_t)std::round(t.targetAngle * 100.0f);
    int16_t trim100   = (int16_t)std::round(t.trim * 100.0f);

    uint8_t flags = 0;
    if (t.armed)        flags |= 0x01;
    if (t.fallen)       flags |= 0x02;
    if (t.autoMode)     flags |= 0x04;
    if (t.positionHold) flags |= 0x08;

    QByteArray payload;
    auto appendBE = [&](int16_t v) {
        payload.append((char)((v >> 8) & 0xFF));
        payload.append((char)(v & 0xFF));
    };
    appendBE(angle100);
    appendBE(gyro10);
    appendBE(target100);
    appendBE(trim100);
    appendBE((int16_t)pack16(t.pwmL));
    appendBE((int16_t)pack16(t.pwmR));
    payload.append((char)flags);
    payload.append((char)0); // reserved

    sendBytes(mTelemetry, payload);
}

// ---------------- BLE data handler ----------------

void BalanceRobot::onDataReceived(QByteArray data)
{
    uint8_t parsedCommand = 0;
    uint8_t rw = 0;
    QByteArray parsedValue;

    if (!parseMessage(&data, parsedCommand, parsedValue, rw)) {
        qDebug() << "Failed to parse incoming msg";
        return;
    }

    // UInt8 değerine ihtiyaç duyan komutlar
    bool needsByte = (parsedCommand == mPP || parsedCommand == mPI ||
                      parsedCommand == mPD || parsedCommand == mAC ||
                      parsedCommand == mSD || parsedCommand == mForward ||
                      parsedCommand == mBackward || parsedCommand == mLeft ||
                      parsedCommand == mRight || parsedCommand == mAutoMode ||
                      parsedCommand == mPositionHold ||
                      parsedCommand == mSpdKp || parsedCommand == mSpdKi ||
                      parsedCommand == mSpdMaxTilt || parsedCommand == mSpdMaxVel);

    int value = 0;
    if (needsByte && !parsedValue.isEmpty()) {
        value = (uint8_t)parsedValue[0];
    }

    if (rw == mRead) {
        // IP'yi tek seferlik gönder
        if (!mIpSend) {
            getDeviceInfo(device, ip, mac, mask);
            sendString(mData, ip);
            mIpSend = true;
        }

        switch (parsedCommand) {
        case mPP:    sendData(mPP, (uint8_t)std::clamp<int>((int)robotControl->getAggKp(), 0, 255)); break;
        case mPI:    sendData(mPI, (uint8_t)std::clamp<int>((int)(robotControl->getAggKi()), 0, 255)); break;
        case mPD:    sendData(mPD, (uint8_t)std::clamp<int>((int)(100*robotControl->getAggKd()), 0, 255)); break;
        case mAC:    sendData(mAC, (uint8_t)std::clamp<int>((int)(10*robotControl->getAggAC()), 0, 255)); break;
        case mSD:    sendData(mSD, (uint8_t)std::clamp<int>((int)(10*robotControl->getAggSD()), 0, 255)); break;
        case mSpdKp:      sendData(mSpdKp,      (uint8_t)std::clamp<int>((int)(100*robotControl->getSpdKp()), 0, 255)); break;
        case mSpdKi:      sendData(mSpdKi,      (uint8_t)std::clamp<int>((int)(100*robotControl->getSpdKi()), 0, 255)); break;
        case mSpdMaxTilt: sendData(mSpdMaxTilt, (uint8_t)std::clamp<int>((int)robotControl->getSpdMaxTilt(), 0, 255)); break;
        case mSpdMaxVel:  sendData(mSpdMaxVel,  (uint8_t)std::clamp<int>((int)(10*robotControl->getSpdMaxVel()), 0, 255)); break;
        case mArmed: sendData(mArmed, robotControl->getIsArmed() ? 1 : 0); break;
        case mAutoMode: sendData(mAutoMode, robotControl->getAutoMode() ? 1 : 0); break;
        case mPositionHold: sendData(mPositionHold, robotControl->getPositionHold() ? 1 : 0); break;
        default: qDebug() << "Unknown read cmd:" << parsedCommand; break;
        }
        return;
    }

    if (rw == mWrite) {
        switch (parsedCommand) {
        case mPP: robotControl->setAggKp((float)value); qDebug() << "Kp =" << robotControl->getAggKp(); break;
        case mPI: robotControl->setAggKi((float)value); qDebug() << "Ki =" << robotControl->getAggKi(); break;
        case mPD: robotControl->setAggKd(value / 100.0f); qDebug() << "Kd =" << robotControl->getAggKd(); break;
        case mAC: robotControl->setAggAC(value / 10.0f); qDebug() << "AC =" << robotControl->getAggAC(); break;
        case mSD: robotControl->setAggSD(value / 10.0f); qDebug() << "SD =" << robotControl->getAggSD(); break;
        case mSpdKp:      robotControl->setSpdKp(value / 100.0f); qDebug() << "Speed Kp =" << robotControl->getSpdKp(); break;
        case mSpdKi:      robotControl->setSpdKi(value / 100.0f); qDebug() << "Speed Ki =" << robotControl->getSpdKi(); break;
        case mSpdMaxTilt: robotControl->setSpdMaxTilt((float)value); qDebug() << "Speed Max Tilt =" << robotControl->getSpdMaxTilt(); break;
        case mSpdMaxVel:  robotControl->setSpdMaxVel(value / 10.0f); qDebug() << "Speed Max Vel =" << robotControl->getSpdMaxVel(); break;
        case mForward:  robotControl->setNeedSpeed(value); break;
        case mBackward: robotControl->setNeedSpeed(-1 * value); break;
        case mLeft:     robotControl->setNeedTurnL(value); robotControl->setNeedTurnR(0); break;
        case mRight:    robotControl->setNeedTurnR(value); robotControl->setNeedTurnL(0); break;
        case mArmed:    robotControl->setIsArmed(true);  break;
        case mDisArmed: robotControl->setIsArmed(false); break;
        case mAutoMode: robotControl->setAutoMode(value != 0); break;
        case mPositionHold: robotControl->setPositionHold(value != 0); break;
        case mResetTrim: robotControl->resetTrim(); break;
        case mTrimFine: {
            // signed 16-bit (BE): 0.01°
            if (parsedValue.size() >= 2) {
                int16_t raw = ((uint8_t)parsedValue[0] << 8) | (uint8_t)parsedValue[1];
                robotControl->setTrimFine(raw / 100.0f);
            }
            break;
        }
        case mSpeak: {
            QString s = QString::fromUtf8(parsedValue.data(), parsedValue.size());
            if (speaker) speaker->speak(s);
            break;
        }
        default: qDebug() << "Unknown write cmd:" << parsedCommand; break;
        }
        // Komut sonrası ayar değişikliği telemetri timer içinde saklanıyor;
        // burada saveSettings cağırmıyoruz (her yazma SD aşındırırdı).
    }
}

void BalanceRobot::requestData(uint8_t command)
{
    QByteArray payload;
    QByteArray out;
    createMessage(command, mRead, payload, &out);
    if (gattServer) gattServer->writeValue(out);
}

// ---------------- Init ----------------

void BalanceRobot::init()
{
    int conn_try = 0;
    while (ip.size() == 0 && conn_try < 6) {
        getDeviceInfo(device, ip, mac, mask);
        conn_try++;
        QThread::msleep(250);
    }
    qDebug() << "Local IP:" << ip << "MAC:" << mac;

    speaker = Speaker::getInstance();
    if (speaker) speaker->setLanguageCode(SType::TR);

    gattServer = GattServer::getInstance();
    if (gattServer) {
        QObject::connect(gattServer, &GattServer::connectionState,
                         this, &BalanceRobot::onConnectionStatedChanged);
        QObject::connect(gattServer, &GattServer::dataReceived,
                         this, &BalanceRobot::onDataReceived);
        gattServer->startBleService();
        qDebug("BLE service started");
    }

    robotControl = RobotControl::getInstance();
    if (robotControl) {
        // Otomatik mod açık - auto-arm/auto-recover etkin.
        // Kullanıcı isterse iOS'ten kapatabilir.
        robotControl->setAutoMode(true);
        robotControl->setPositionHold(true);
        robotControl->start();
    }

    QObject::connect(&telemetryTimer, &QTimer::timeout,
                     this, &BalanceRobot::onTelemetryTick);
    // İstemci bağlanmadığı için timer henüz başlatılmıyor;
    // connection state değiştiğinde başlatılır.
}

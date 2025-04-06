#include "gattserver.h"

GattServer *GattServer::theInstance_= nullptr;

GattServer* GattServer::getInstance()
{
    if (theInstance_ == nullptr)
    {
        theInstance_ = new GattServer();
    }
    return theInstance_;
}

GattServer::GattServer(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<QLowEnergyController::ControllerState>();
    qRegisterMetaType<QLowEnergyController::Error>();
    qRegisterMetaType<QLowEnergyConnectionParameters>();   
}

GattServer::~GattServer()
{
}

void GattServer::onInfoReceived(QString info)
{
    emit sendInfo(info);
}

void GattServer::onSensorReceived(QString sensor_value)
{
    QByteArray textData = sensor_value.toUtf8();
    writeValue(textData);
}

void GattServer::handleConnected()
{
    try {
        // Bağlantı kurulduktan sonra veri işleme
        remoteDeviceUuid = leController.data()->remoteDeviceUuid();
        m_ConnectionState = true;

        auto statusText = QString("Connected to device %1").arg(remoteDeviceUuid.toString());
        emit sendInfo(statusText);
        qDebug() << statusText;

        // Use the custom service UUID defined in the header
        QBluetoothUuid customServiceUuid = QBluetoothUuid(QString(SCANPARAMETERSUUID));
        const ServicePtr service = services.value(customServiceUuid);

        if (!service) {
            qDebug() << "Warning: Service not found in handleConnected(), looking for:" << customServiceUuid.toString();
            qDebug() << "Available services:";
            for (const auto& uuid : services.keys()) {
                qDebug() << "Service UUID:" << uuid.toString();
            }
            return;
        }

        // Servis başlatma onayı mesajı gönder
        QByteArray welcomeMsg = "Balance Robot ready!";
        writeValue(welcomeMsg);
        emit connectionState(m_ConnectionState);
    }
    catch (const std::exception& e) {
        qDebug() << "Exception in handleConnected: " << e.what();
    }
    catch (...) {
        qDebug() << "Unknown exception in handleConnected";
    }
}

void GattServer::handleDisconnected()
{
    m_ConnectionState = false;
    emit connectionState(m_ConnectionState);

    while (leController->state() != QLowEnergyController::UnconnectedState) {
        leController->disconnectFromDevice();
    }

    if(leController->state() == QLowEnergyController::UnconnectedState)
    {
        auto statusText = QString("Disconnected from %1").arg(remoteDeviceUuid.toString());
        emit sendInfo(statusText);
        reConnect();
    }
}

void GattServer::errorOccurred(QLowEnergyController::Error newError)
{
    auto statusText = QString("Controller Error: %1").arg(newError);
    emit sendInfo(statusText);
}

void GattServer::addService(const QLowEnergyServiceData &serviceData)
{
    const ServicePtr service(leController->addService(serviceData));
    Q_ASSERT(service);

    services.insert(service->serviceUuid(), service);
}

void GattServer::startBleService()
{
    leController.reset(QLowEnergyController::createPeripheral());

    // UUID değerlerini doğrudan kullan - bu değerler header dosyasında #define olarak tanımlanmış
    QBluetoothUuid customServiceUuid = QBluetoothUuid(QString(SCANPARAMETERSUUID));

    serviceData.setType(QLowEnergyServiceData::ServiceTypePrimary);
    serviceData.setUuid(customServiceUuid);

    // Önceden tanımlanmış RX/TX UUID'lerini kullan
    QBluetoothUuid rxUuid = QBluetoothUuid(QString(RXUUID));
    QBluetoothUuid txUuid = QBluetoothUuid(QString(TXUUID));

    QLowEnergyCharacteristicData charRxData;
    charRxData.setUuid(rxUuid);
    charRxData.setProperties(QLowEnergyCharacteristic::Read | QLowEnergyCharacteristic::Notify);
    charRxData.setValue(QByteArray(2, 0));
    const QLowEnergyDescriptorData rxClientConfig(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration, QByteArray(2, 0));
    charRxData.addDescriptor(rxClientConfig);
    serviceData.addCharacteristic(charRxData);

    QLowEnergyCharacteristicData charTxData;
    charTxData.setUuid(txUuid);
    charTxData.setValue(QByteArray(2, 0));
    charTxData.setProperties(QLowEnergyCharacteristic::Write);
    const QLowEnergyDescriptorData txClientConfig(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration, QByteArray(2, 0));
    charTxData.addDescriptor(txClientConfig);
    serviceData.addCharacteristic(charTxData);

    addService(serviceData);

    // Servis değişkeni - customServiceUuid kullanıyoruz
    const ServicePtr service = services.value(customServiceUuid);
    if (!service) {
        qDebug() << "Error: Service could not be created with UUID:" << customServiceUuid.toString();
        return;
    }

    // Bağlantıları ayarla
    QObject::connect(leController.data(), &QLowEnergyController::connected, this, &GattServer::handleConnected);
    QObject::connect(leController.data(), &QLowEnergyController::disconnected, this, &GattServer::handleDisconnected);
    QObject::connect(leController.data(), QOverload<QLowEnergyController::Error>::of(&QLowEnergyController::errorOccurred),
                     this, &GattServer::errorOccurred);

    QObject::connect(service.data(), &QLowEnergyService::characteristicChanged, this, &GattServer::onCharacteristicChanged);
    QObject::connect(service.data(), &QLowEnergyService::characteristicRead, this, &GattServer::onCharacteristicChanged);

    // Servis verilerini ayarla
    advertisingData.setDiscoverability(QLowEnergyAdvertisingData::DiscoverabilityGeneral);
    advertisingData.setServices(QList<QBluetoothUuid>() << customServiceUuid);
    advertisingData.setIncludePowerLevel(true);
    advertisingData.setLocalName("Balance Robot");

    // ServicesData'yı da ayarla
    QLowEnergyAdvertisingData scanResponseData;
    scanResponseData.setServices(QList<QBluetoothUuid>() << customServiceUuid);

    // Bluetooth servisini yeniden başlatma
    system("rfkill unblock bluetooth");
    QThread::msleep(1000);

    // Servis parametrelerini ayarla
    QLowEnergyAdvertisingParameters params;
    params.setMode(QLowEnergyAdvertisingParameters::AdvInd);
    params.setInterval(100, 200);

    // Servis vermeyi başlat
    leController->startAdvertising(params, advertisingData, scanResponseData);

    qDebug() << "Servis UUID:" << customServiceUuid.toString();
    qDebug() << "RX UUID:" << rxUuid.toString();
    qDebug() << "TX UUID:" << txUuid.toString();
    qDebug() << "Servis durumu:" << leController->state();

    if(leController->state() != QLowEnergyController::AdvertisingState) {
        qDebug() << "Servis verme başarısız oldu, Bluetooth servisini yeniden başlatıyorum...";
        resetBluetoothService();

        // Yeniden deneme
        QThread::msleep(2000);
        leController->startAdvertising(params, advertisingData, scanResponseData);
        qDebug() << "Yeniden deneme sonrası durum:" << leController->state();
    }

    if(leController->state() == QLowEnergyController::AdvertisingState)
    {
        auto statusText = QString("Listening for Ble connection %1 with service UUID %2")
        .arg(advertisingData.localName())
            .arg(customServiceUuid.toString());
        emit sendInfo(statusText);
        qDebug() << statusText;
    }
    else
    {
        auto statusText = QString("Ble connection can not start for %1").arg(advertisingData.localName());
        emit sendInfo(statusText);
        qDebug() << statusText;
    }

    // RXUUID ve TXUUID sabitlerine değer atama kaldırıldı çünkü bunlar #define ile tanımlanmış
}

void GattServer::writeValue(const QByteArray &value)
{
    try {
        if (leController == nullptr || leController->state() != QLowEnergyController::ConnectedState) {
            qDebug() << "Warning: Attempting to write value while not connected";
            return;
        }

        // Tanımlı hizmet UUID'sini kullan
        QBluetoothUuid customServiceUuid = QBluetoothUuid(QString(SCANPARAMETERSUUID));
        const ServicePtr service = services.value(customServiceUuid);

        if (!service) {
            qDebug() << "Error: Service not found in writeValue(), looking for:" << customServiceUuid.toString();
            qDebug() << "Available services:" << services.keys();
            return;
        }

        // Tanımlı RX UUID'sini kullan
        QBluetoothUuid rxUuid = QBluetoothUuid(QString(RXUUID));
        QLowEnergyCharacteristic cCharacteristic = service->characteristic(rxUuid);
        if (!cCharacteristic.isValid()) {
            qDebug() << "Error: Invalid characteristic in writeValue(), looking for:" << QString(RXUUID);
            return;
        }

        service->writeCharacteristic(cCharacteristic, value);
    }
    catch (const std::exception& e) {
        qDebug() << "Exception in writeValue: " << e.what();
    }
    catch (...) {
        qDebug() << "Unknown exception in writeValue";
    }
}

// resetBluetoothService fonksiyonu iyileştirildi
void GattServer::resetBluetoothService()
{
    try {
        if(leController->state() == QLowEnergyController::AdvertisingState)
            leController->stopAdvertising();

        qDebug() << "Resetting Ble connection...";

        // Bluetooth servisini yeniden başlatma
        system("sudo rfkill unblock bluetooth");
        QThread::msleep(1000);
        system("sudo service bluetooth restart");
        QThread::msleep(2000);
        system("sudo hciconfig hci0 reset");
        QThread::msleep(1000);

        // Bazı durumlarda BLE aygıtının yeniden başlatılması gerekebilir
        system("sudo hciconfig hci0 down");
        QThread::msleep(500);
        system("sudo hciconfig hci0 up");
        QThread::msleep(1000);
    }
    catch(const std::exception& e) {
        qDebug() << "Error during Bluetooth reset: " << e.what();
    }
}

void GattServer::stopBleService()
{
    if (leController->state() == QLowEnergyController::AdvertisingState || leController->state() == QLowEnergyController::ConnectedState)
    {
        QByteArray textData = "Ble service stopped!";
        writeValue(textData);

        if (leController->state() == QLowEnergyController::ConnectedState)
            leController->disconnectFromDevice();

        // Stop advertising if needed
        if (leController->state() == QLowEnergyController::AdvertisingState) {
            leController->stopAdvertising();
        }

        // Emit appropriate status information
        auto statusText = QString("Ble service stopped for %1").arg(advertisingData.localName());
        emit sendInfo(statusText);
    }
}

void GattServer::readValue()
{
    // Use the custom service UUID
    QBluetoothUuid customServiceUuid = QBluetoothUuid(QString(SCANPARAMETERSUUID));
    const ServicePtr service = services.value(customServiceUuid);

    if (!service) {
        qDebug() << "Error: Service not found in readValue()";
        return;
    }

    QBluetoothUuid txUuid = QBluetoothUuid(QString(TXUUID));
    QLowEnergyCharacteristic cCharacteristic = service->characteristic(txUuid);

    if (!cCharacteristic.isValid()) {
        qDebug() << "Error: TX characteristic not valid";
        return;
    }

    service->readCharacteristic(cCharacteristic);
}

void GattServer::onCharacteristicChanged(const QLowEnergyCharacteristic &c, const QByteArray &value)
{
    // Print raw data for debugging
    QString hexData;
    for (int i = 0; i < value.size(); i++) {
        hexData += QString("%1 ").arg((unsigned char)value.at(i), 2, 16, QChar('0'));
    }

    try {
        // Regular processing for other data
        if (c.uuid() == QBluetoothUuid(QString(TXUUID))) {

            if (!value.isEmpty()) {
                QByteArray safeCopy(value); // Create a copy of the data
                QMetaObject::invokeMethod(this, "safeDataReceived", Qt::QueuedConnection,
                                          Q_ARG(QByteArray, safeCopy));
            }
        }
    }
    catch (const std::exception& e) {
        qDebug() << "Exception in onCharacteristicChanged: " << e.what();
    }
    catch (...) {
        qDebug() << "Unknown exception in onCharacteristicChanged";
    }
}

void GattServer::safeDataReceived(const QByteArray &data) {
    try {
        // Process data safely here
        emit dataReceived(data);
    }
    catch (const std::exception& e) {
        qDebug() << "Exception in safeDataReceived: " << e.what();
    }
    catch (...) {
        qDebug() << "Unknown exception in safeDataReceived";
    }
}

void GattServer::writeValuePeriodically()
{
    // Construct the QByteArray containing the text you want to write
    QByteArray textData = "Hello, BLE Client!";

    // Call the writeValue function with the constructed textData
    writeValue(textData);
}

void GattServer::reConnect()
{
    try {
        if (leController->state() == QLowEnergyController::UnconnectedState) {
            services.clear();
            leController->services().clear();

            addService(serviceData);

            // Use the custom service UUID
            QBluetoothUuid customServiceUuid = QBluetoothUuid(QString(SCANPARAMETERSUUID));
            const ServicePtr service = services.value(customServiceUuid);

            if (service.isNull()) {
                qDebug() << "Error: Service pointer is nullptr in reConnect.";
                qDebug() << "Available services:";
                for (const auto& uuid : services.keys()) {
                    qDebug() << "Service UUID:" << uuid.toString();
                }
                return;
            }

            QObject::connect(service.data(), &QLowEnergyService::characteristicChanged,
                             this, &GattServer::onCharacteristicChanged);
            QObject::connect(service.data(), &QLowEnergyService::characteristicRead,
                             this, &GattServer::onCharacteristicChanged);

            leController->startAdvertising(params, advertisingData);

            while (leController->state() != QLowEnergyController::AdvertisingState) {
                resetBluetoothService();
                leController->startAdvertising(params, advertisingData);
            }

            if (leController->state() == QLowEnergyController::AdvertisingState) {
                auto statusText = QString("Listening for Ble connection %1").arg(advertisingData.localName());
                emit sendInfo(statusText);
            } else {
                auto statusText = QString("Ble connection cannot start for %1").arg(advertisingData.localName());
                emit sendInfo(statusText);
            }
        }
    } catch(const std::exception& e) {
        qDebug() << "Error reconnect: " << e.what();
    }
}


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
    remoteDeviceUuid = leController.data()->remoteDeviceUuid();
    m_ConnectionState = true;
    emit connectionState(m_ConnectionState);
    auto statusText = QString("Connected to device %1").arg(remoteDeviceUuid.toString());
    emit sendInfo(statusText);
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
    qDebug() << statusText;
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

    serviceData.setType(QLowEnergyServiceData::ServiceTypePrimary);
    serviceData.setUuid(QBluetoothUuid::ServiceClassUuid::ScanParameters);

    QLowEnergyCharacteristicData charRxData;
    charRxData.setUuid(QBluetoothUuid(QUuid(RXUUID)));
    charRxData.setProperties(QLowEnergyCharacteristic::Read | QLowEnergyCharacteristic::Notify) ;
    charRxData.setValue(QByteArray(2, 0));
    const QLowEnergyDescriptorData rxClientConfig(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration, QByteArray(2, 0));
    charRxData.addDescriptor(rxClientConfig);
    serviceData.addCharacteristic(charRxData);

    QLowEnergyCharacteristicData charTxData;
    charTxData.setUuid(QBluetoothUuid(QUuid(TXUUID)));
    charTxData.setValue(QByteArray(2, 0));
    charTxData.setProperties(QLowEnergyCharacteristic::Write);
    const QLowEnergyDescriptorData txClientConfig(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration, QByteArray(2, 0));
    charTxData.addDescriptor(txClientConfig);
    serviceData.addCharacteristic(charTxData);

    addService(serviceData);

    const ServicePtr service = services.value(QBluetoothUuid::ServiceClassUuid::ScanParameters);
    Q_ASSERT(service);

    QObject::connect(leController.data(), &QLowEnergyController::connected, this, &GattServer::handleConnected);
    QObject::connect(leController.data(), &QLowEnergyController::disconnected, this, &GattServer::handleDisconnected);
//    QObject::connect(leController.data(), &QLowEnergyController::errorOccurred, this, &GattServer::errorOccurred);
    QObject::connect(leController.data(), SIGNAL(error(QLowEnergyController::Error)), this, SLOT(errorOccurred(QLowEnergyController::Error)));

    QObject::connect(service.data(), &QLowEnergyService::characteristicChanged, this, &GattServer::onCharacteristicChanged);
    QObject::connect(service.data(), &QLowEnergyService::characteristicRead, this, &GattServer::onCharacteristicChanged);

    advertisingData.setDiscoverability(QLowEnergyAdvertisingData::DiscoverabilityGeneral);
    advertisingData.setServices(services.keys());
    advertisingData.setIncludePowerLevel(true);
    advertisingData.setLocalName("Esc Controller");

    // We have to check if advertising succeeded ot not. If there was an advertising error we will
    // try to reinitialize our bluetooth service
    while (leController->state() != QLowEnergyController::AdvertisingState) {
        qDebug() << "Attempting to start advertising...";
        leController->startAdvertising(QLowEnergyAdvertisingParameters(), advertisingData, advertisingData);
        QThread::msleep(1000);  // Add a delay to avoid tight loop
        //run this if not work "rfkill unblock bluetooth"
    }

    if(leController->state()== QLowEnergyController::AdvertisingState)
    {
        // writeTimer->start(1000);
        auto statusText = QString("Listening for Ble connection %1").arg(advertisingData.localName());
        emit sendInfo(statusText);
    }
    else
    {
        auto statusText = QString("Ble connection can not start for %1").arg(advertisingData.localName());
        emit sendInfo(statusText);
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
        qDebug() << statusText;
    }
}

void GattServer::readValue()
{
    const ServicePtr service = services.value(QBluetoothUuid::ServiceClassUuid::ScanParameters);
    Q_ASSERT(service);

    QLowEnergyCharacteristic cCharacteristic = service->characteristic(QBluetoothUuid(QUuid(TXUUID)));
    Q_ASSERT(cCharacteristic.isValid());
    service->readCharacteristic(cCharacteristic);
}

void GattServer::writeValue(const QByteArray &value)
{
    const ServicePtr service = services.value(QBluetoothUuid::ServiceClassUuid::ScanParameters);
    Q_ASSERT(service);

    QLowEnergyCharacteristic cCharacteristic = service->characteristic(QBluetoothUuid(QUuid(RXUUID)));
    Q_ASSERT(cCharacteristic.isValid());
    service->writeCharacteristic(cCharacteristic, value);
}

void GattServer::onCharacteristicChanged(const QLowEnergyCharacteristic &c, const QByteArray &value)
{
    Q_UNUSED(c)
    emit dataReceived(value);
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

            const ServicePtr service = services.value(QBluetoothUuid::ServiceClassUuid::ScanParameters);
            if (service.isNull()) {
                qDebug() << "Error: Service pointer is nullptr in reConnect.";
                qDebug() << "Available services:";
                for (const auto& uuid : services.keys()) {
                    qDebug() << "Service UUID:" << uuid.toString();
                }
                return;
            }

            QObject::connect(service.data(), &QLowEnergyService::characteristicChanged, this, &GattServer::onCharacteristicChanged);
            QObject::connect(service.data(), &QLowEnergyService::characteristicRead, this, &GattServer::onCharacteristicChanged);

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


void GattServer::resetBluetoothService()
{
    try{

        if(leController->state()==QLowEnergyController::AdvertisingState)
            leController->stopAdvertising();

        qDebug() << "Resetting Ble connection.";

        system("sudo service bluetooth stop");
        QThread::sleep(1);
        system("sudo service bluetooth start");
        QThread::sleep(1);
    }
    catch(const std::exception& e) {
        qDebug() << "Error reset: " << e.what();
    }
}

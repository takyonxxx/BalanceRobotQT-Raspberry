#include "gattserver.h"

#define SERVICEUUID   "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RXUUID        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define TXUUID        "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

GattServer *GattServer::theInstance_= nullptr;
static QScopedPointer<QLowEnergyController> bleController;
static QHash<QBluetoothUuid, ServicePtr> services;
static QBluetoothAddress remoteDevice;
//static QByteArray deviceName() { return "Balance Robot GATT server"; }

static bool m_ConnectionState = false;

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
    bleController.reset(QLowEnergyController::createPeripheral());

    addService();
    startAdvertising();

    const ServicePtr service = services.value(QBluetoothUuid(QUuid(SERVICEUUID)));
    Q_ASSERT(service);

    QObject::connect(bleController.data(), &QLowEnergyController::connected, this, &GattServer::handleConnected);
    QObject::connect(bleController.data(), &QLowEnergyController::disconnected, this, &GattServer::handleDisconnected);
    QObject::connect(bleController.data(), SIGNAL(error(QLowEnergyController::Error)), this, SLOT(controllerError(QLowEnergyController::Error)));

    QObject::connect(service.data(), &QLowEnergyService::characteristicChanged, this, &GattServer::onCharacteristicChanged);
    QObject::connect(service.data(), &QLowEnergyService::characteristicRead, this, &GattServer::onCharacteristicChanged);

    qDebug() << "Listening for Ble connection.";
}

GattServer::~GattServer()
{
}

void GattServer::controllerError(QLowEnergyController::Error error)
{
    QString errorDesc =  "Controller Error:" + error;
    qDebug() << errorDesc;
}

void GattServer::handleConnected()
{
    remoteDevice = bleController.data()->remoteAddress();
    m_ConnectionState = true;   
    emit connectionState(m_ConnectionState);
    qDebug() << "Connected to " <<  remoteDevice;
}

void GattServer::handleDisconnected()
{
    m_ConnectionState = false;    
    emit connectionState(m_ConnectionState);
    qDebug() << "Disconnected from " <<  remoteDevice;

    if(bleController && services.count() != 0)
    {
        startAdvertising();
        qDebug() << "Listening for Ble connection.";
    }
}

void GattServer::addService(const QLowEnergyServiceData &serviceData)
{
    const ServicePtr service(bleController->addService(serviceData));
    Q_ASSERT(service);

    services.insert(service->serviceUuid(), service);
}
void GattServer::addService()
{
    QLowEnergyServiceData serviceData;
    serviceData.setUuid(QBluetoothUuid(QUuid(SERVICEUUID)));
    serviceData.setType(QLowEnergyServiceData::ServiceTypePrimary);

    QLowEnergyCharacteristicData charRxData;
    charRxData.setUuid(QBluetoothUuid(QUuid(RXUUID)));
    charRxData.setProperties(QLowEnergyCharacteristic::Notify) ;
    charRxData.setValue(QByteArray(2, 0));
    const QLowEnergyDescriptorData rxClientConfig(QBluetoothUuid::ClientCharacteristicConfiguration, QByteArray(2, 0));
    charRxData.addDescriptor(rxClientConfig);
    serviceData.addCharacteristic(charRxData);

    QLowEnergyCharacteristicData charTxData;
    charTxData.setUuid(QBluetoothUuid(QUuid(TXUUID)));
    charTxData.setValue(QByteArray(2, 0));
    charTxData.setProperties(QLowEnergyCharacteristic::Write);
    const QLowEnergyDescriptorData txClientConfig(QBluetoothUuid::ClientCharacteristicConfiguration, QByteArray(2, 0));
    charTxData.addDescriptor(txClientConfig);
    serviceData.addCharacteristic(charTxData);

    addService(serviceData);
}

void GattServer::startAdvertising()
{
    QLowEnergyAdvertisingParameters params;
    params.setMode(QLowEnergyAdvertisingParameters::AdvInd);
    QLowEnergyAdvertisingData data;
    data.setDiscoverability(QLowEnergyAdvertisingData::DiscoverabilityGeneral);
    //data.setLocalName("BalanceRobotServer");
    data.setServices(services.keys());
    data.setIncludePowerLevel(true);
    bleController->setRemoteAddressType(QLowEnergyController::RandomAddress);
    bleController->startAdvertising(params, data);
}

void GattServer::readValue()
{
    const ServicePtr service = services.value(QBluetoothUuid(QUuid(SERVICEUUID)));
    Q_ASSERT(service);

    QLowEnergyCharacteristic cCharacteristic = service->characteristic(QBluetoothUuid(QUuid(TXUUID)));
    Q_ASSERT(cCharacteristic.isValid());
    service->readCharacteristic(cCharacteristic);
}

void GattServer::writeValue(const QByteArray &value)
{
    const ServicePtr service = services.value(QBluetoothUuid(QUuid(SERVICEUUID)));
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

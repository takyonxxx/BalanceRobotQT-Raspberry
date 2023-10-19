#include "bluetoothclient.h"

BluetoothClient::BluetoothClient() :
    m_control(nullptr),
    m_service(nullptr),
    m_state(bluetoothleState::Idle)
{
    /* 1 Step: Bluetooth LE Device Discovery */
    m_deviceDiscoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    m_deviceDiscoveryAgent->setLowEnergyDiscoveryTimeout(60000);
    /* Device Discovery Initialization */
    connect(m_deviceDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BluetoothClient::addDevice);
    connect(m_deviceDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &BluetoothClient::deviceScanError);
    connect(m_deviceDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BluetoothClient::scanFinished);
    connect(m_deviceDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::canceled,
            this, &BluetoothClient::scanFinished);
}

BluetoothClient::~BluetoothClient(){
    if(current_device)
        delete current_device;
}

void BluetoothClient::getDeviceList(QList<QString> &qlDevices){

    if(m_state == bluetoothleState::ScanFinished && current_device)
    {
        qlDevices.append(current_device->getName());
    }
    else
    {
        qlDevices.clear();
    }
}

void BluetoothClient::addDevice(const QBluetoothDeviceInfo &device)
{
    /* Is it a LE Bluetooth device? */
    if (device.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)
    {
        if(device.name().isEmpty()) return;

        QString info = "Device Found: " + device.name() + "\nUuid: " + device.deviceUuid().toString();        

        if(device.name().startsWith("Bal"))
        {
            emit statusChanged(info);
            current_device = new DeviceInfo(device);
            m_deviceDiscoveryAgent->stop();
            emit m_deviceDiscoveryAgent->finished();
            startConnect(0);            
        }
    }
}

void BluetoothClient::scanFinished()
{
    if (!current_device)
    {
        QString info = "Low Energy device not found.";
        emit statusChanged(info);
    }
    setState(ScanFinished);
}

void BluetoothClient::deviceScanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    QString info;
    if (error == QBluetoothDeviceDiscoveryAgent::PoweredOffError)
        info = "The Bluetooth adaptor is powered off, power it on before doing discovery.";
    else if (error == QBluetoothDeviceDiscoveryAgent::InputOutputError)
        info = "Writing or reading from the device resulted in an error.";
    else {
        static QMetaEnum qme = m_deviceDiscoveryAgent->metaObject()->enumerator(
            m_deviceDiscoveryAgent->metaObject()->indexOfEnumerator("Error"));
        info = "Error: " + QLatin1String(qme.valueToKey(error));
    }

    setState(Error);
    emit statusChanged(info);
}

void BluetoothClient::startScan(){

    setState(Scanning);
    current_device = nullptr;
    m_deviceDiscoveryAgent->start();
}

void BluetoothClient::startConnect(int i){

    m_qvMeasurements.clear();

    if (m_control) {
        m_control->disconnectFromDevice();
        delete m_control;
        m_control = 0;
    }

    /* 2 Step: QLowEnergyController */
    m_control = QLowEnergyController::createCentral(current_device->getDevice(), this);
    m_control ->setRemoteAddressType(QLowEnergyController::RandomAddress);

    connect(m_control, SIGNAL(error(QLowEnergyController::Error)), this, SLOT(controllerError(QLowEnergyController::Error)));

    connect(m_control, &QLowEnergyController::connected, this, &BluetoothClient::deviceConnected);
    connect(m_control, &QLowEnergyController::disconnected, this, &BluetoothClient::deviceDisconnected);
    connect(m_control, &QLowEnergyController::serviceDiscovered, this, &BluetoothClient::serviceDiscovered);
    connect(m_control, &QLowEnergyController::discoveryFinished, this, &BluetoothClient::serviceScanDone);

    /* Start connecting to device */
    m_control->connectToDevice();
    setState(Connecting);
}

void BluetoothClient::setService_name(const QString &newService_name)
{
    m_service_name = newService_name;
}

void BluetoothClient::serviceDiscovered(const QBluetoothUuid &gatt)
{
    m_bFoundUARTService =true;
    current_gatt = gatt;
}

void BluetoothClient::serviceScanDone()
{
    delete m_service;
    m_service=0;

    if(m_bFoundUARTService)
    {
        m_service = m_control->createServiceObject(current_gatt, this);
    }

    if(!m_service)
    {
        QString info =  "Service Found: " + current_gatt.toString() ;
        emit statusChanged(info);;
        disconnectFromDevice();
        setState(DisConnected);
        return;
    }

    /* 3 Step: Service Discovery */
    connect(m_service, SIGNAL(stateChanged(QLowEnergyService::ServiceState)),
            this, SLOT(serviceStateChanged(QLowEnergyService::ServiceState)));
    connect(m_service, SIGNAL(characteristicChanged(QLowEnergyCharacteristic,QByteArray)),
            this, SLOT(updateData(QLowEnergyCharacteristic,QByteArray)));
    connect(m_service, SIGNAL(descriptorWritten(QLowEnergyDescriptor,QByteArray)),
            this, SLOT(confirmedDescriptorWrite(QLowEnergyDescriptor,QByteArray)));

    m_service->discoverDetails();
    setState(ServiceFound);

}

void BluetoothClient::disconnectFromDevice()
{
    m_control->disconnectFromDevice();
}

void BluetoothClient::deviceDisconnected()
{
    delete m_service;
    m_service = 0;
    setState(DisConnected);
}

void BluetoothClient::deviceConnected()
{
    m_control->discoverServices();
    setState(Connected);
}

void BluetoothClient::controllerError(QLowEnergyController::Error error)
{
    QString info = QStringLiteral("Controller Error: ") + m_control->errorString();
    emit statusChanged(info);
}

void BluetoothClient::searchCharacteristic()
{
    if(m_service){
        foreach (QLowEnergyCharacteristic c, m_service->characteristics())
        {
            if(c.isValid())
            {
                if (c.properties() & QLowEnergyCharacteristic::WriteNoResponse ||
                    c.properties() & QLowEnergyCharacteristic::Write)
                {
                    m_writeCharacteristic = c;
                    QString info =  "Tx Characteristic found\n" + c.uuid().toString();
                    emit statusChanged(info);

                    if(c.properties() & QLowEnergyCharacteristic::WriteNoResponse)
                        m_writeMode = QLowEnergyService::WriteWithoutResponse;
                    else
                        m_writeMode = QLowEnergyService::WriteWithResponse;
                }
                if (c.properties() & QLowEnergyCharacteristic::Notify || c.properties() & QLowEnergyCharacteristic::Read)
                {
                    m_readCharacteristic = c;
                    QString info =  "Rx Characteristic found\n" + c.uuid().toString();
                    emit statusChanged(info);

                    m_notificationDescRx = c.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
                    if (m_notificationDescRx.isValid())
                    {
                        m_service->writeDescriptor(m_notificationDescRx, QByteArray::fromHex("0100"));

                        QString info =  "Write Rx Descriptor defined.\n" + c.uuid().toString();
                        emit statusChanged(info);
                    }
                }
            }
        }
    }
}

/* Slotes for QLowEnergyService */
void BluetoothClient::serviceStateChanged(QLowEnergyService::ServiceState s)
{
    if (s == QLowEnergyService::ServiceDiscovered)
    {
        searchCharacteristic();
    }
}

void BluetoothClient::updateData(const QLowEnergyCharacteristic &c,const QByteArray &value)
{
    emit newData(value);
}

void BluetoothClient::confirmedDescriptorWrite(const QLowEnergyDescriptor &d, const QByteArray &value)
{
    setState(AcquireData);
}

void BluetoothClient::writeData(QByteArray data)
{
    if(m_service && m_writeCharacteristic.isValid())
    {
        m_service->writeCharacteristic(m_writeCharacteristic, data, m_writeMode);
    }
}

void BluetoothClient::setState(BluetoothClient::bluetoothleState newState)
{
    if (m_state == newState)
        return;

    m_state = newState;
    emit changedState(newState);
}

BluetoothClient::bluetoothleState BluetoothClient::getState() const {
    return m_state;
}

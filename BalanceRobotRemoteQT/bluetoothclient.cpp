#include "bluetoothclient.h"

BluetoothClient::BluetoothClient() :    
    m_control(nullptr),
    m_currentDevice(QBluetoothDeviceInfo()),
    m_service(nullptr),
    m_state(bluetoothleState::Idle)
{
    /* 1 Step: Bluetooth LE Device Discovery */
    m_deviceDiscoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    m_deviceDiscoveryAgent->setLowEnergyDiscoveryTimeout(3000);
    /* Device Discovery Initialization */
    connect(m_deviceDiscoveryAgent, SIGNAL(deviceDiscovered(const QBluetoothDeviceInfo&)),
            this, SLOT(addDevice(const QBluetoothDeviceInfo&)));
    connect(m_deviceDiscoveryAgent, SIGNAL(error(QBluetoothDeviceDiscoveryAgent::Error)),
            this, SLOT(deviceScanError(QBluetoothDeviceDiscoveryAgent::Error)));
    connect(m_deviceDiscoveryAgent, SIGNAL(finished()), this, SLOT(scanFinished()));
}

BluetoothClient::~BluetoothClient(){

}

void BluetoothClient::getDeviceList(QList<QString> &qlDevices){

    if(m_state == bluetoothleState::ScanFinished)
    {
        qlDevices = m_qlFoundDevices;
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

        QString info =  device.name() + "\nAddress: "
                + device.address().toString();

         qDebug() << device.name() ;

        if(device.name().startsWith("BlueZ") || device.name().startsWith("rasp"))
        {
            emit statusChanged(info);
            DeviceInfo *dev = new DeviceInfo(device);
            m_qlDevices.append(dev);
            m_deviceDiscoveryAgent->stop();
            emit m_deviceDiscoveryAgent->finished();
            startConnect(0);
        }
    }
}

void BluetoothClient::scanFinished()
{
    if (m_qlDevices.size() == 0)
    {
        QString info = "No Low Energy devices found";
        emit statusChanged(info);
    }
    else
    {
        for (int i = 0; i < m_qlDevices.size(); i++) {
            m_qlFoundDevices.append(((DeviceInfo*) m_qlDevices.at(i))->getName());
        }

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
    qDeleteAll(m_qlDevices);
    m_qlDevices.clear();

    m_qlFoundDevices.clear();
    m_deviceDiscoveryAgent->start();
    QString info = "Searching Balance Robot Ble Device...";

    emit statusChanged(info);
}

void BluetoothClient::startConnect(int i){

    m_qvMeasurements.clear();

    m_currentDevice.setDevice(((DeviceInfo*)m_qlDevices.at(i))->getDevice());

    if (m_control) {
        m_control->disconnectFromDevice();
        delete m_control;
        m_control = 0;

    }

    /* 2 Step: QLowEnergyController */
    m_control = new QLowEnergyController(m_currentDevice.getDevice(), this);
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

void BluetoothClient::serviceDiscovered(const QBluetoothUuid &gatt){


    if(gatt==QBluetoothUuid(QUuid(UARTSERVICEUUID)))
    {
        m_bFoundUARTService =true;

        QString info =  "Service Discovered:\n" + QBluetoothUuid(QUuid(UARTSERVICEUUID)).toString() ;
        emit statusChanged(info);
    }
}

void BluetoothClient::serviceScanDone()
{
    delete m_service;
    m_service=0;

    if(m_bFoundUARTService)
    {
        m_service = m_control->createServiceObject(QBluetoothUuid(QUuid(UARTSERVICEUUID)),this);
    }

    if(!m_service)
    {
        QString info =  "UART service not found";
        emit statusChanged(info);
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
    QString info =  "Controller Error:" + error;
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

                    m_notificationDescRx = c.descriptor(QBluetoothUuid::ClientCharacteristicConfiguration);
                    if (m_notificationDescRx.isValid())
                    {
                        m_service->writeDescriptor(m_notificationDescRx, QByteArray::fromHex("0100"));

                        QString info =  "Write Rx Descriptor ok.\n" + c.uuid().toString();
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

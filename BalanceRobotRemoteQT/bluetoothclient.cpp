#include "bluetoothclient.h"
#include <QTimer>
#include <QBluetoothLocalDevice>

BluetoothClient::BluetoothClient() :
    m_control(nullptr),
    m_service(nullptr),
    m_state(bluetoothleState::Idle),
    m_bFoundUARTService(false)
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
        emit statusChanged(info);

        if(device.name().startsWith("Balance"))
        {
            emit statusChanged("Found Balance Robot device: " + device.name());
            current_device = new DeviceInfo(device);
            m_deviceDiscoveryAgent->stop();
            QTimer::singleShot(500, this, [this]() {
                emit m_deviceDiscoveryAgent->finished();
                startConnect(0);
            });
        }
    }
}

void BluetoothClient::scanFinished()
{
    if (!current_device)
    {
        QString info = "Low Energy device not found. Make sure your Balance Robot is powered on.";
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
    emit statusChanged("Starting Bluetooth LE scan...");
    setState(Scanning);
    current_device = nullptr;
    m_bFoundUARTService = false;

    // Set up the discovery agent to only look for LE devices
    // Use useLowEnergyDiscoveryTimeout instead of setDiscoveryMethods
    m_deviceDiscoveryAgent->setLowEnergyDiscoveryTimeout(60000);

    // Start the discovery
    m_deviceDiscoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BluetoothClient::startConnect(int i){
    emit statusChanged("Connecting to device...");
    m_qvMeasurements.clear();

    if (m_control) {
        m_control->disconnectFromDevice();
        delete m_control;
        m_control = nullptr;
    }

    /* 2 Step: QLowEnergyController */
    m_control = QLowEnergyController::createCentral(current_device->getDevice(), this);
    m_control->setRemoteAddressType(QLowEnergyController::RandomAddress);

    // Ensure device is in connectable mode
    QBluetoothLocalDevice *localDevice = new QBluetoothLocalDevice();
    localDevice->setHostMode(QBluetoothLocalDevice::HostConnectable);
    delete localDevice;

    connect(m_control, &QLowEnergyController::errorOccurred, this, &BluetoothClient::errorOccurred);
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
    QString discoveredUuid = gatt.toString().remove("{").remove("}").toLower();
    QString targetUuid = QString(SCANPARAMETERSUUID).toLower();

    emit statusChanged("Service discovered: " + discoveredUuid);

    if (discoveredUuid.contains(targetUuid) || targetUuid.contains(discoveredUuid)) {
        m_bFoundUARTService = true;
        current_gatt = gatt;
        emit statusChanged("Found target UART service");
    }
}

void BluetoothClient::serviceScanDone()
{
    emit statusChanged("Service scan completed");

    delete m_service;
    m_service = nullptr;

    if(m_bFoundUARTService)
    {
        m_service = m_control->createServiceObject(current_gatt, this);
        emit statusChanged("Created service object for UART");
    }

    if(!m_service)
    {
        emit statusChanged("Failed to create service object. Disconnecting.");
        disconnectFromDevice();
        setState(DisConnected);
        return;
    }

    QString info = "Service Found: " + current_gatt.toString();
    emit statusChanged(info);

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
    emit statusChanged("Disconnecting from device...");
    if (m_control) {
        m_control->disconnectFromDevice();
    } else {
        setState(DisConnected);
    }
}

void BluetoothClient::deviceDisconnected()
{
    emit statusChanged("Device disconnected. Try reconnecting if needed.");

    delete m_service;
    m_service = nullptr;
    setState(DisConnected);
}

void BluetoothClient::deviceConnected()
{
    emit statusChanged("Connected to device. Discovering services...");
    m_control->discoverServices();
    setState(Connected);
}

void BluetoothClient::errorOccurred(QLowEnergyController::Error newError)
{
    auto statusText = QString("Controller Error: %1").arg(newError);
    qDebug() << statusText;
    emit statusChanged(statusText);

    // Add a recovery attempt for certain errors
    if (newError == QLowEnergyController::ConnectionError) {
        emit statusChanged("Connection error. Trying to reconnect in 3 seconds...");
        QTimer::singleShot(3000, this, [this]() {
            if (current_device && m_state == Error) {
                startConnect(0);
            }
        });
    }
}

void BluetoothClient::controllerError(QLowEnergyController::Error error)
{
    QString info = QStringLiteral("Controller Error: ") + m_control->errorString();
    emit statusChanged(info);
}

void BluetoothClient::searchCharacteristic()
{
    emit statusChanged("Searching for characteristics...");

    if(m_service){
        foreach (QLowEnergyCharacteristic c, m_service->characteristics())
        {
            if(c.isValid())
            {
                if (c.properties() & QLowEnergyCharacteristic::WriteNoResponse ||
                    c.properties() & QLowEnergyCharacteristic::Write)
                {
                    m_writeCharacteristic = c;
                    QString info = "Tx Characteristic found\n" + c.uuid().toString();
                    emit statusChanged(info);

                    if(c.properties() & QLowEnergyCharacteristic::WriteNoResponse)
                        m_writeMode = QLowEnergyService::WriteWithoutResponse;
                    else
                        m_writeMode = QLowEnergyService::WriteWithResponse;
                }
                if (c.properties() & QLowEnergyCharacteristic::Notify || c.properties() & QLowEnergyCharacteristic::Read)
                {
                    m_readCharacteristic = c;
                    QString info = "Rx Characteristic found\n" + c.uuid().toString();
                    emit statusChanged(info);

                    m_notificationDescRx = c.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
                    if (m_notificationDescRx.isValid())
                    {
                        // Add a small delay before writing the descriptor
                        QTimer::singleShot(100, this, [this]() {
                            m_service->writeDescriptor(m_notificationDescRx, QByteArray::fromHex("0100"));
                            emit statusChanged("Writing notification descriptor...");
                        });
                    }
                }
            }
        }
    } else {
        emit statusChanged("Service object is invalid");
    }
}

/* Slots for QLowEnergyService */
void BluetoothClient::serviceStateChanged(QLowEnergyService::ServiceState s)
{
    if (s == QLowEnergyService::ServiceDiscovered)
    {
        emit statusChanged("Service details discovered. Looking for characteristics...");
        searchCharacteristic();
    } else if (s == QLowEnergyService::InvalidService) {
        emit statusChanged("Service became invalid");
    } else if (s == QLowEnergyService::DiscoveringService) {
        emit statusChanged("Discovering service details...");
    }
}

void BluetoothClient::updateData(const QLowEnergyCharacteristic &c, const QByteArray &value)
{
    // Check if data is valid before processing
    if (value.isEmpty()) {
        emit statusChanged("Received empty data packet");
        return;
    }

    // Log the received data
    QString hexData;
    for (char byte : value) {
        hexData += QString("%1 ").arg(static_cast<unsigned char>(byte), 2, 16, QChar('0'));
    }

    // Process the data safely
    try {
        emit newData(value);
    } catch (const std::exception& e) {
        emit statusChanged(QString("Error processing data: %1").arg(e.what()));
    } catch (...) {
        emit statusChanged("Unknown error processing data");
    }
}

void BluetoothClient::confirmedDescriptorWrite(const QLowEnergyDescriptor &d, const QByteArray &value)
{
    emit statusChanged("Notification descriptor written successfully");
    setState(AcquireData);
}

void BluetoothClient::writeData(QByteArray data)
{
    if(m_service && m_writeCharacteristic.isValid())
    {
        // Add a small delay before writing to avoid GATT timeouts
        QTimer::singleShot(50, this, [this, data]() {
            m_service->writeCharacteristic(m_writeCharacteristic, data, m_writeMode);
        });
    } else {
        emit statusChanged("Cannot write data: service or characteristic not valid");
    }
}

void BluetoothClient::setState(BluetoothClient::bluetoothleState newState)
{
    if (m_state == newState)
        return;

    m_state = newState;
    emit changedState(newState);

    // Add debug information about state changes
    QString stateStr;
    switch (newState) {
    case Idle: stateStr = "Idle"; break;
    case Scanning: stateStr = "Scanning"; break;
    case ScanFinished: stateStr = "Scan Finished"; break;
    case Connecting: stateStr = "Connecting"; break;
    case Connected: stateStr = "Connected"; break;
    case ServiceFound: stateStr = "Service Found"; break;
    case DisConnected: stateStr = "Disconnected"; break;
    case AcquireData: stateStr = "Ready for Data"; break;
    case Error: stateStr = "Error"; break;
    default: stateStr = "Unknown"; break;
    }

    qDebug() << "Bluetooth state changed to:" << stateStr;
}

BluetoothClient::bluetoothleState BluetoothClient::getState() const {
    return m_state;
}

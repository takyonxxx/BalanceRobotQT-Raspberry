#ifndef BLUETOOTHCLIENT_H
#define BLUETOOTHCLIENT_H

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QMetaEnum>
#include <QLatin1String>
#include <qregularexpression.h>
#include <deviceinfo.h>

#define UARTSERVICEUUID     "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RXUUID              "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define TXUUID              "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

class BluetoothClient : public QObject
{
    Q_OBJECT

public:
    //TODO Error handling
    enum bluetoothleState {
        Idle = 0,
        Scanning,
        ScanFinished,
        Connecting,
        Connected,
        DisConnected,
        ServiceFound,
        AcquireData,
        Error
    };
    Q_ENUM(bluetoothleState)

    BluetoothClient();
    ~BluetoothClient();

    void writeData(QByteArray data);
    void setState(BluetoothClient::bluetoothleState newState);
    BluetoothClient::bluetoothleState getState() const;
    void getDeviceList(QList<QString> &qlDevices);
    void disconnectFromDevice();

private slots:
    /* Slots for QBluetothDeviceDiscoveryAgent */
    void addDevice(const QBluetoothDeviceInfo&);
    void scanFinished();
    void deviceScanError(QBluetoothDeviceDiscoveryAgent::Error);

    /* Slots for QLowEnergyController */
    void serviceDiscovered(const QBluetoothUuid &);
    void serviceScanDone();
    void controllerError(QLowEnergyController::Error);
    void deviceConnected();
    void deviceDisconnected();

    /* Slotes for QLowEnergyService */
    void serviceStateChanged(QLowEnergyService::ServiceState s);
    void updateData(const QLowEnergyCharacteristic &c, const QByteArray &value);
    void confirmedDescriptorWrite(const QLowEnergyDescriptor &d, const QByteArray &value);

    /* Slots for user */
    void startScan();
    void startConnect(int i);

    void searchCharacteristic();

signals:
    /* Signals for user */
    void statusChanged(const QString &status);
    void newData(QByteArray data);
    void changedState(BluetoothClient::bluetoothleState newState);


private:

    QLowEnergyController *m_control;

    DeviceInfo m_currentDevice;
    QBluetoothDeviceDiscoveryAgent *m_deviceDiscoveryAgent;
    QList<QObject*> m_qlDevices;
    QList<QString> m_qlFoundDevices;
    QVector<quint16> m_qvMeasurements;

    QLowEnergyService *m_service;
    QLowEnergyDescriptor m_notificationDescTx;
    QLowEnergyDescriptor m_notificationDescRx;
    QLowEnergyService *m_UARTService;
    bool m_bFoundUARTService;
    BluetoothClient::bluetoothleState m_state;

    QLowEnergyCharacteristic m_readCharacteristic;
    QLowEnergyCharacteristic m_writeCharacteristic;
    QLowEnergyService::WriteMode m_writeMode;

};

#endif // BLUETOOTHCLIENT_H

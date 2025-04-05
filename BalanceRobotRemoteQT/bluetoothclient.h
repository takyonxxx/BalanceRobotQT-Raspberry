#ifndef BLUETOOTHCLIENT_H
#define BLUETOOTHCLIENT_H

#include <QtBluetooth/qbluetoothdeviceinfo.h>
#include <QtBluetooth/qbluetoothuuid.h>
#include <QtBluetooth/qbluetoothdevicediscoveryagent.h>
#include <QtBluetooth/qlowenergycontroller.h>
#include <QtBluetooth/qlowenergyservice.h>
#include <QBluetoothDeviceInfo>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QMetaEnum>
#include <QLatin1String>
#include <qregularexpression.h>
#include <deviceinfo.h>

// Make sure this matches the server's SCANPARAMETERSUUID
#define SCANPARAMETERSUUID  "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
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

    void setService_name(const QString &newService_name);

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
    void errorOccurred(QLowEnergyController::Error newError);

    /* Slotes for QLowEnergyService */
    void serviceStateChanged(QLowEnergyService::ServiceState s);
    void updateData(const QLowEnergyCharacteristic &c, const QByteArray &value);
    void confirmedDescriptorWrite(const QLowEnergyDescriptor &d, const QByteArray &value);
    void searchCharacteristic();

public slots:
    /* Slots for user */
    void startScan();
    void startConnect(int i);

signals:
    /* Signals for user */
    void statusChanged(const QString &status);
    void newData(QByteArray data);
    void changedState(BluetoothClient::bluetoothleState newState);

private:

    QLowEnergyController *m_control;
    QString m_service_name{"Bal"};
    QBluetoothUuid current_gatt{};

    QBluetoothDeviceDiscoveryAgent *m_deviceDiscoveryAgent;
    DeviceInfo *current_device{};
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

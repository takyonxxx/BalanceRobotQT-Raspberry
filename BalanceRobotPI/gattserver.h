#ifndef GATTSERVER_H
#define GATTSERVER_H

#include <QObject>
#include <QtBluetooth>
#include <QtCore/qbytearray.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qlist.h>
#include <QtCore/qscopedpointer.h>

#define SERVICEUUID   "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RXUUID        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define TXUUID        "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

QT_USE_NAMESPACE

typedef QSharedPointer<QLowEnergyService> ServicePtr;

class GattServer : public QObject
{
    Q_OBJECT

public:
    explicit GattServer(QObject *parent = nullptr);
    ~GattServer();

    static GattServer* getInstance();

    void readValue();
    void writeValue(const QByteArray &value);
    void startBleService();
    void stopBleService();
    void resetBluetoothService();
    void reConnect();

private:
    void addService(const QLowEnergyServiceData &serviceData);

    QScopedPointer<QLowEnergyController> leController;
    QHash<QBluetoothUuid, ServicePtr> services;
    QBluetoothAddress remoteDevice;
    QBluetoothUuid remoteDeviceUuid;
    bool m_ConnectionState = false;

    QLowEnergyServiceData serviceData{};
    QLowEnergyAdvertisingParameters params{};
    QLowEnergyAdvertisingData advertisingData{};

    QTimer *writeTimer{};
    void writeValuePeriodically();

    static GattServer *theInstance_;

signals:
    void dataReceived(QByteArray);
    void connectionState(bool);
    void sendInfo(QString);


private slots:

    //QLowEnergyService
    void onCharacteristicChanged(const QLowEnergyCharacteristic &c, const QByteArray &value);
    void onInfoReceived(QString);
    void onSensorReceived(QString);
    void handleConnected();
    void handleDisconnected();
    void errorOccurred(QLowEnergyController::Error newError);
};

#endif

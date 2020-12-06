#ifndef GATTSERVER_H
#define GATTSERVER_H

#include <QObject>
#include <QtBluetooth>
#include <QtCore/qbytearray.h>
#include <QtCore/qcoreapplication.h>
#include <QtCore/qlist.h>
#include <QtCore/qscopedpointer.h>

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

private:
    void addService(const QLowEnergyServiceData &serviceData);
    void startBleService();
    void resetBluetoothService();
    void reconnect();

    QLowEnergyServiceData serviceData{};
    QLowEnergyAdvertisingParameters params{};
    QLowEnergyAdvertisingData advertisingData{};

    static GattServer *theInstance_;

signals:
    void dataReceived(QByteArray);
    void connectionState(bool);


private slots:

    //QLowEnergyService
    void onCharacteristicChanged(const QLowEnergyCharacteristic &c, const QByteArray &value);

    void handleConnected();
    void handleDisconnected();
    void controllerError(QLowEnergyController::Error error);
};

#endif

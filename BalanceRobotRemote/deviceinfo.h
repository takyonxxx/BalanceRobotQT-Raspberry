#ifndef DEVICEINFO_H
#define DEVICEINFO_H

#include <QString>
#include <QObject>
#include <qbluetoothdeviceinfo.h>
#include <qbluetoothaddress.h>

class DeviceInfo: public QObject
{
    Q_OBJECT
public:
    DeviceInfo(const QBluetoothDeviceInfo &device);
    void setDevice(const QBluetoothDeviceInfo &device);
    QString getName() const { return m_device.name(); }
    QString getAddress() const;
    QBluetoothDeviceInfo getDevice() const;

signals:
    void deviceChanged();

private:
    QBluetoothDeviceInfo m_device;
};

#endif // DEVICEINFO_H

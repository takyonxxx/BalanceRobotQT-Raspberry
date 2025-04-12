#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <QLabel>
#include <QComboBox>
#include <QLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QLineEdit>
#include <QString>
#include <QDebug>
#include <QtWidgets>
#include <qregularexpression.h>
#include "message.h"
#include "bluetoothclient.h"
#include <QBluetoothPermission>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QJniEnvironment>
#endif

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private slots:
    void DataHandler(QByteArray data);
    void sendCommand(uint8_t command, uint8_t value);
    void sendString(uint8_t command, QByteArray value);
    void requestData(uint8_t command);
    void changedState(BluetoothClient::bluetoothleState state);
    void statusChanged(const QString &status);
    void on_scrollPP_valueChanged(int value);
    void on_scrollPI_valueChanged(int value);
    void on_scrollPD_valueChanged(int value);
    void on_ConnectClicked();
    void on_ForwardPressed();
    void on_ForwardReleased();
    void on_BackwardPressed();
    void on_BackwardReleased();
    void on_RightPressed();
    void on_RightReleased();
    void on_LeftPressed();
    void on_LeftReleased();
    void on_Exit();
    void on_scrollDS_valueChanged(int value);
    void on_scrollAC_valueChanged(int value);
    void on_m_pBSpeak_clicked();
    void on_m_pBFormat_clicked();

    void on_m_pBArmed_clicked();

signals:
    void connectToDevice(int i);

private:
    // Original methods
    void initButtons();
    void createMessage(uint8_t msgId, uint8_t rw, QByteArray payload, QByteArray *result);
    void parseMessage(QByteArray *data, uint8_t &command, QByteArray &value, uint8_t &rw);

    // New styling methods
    void setupCommonStyles();
    void setupScaledIcons();
    QFont getScaledFont(int baseSize, bool isBold = false);
    QString getButtonStyle(const QString &bgColor);
    QString getLabelStyle(const QString &bgColor);
#if defined(Q_OS_ANDROID)
    void requestBluetoothPermissions();
#endif
#if defined(Q_OS_IOS)
    void requestiOSBluetoothPermissions();
    bool m_iOSBluetoothInitialized =false;
#endif

    // Member variables
    int remoteConstant;
    float m_scaleFactor;  // Added for scaling factor
    Ui::MainWindow *ui;
    QList<QString> m_qlFoundDevices;
    BluetoothClient *m_bleConnection{};
    Message message;
};
#endif // MAINWINDOW_H

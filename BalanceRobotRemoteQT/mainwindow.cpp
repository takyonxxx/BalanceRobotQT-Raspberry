#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    remoteConstant = 50;

    initButtons();

    setWindowTitle(tr("BalanceRobot Remote Control"));

    // Apply common styles with proper scaling for different devices
    setupCommonStyles();

    m_bleConnection = new BluetoothClient();

    connect(m_bleConnection, &BluetoothClient::statusChanged, this, &MainWindow::statusChanged);
    connect(m_bleConnection, SIGNAL(changedState(BluetoothClient::bluetoothleState)),this,SLOT(changedState(BluetoothClient::bluetoothleState)));

    connect(ui->m_pBConnect, SIGNAL(clicked()),this, SLOT(on_ConnectClicked()));

    connect(ui->m_pBForward, SIGNAL(pressed()),this, SLOT(on_ForwardPressed()));
    connect(ui->m_pBForward, SIGNAL(released()),this, SLOT(on_ForwardReleased()));

    connect(ui->m_pBBackward, SIGNAL(pressed()),this, SLOT(on_BackwardPressed()));
    connect(ui->m_pBBackward, SIGNAL(released()),this, SLOT(on_BackwardReleased()));

    connect(ui->m_pBLeft, SIGNAL(pressed()),this, SLOT(on_LeftPressed()));
    connect(ui->m_pBLeft, SIGNAL(released()),this, SLOT(on_LeftReleased()));

    connect(ui->m_pBRight, SIGNAL(pressed()),this, SLOT(on_RightPressed()));
    connect(ui->m_pBRight, SIGNAL(released()),this, SLOT(on_RightReleased()));

    connect(ui->m_pBExit, SIGNAL(clicked()),this, SLOT(on_Exit()));

    statusChanged("No Device Connected.");
}

#if defined(Q_OS_ANDROID)
void MainWindow::requestBluetoothPermissions()
{
    QBluetoothPermission bluetoothPermission;
    bluetoothPermission.setCommunicationModes(QBluetoothPermission::Access);

    switch (qApp->checkPermission(bluetoothPermission)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(bluetoothPermission, this,
                                [this](const QPermission &permission) {
                                    if (qApp->checkPermission(permission) == Qt::PermissionStatus::Granted) {
                                        statusChanged("Bluetooth permission granted. Starting scan...");
                                        m_bleConnection->startScan();
                                    } else {
                                        statusChanged("Bluetooth permission denied. Cannot proceed.");
                                    }
                                });
        break;
    case Qt::PermissionStatus::Granted:
        statusChanged("Bluetooth permission already granted. Starting scan...");
        m_bleConnection->startScan();
        break;
    case Qt::PermissionStatus::Denied:
        statusChanged("Bluetooth permission denied. Please enable in Settings.");
        break;
    }
}
#endif

void MainWindow::setupCommonStyles()
{
    // Determine if we're on Android and set scale factor accordingly
#ifdef Q_OS_ANDROID
    // Higher scale factor for Android
    m_scaleFactor = 1.25;
#else
    // Normal scale for desktop
    m_scaleFactor = 1.0;
#endif

    // Apply styles to text status area
    ui->m_textStatus->setStyleSheet("color: #cccccc; background-color: #003333;");
    ui->m_textStatus->setFont(getScaledFont(12));

    // Apply styles to labels
    QString labelStyle = getLabelStyle("#FF4633");
    ui->labelPP->setStyleSheet(labelStyle);
    ui->labelPI->setStyleSheet(labelStyle);
    ui->labelPD->setStyleSheet(labelStyle);
    ui->labelDS->setStyleSheet(labelStyle);
    ui->labelAC->setStyleSheet(labelStyle);

    // Apply style to line edit
    ui->lineEdit_Speak->setStyleSheet("color: #ffffff; background-color: #FF4633;");
    ui->lineEdit_Speak->setFont(getScaledFont(18));

    // Apply styles to control buttons
    ui->m_pBForward->setStyleSheet("color: #ffffff; background-color: transparent;");
    ui->m_pBBackward->setStyleSheet("color: #ffffff; background-color: transparent;");
    ui->m_pBLeft->setStyleSheet("color: #ffffff; background-color: transparent;");
    ui->m_pBRight->setStyleSheet("color: #ffffff; background-color: transparent;");

    ui->m_pBForward->setMaximumHeight(30);
    ui->m_pBBackward->setMaximumHeight(30);

    // Apply styles to action buttons
    QString blueButtonStyle = getButtonStyle("#336699");
    QString redButtonStyle = getButtonStyle("#900C3F");

    ui->m_pBConnect->setStyleSheet(blueButtonStyle);
    ui->m_pBExit->setStyleSheet(blueButtonStyle);
    ui->m_pBSpeak->setStyleSheet(redButtonStyle);

    // Set scaled fonts for labels
    ui->labelPP->setFont(getScaledFont(22));
    ui->labelPI->setFont(getScaledFont(22));
    ui->labelPD->setFont(getScaledFont(22));
    ui->labelDS->setFont(getScaledFont(22));
    ui->labelAC->setFont(getScaledFont(22));

    // Set scaled fonts for buttons
    ui->m_pBConnect->setFont(getScaledFont(24, true));
    ui->m_pBExit->setFont(getScaledFont(24, true));
    ui->m_pBSpeak->setFont(getScaledFont(24, true));

    // Setup scaled icons for direction buttons
    setupScaledIcons();
}

QFont MainWindow::getScaledFont(int baseSize, bool isBold)
{
    QFont font;
    font.setPointSize(static_cast<int>(baseSize * m_scaleFactor));
    if (isBold) {
        font.setBold(true);
    }
    return font;
}

QString MainWindow::getButtonStyle(const QString &bgColor)
{
    return QString("color: #ffffff; background-color: %1;").arg(bgColor);
}

QString MainWindow::getLabelStyle(const QString &bgColor)
{
    return QString("color: #ffffff; background-color: %1;").arg(bgColor);
}

void MainWindow::setupScaledIcons()
{
    // Calculate icon size based on scale factor
    int iconWidth = static_cast<int>(96 * m_scaleFactor);
    int iconHeight = static_cast<int>(64 * m_scaleFactor);

    QSize iconSize(iconWidth, iconHeight);

    // Forward button
    QPixmap pixmapf(":/icons/forward.png");
    QIcon ForwardIcon(pixmapf.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->m_pBForward->setIcon(ForwardIcon);
    ui->m_pBForward->setIconSize(iconSize);
    ui->m_pBForward->setFixedSize(iconSize);

    // Backward button
    QPixmap pixmapb(":/icons/back.png");
    QIcon BackwardIcon(pixmapb.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->m_pBBackward->setIcon(BackwardIcon);
    ui->m_pBBackward->setIconSize(iconSize);
    ui->m_pBBackward->setFixedSize(iconSize);

    // Left button
    QPixmap pixmapl(":/icons/left.png");
    QIcon LeftIcon(pixmapl.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->m_pBLeft->setIcon(LeftIcon);
    ui->m_pBLeft->setIconSize(iconSize);
    ui->m_pBLeft->setFixedSize(iconSize);

    // Right button
    QPixmap pixmapr(":/icons/right.png");
    QIcon RightIcon(pixmapr.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->m_pBRight->setIcon(RightIcon);
    ui->m_pBRight->setIconSize(iconSize);
    ui->m_pBRight->setFixedSize(iconSize);
}

void MainWindow::changedState(BluetoothClient::bluetoothleState state){

    switch(state){

    case BluetoothClient::Scanning:
    {
        statusChanged("Searching for low energy devices...");
        break;
    }
    case BluetoothClient::ScanFinished:
    {
        break;
    }

    case BluetoothClient::Connecting:
    {
        break;
    }
    case BluetoothClient::Connected:
    {
        ui->m_pBConnect->setText("Disconnect");
        connect(m_bleConnection, SIGNAL(newData(QByteArray)), this, SLOT(DataHandler(QByteArray)));

        break;
    }
    case BluetoothClient::DisConnected:
    {
        statusChanged("Device disconnected.");
        ui->m_pBConnect->setEnabled(true);
        ui->m_pBConnect->setText("Connect");
        ui->scrollAC->setValue(0);
        ui->scrollDS->setValue(0);
        ui->scrollPD->setValue(0);
        ui->scrollPI->setValue(0);
        ui->scrollPP->setValue(0);
        break;
    }
    case BluetoothClient::ServiceFound:
    {
        break;
    }
    case BluetoothClient::AcquireData:
    {
        requestData(mPP);
        requestData(mPI);
        requestData(mPD);
        requestData(mSD);
        requestData(mAC);

        break;
    }
    case BluetoothClient::Error:
    {
        ui->m_textStatus->clear();
        break;
    }
    default:
        //nothing for now
        break;
    }
}

void MainWindow::DataHandler(QByteArray data)
{
    uint8_t parsedCommand;
    uint8_t rw;
    QByteArray parsedValue;
    parseMessage(&data, parsedCommand, parsedValue, rw);
    bool ok;
    int value =  parsedValue.toHex().toInt(&ok, 16);

    if(rw == mRead)
    {

    }
    else if(rw == mWrite)
    {
        switch(parsedCommand){

        case mPP:
        {
            ui->scrollPP->setValue(value);
            break;
        }
        case mPI:
        {
            ui->scrollPI->setValue(value);
            break;
        }
        case mPD:
        {
            ui->scrollPD->setValue(value);
            break;
        }
        case mSD:
        {
            ui->scrollDS->setValue(value);
            break;
        }
        case mAC:
        {
            ui->scrollAC->setValue(value);
            break;
        }
        case mData:
        {
            ui->m_textStatus->append(parsedValue.toStdString().c_str());
            break;
        }
        default:
            //nothing for now
            break;
        }
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::initButtons(){
    /* Init Buttons */
    ui->m_pBConnect->setText("Connect");
}

void MainWindow::statusChanged(const QString &status)
{
    ui->m_textStatus->append(status);
}

void MainWindow::on_ConnectClicked()
{
    if(ui->m_pBConnect->text() == QString("Connect"))
    {
#if defined(Q_OS_ANDROID)
        // On Android, request permissions first
        requestBluetoothPermissions();
#else \
    // On desktop platforms, just start scanning directly
        statusChanged("Starting Bluetooth scan...");
        m_bleConnection->startScan();
#endif
    }
    else
    {
        ui->m_textStatus->clear();
        m_bleConnection->disconnectFromDevice();
    }
}

void MainWindow::createMessage(uint8_t msgId, uint8_t rw, QByteArray payload, QByteArray *result)
{
    uint8_t buffer[MaxPayload+8] = {'\0'};
    uint8_t command = msgId;

    int len = message.create_pack(rw , command , payload, buffer);

    for (int i = 0; i < len; i++)
    {
        result->append(static_cast<char>(buffer[i]));
    }
}

void MainWindow::parseMessage(QByteArray *data, uint8_t &command, QByteArray &value,  uint8_t &rw)
{
    MessagePack parsedMessage;

    uint8_t* dataToParse = reinterpret_cast<uint8_t*>(data->data());
    QByteArray returnValue;
    if(message.parse(dataToParse, static_cast<uint8_t>(data->length()), &parsedMessage))
    {
        command = parsedMessage.command;
        rw = parsedMessage.rw;
        for(int i = 0; i< parsedMessage.len; i++)
        {
            value.append(static_cast<char>(parsedMessage.data[i]));
        }
    }
}

void MainWindow::requestData(uint8_t command)
{
    QByteArray payload;
    QByteArray sendData;
    createMessage(command, mRead, payload, &sendData);
    m_bleConnection->writeData(sendData);
}

void MainWindow::sendCommand(uint8_t command, uint8_t value)
{
    QByteArray payload;
    payload.resize(1);

    // Assign the value to the first element of the payload
    payload[0] = static_cast<char>(value);

    // Create the message and send it
    QByteArray sendData;
    createMessage(command, mWrite, payload, &sendData);

    m_bleConnection->writeData(sendData);
}

void MainWindow::sendString(uint8_t command, QByteArray value)
{
    QByteArray sendData;
    createMessage(command, mWrite, value, &sendData);

    m_bleConnection->writeData(sendData);
}

void MainWindow::on_scrollPP_valueChanged(int value)
{
    sendCommand(mPP, static_cast<uint8_t>(value));
    ui->labelPP->setText(QString::number(value));
}

void MainWindow::on_scrollPI_valueChanged(int value)
{
    sendCommand(mPI, static_cast<uint8_t>(value));
    ui->labelPI->setText(QString::number(static_cast<double>(value/10.0), 'f', 1));
}

void MainWindow::on_scrollPD_valueChanged(int value)
{
    sendCommand(mPD, static_cast<uint8_t>(value));
    ui->labelPD->setText(QString::number(static_cast<double>(value/10.0), 'f', 1));
}

void MainWindow::on_scrollDS_valueChanged(int value)
{
    sendCommand(mSD, static_cast<uint8_t>(value));
    ui->labelDS->setText(QString::number(static_cast<double>(value/10.0), 'f', 1));
}

void MainWindow::on_scrollAC_valueChanged(int value)
{
    sendCommand(mAC, static_cast<uint8_t>(value));
    ui->labelAC->setText(QString::number(static_cast<double>(value/10.0), 'f', 1));
}


void MainWindow::on_Exit()
{
    exit(0);
}

void MainWindow::on_ForwardPressed()
{
    sendCommand(mForward, static_cast<uint8_t>(remoteConstant));
}

void MainWindow::on_ForwardReleased()
{
    sendCommand(mForward, 0);
}

void MainWindow::on_BackwardPressed()
{
    sendCommand(mBackward, static_cast<uint8_t>(remoteConstant));
}

void MainWindow::on_BackwardReleased()
{
    sendCommand(mBackward, 0);
}

void MainWindow::on_RightPressed()
{
    sendCommand(mRight, static_cast<uint8_t>(remoteConstant));
}

void MainWindow::on_RightReleased()
{
    sendCommand(mRight, 0);
}

void MainWindow::on_LeftPressed()
{
    sendCommand(mLeft, static_cast<uint8_t>(remoteConstant));
}

void MainWindow::on_LeftReleased()
{
    sendCommand(mLeft, 0);
}

void MainWindow::on_m_pBSpeak_clicked()
{
    QByteArray data;
    data.append(QString(ui->lineEdit_Speak->text()).toUtf8());
    sendString(mSpeak, data);
}

void MainWindow::on_m_pBFormat_clicked()
{
    ui->lineEdit_Speak->setText("espeak -vtr+f6");
}

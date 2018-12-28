QT       += core gui bluetooth

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = BalanceRobotRemote
TEMPLATE = app

SOURCES += main.cpp\
        mainwindow.cpp \
    deviceinfo.cpp \
    message.cpp \
    bluetoothclient.cpp

HEADERS  += mainwindow.h \
    deviceinfo.h \
    message.h \
    bluetoothclient.h

FORMS    += mainwindow.ui

RESOURCES += \
    resources.qrc


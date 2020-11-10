TEMPLATE = app
QT -= gui
QT += bluetooth
CONFIG += c++11 console
CONFIG -= app_bundle
TARGET = BalanceRobot

SOURCES += main.cpp \
    balancerobot.cpp \
    gattserver.cpp \
    i2cdev.cpp \
    message.cpp \
    mpu6050.cpp \
    pid.cpp

HEADERS += \
    balancerobot.h \
    gattserver.h \
    i2cdev.h \
    kalman.h \
    message.h \
    mpu6050.h \
    pid.h

LIBS +=  -lm -lcrypt -lasound -lwiringPi

TEMPLATE = app
TARGET = BalanceRobotPI

QT += bluetooth
CONFIG += c++11

SOURCES += main.cpp \
    gattserver.cpp \
    balancerobot.cpp \
    mpu6050.cpp \S
    pid.cpp \
    i2cdev.cpp \
    message.cpp

## Install directory
target.path = /home/pi/BalanceRobotPI
INSTALLS += target

HEADERS += \
    gattserver.h \
    balancerobot.h \
    mpu6050.h \
    pid.h \
    kalman.h \
    i2cdev.h \S
    message.h
LIBS += -L/usr/local/lib -lwiringPi
LIBS +=  -lm -lcrypt -lboost_system -lasound

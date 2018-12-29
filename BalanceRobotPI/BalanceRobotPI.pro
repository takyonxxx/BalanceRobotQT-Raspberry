TEMPLATE = app
TARGET = BalanceRobotPI

QT += bluetooth
CONFIG += c++11

SOURCES += main.cpp \
    balancerobot.cpp \
    gattserver.cpp \
    i2cdev.cpp \
    message.cpp \
    mpu6050.cpp \
    pid.cpp

## Install directory
target.path = /home/pi/BalanceRobotPI
INSTALLS += target

HEADERS += \
    balancerobot.h \
    gattserver.h \
    i2cdev.h \
    kalman.h \
    message.h \
    mpu6050.h \
    pid.h

LIBS += -L/usr/local/lib -lwiringPi
LIBS +=  -lm -lcrypt -lasound

TEMPLATE = app
QT -= gui
QT += bluetooth network

CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = BalanceRobotPI

SOURCES += main.cpp \
    balancerobot.cpp \
    gattserver.cpp \
    i2cdev.cpp \
    message.cpp \
    mpu6050.cpp \
    pid.cpp \
    pidautotuner.cpp \
    robotcontrol.cpp

HEADERS += \
    balancerobot.h \
    constants.h \
    gattserver.h \
    i2cdev.h \
    kalman.h \
    message.h \
    mpu6050.h \
    pid.h \
    pidautotuner.h \
    robotcontrol.h

QMAKE_INCDIR += /usr/local/include
QMAKE_LIBDIR += /usr/lib /usr/local/lib /usr/lib/aarch64-linux-gnu
INCLUDEPATH  += /usr/local/include

# Link order matters: WiringPi (GPIO + soft PWM) + libi2c (MPU6050 bus access).
LIBS += -lm -lcrypt -lwiringPi -li2c

# -------- Build environment notes --------
#
# Tested on Raspberry Pi OS Bookworm (64-bit) with Qt6. Build with qmake6:
#     qmake6 BalanceRobotPI.pro && make -j4
#
# Required apt packages (see README "Installation > Pi 5" for the full block):
#     sudo apt-get install qmake6 qt6-base-dev qt6-connectivity-dev
#     sudo apt-get install libqt6bluetooth6 libqt6network6
#     sudo apt-get install libi2c-dev
#
# Qt5 fallback (older Raspberry Pi OS only) — use qmake instead of qmake6:
#     sudo apt-get install qtconnectivity5-dev libqt5bluetooth5 libqt5bluetooth5-bin
#
# WiringPi is not in apt anymore - build it from the maintained fork:
#     git clone https://github.com/WiringPi/WiringPi.git
#     cd WiringPi && sudo ./build
#
# Enable I2C on the Pi:
#     sudo raspi-config       # Interface Options > I2C > Enable, then reboot

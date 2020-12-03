TEMPLATE = app
QT -= gui
QT += bluetooth network multimedia

CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = BalanceRobotPI

SOURCES += main.cpp \
    alsadevices.cpp \
    balancerobot.cpp \
    gattserver.cpp \
    i2cdev.cpp \
    message.cpp \
    mpu6050.cpp \
    pid.cpp \
    voicetranslator.cpp

HEADERS += \
    alsadevices.h \
    balancerobot.h \
    constants.h \
    gattserver.h \
    i2cdev.h \
    kalman.h \
    message.h \
    mpu6050.h \
    pid.h \
    voicetranslator.h

QMAKE_LIBDIR +=usr/lib
QMAKE_LIBDIR += /usr/local/lib
INCLUDEPATH += /usr/local/include
INCLUDEPATH += /usr/local/include/pocketsphinx
INCLUDEPATH += /usr/local/include/sphinxbase

LIBS +=  -lm -lcrypt -lasound -lwiringPi -li2c -lasound -lsphinxbase -lpocketsphinx -lsphinxad

RESOURCES += \
    resources.qrc

DISTFILES += \
    install.sh

#sudo apt-get install bison
#sudo apt-get install swig
#sudo apt-get install libasound2-dev
#sudo apt-get install alsa alsa-tools
#sudo ldconfig
#espeak voice folder under /usr/lib/arm-linux-gnueabihf/espeak-data/voices/!v


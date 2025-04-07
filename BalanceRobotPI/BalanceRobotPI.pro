TEMPLATE = app
QT -= gui
QT += bluetooth network multimedia

CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = BalanceRobotPI

SOURCES += main.cpp \
    alsarecorder.cpp \
    alsatranslator.cpp \
    balancerobot.cpp \
    gattserver.cpp \
    i2cdev.cpp \
    kalmanfilter.cpp \
    message.cpp \
    mpu6050.cpp \
    networkrequest.cpp \
    pid.cpp \
    robotcontrol.cpp \
    speaker.cpp

HEADERS += \
    alsarecorder.h \
    alsatranslator.h \
    balancerobot.h \
    constants.h \
    gattserver.h \
    i2cdev.h \
    kalman.h \
    kalmanfilter.h \
    message.h \
    mpu6050.h \
    networkrequest.h \
    pid.h \
    robotcontrol.h \
    speaker.h

QMAKE_INCDIR += /usr/local/include
QMAKE_LIBDIR += /usr/lib
QMAKE_LIBDIR += /usr/local/lib
QMAKE_LIBDIR += /usr/lib/x86_64-linux-gnu
INCLUDEPATH += /usr/local/include

LIBS +=  -lm -lcrypt -lasound -lwiringPi -li2c -lFLAC -lespeak


RESOURCES +=

DISTFILES +=

#sudo apt install espeak
#sudo apt install libespeak-dev
#sudo apt-get install libasound2-dev
#sudo apt-get install sox libsox-fmt-all
#sudo apt-get install pulseaudio alsa-tools
#sudo apt-get install qtmultimedia5-dev
#sudo apt-get install libqt5bluetooth5 libqt5bluetooth5-bin
#sudo apt-get install qtconnectivity5-dev
#sudo apt install libqt5multimedia5-plugins
#sudo apt-get install libflac-dev
#sudo apt-get update
#sudo apt-get install libi2c-dev
#git clone https://github.com/WiringPi/WiringPi.git
#cd WiringPi
#sudo ./build
#rtsp://192.168.1.8:8554/webcam

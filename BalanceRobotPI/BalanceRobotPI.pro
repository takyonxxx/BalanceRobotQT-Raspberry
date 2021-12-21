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
    speaker.h

QMAKE_INCDIR += /usr/local/include
QMAKE_LIBDIR += /usr/lib
QMAKE_LIBDIR += /usr/local/lib
QMAKE_LIBDIR += /usr/lib/x86_64-linux-gnu
INCLUDEPATH += /usr/local/include

LIBS +=  -lm -lcrypt -lasound -lwiringPi -li2c -lFLAC -lespeak


RESOURCES +=

DISTFILES +=

#sudo apt install espeak-ng
#sudo apt install libespeak-ng-dev
#sudo apt-get install libasound2-dev
#sudo apt-get install sox libsox-fmt-all
#sudo apt-get install pulseaudio alsa-tools
#sudo apt-get install qtmultimedia5-dev
#sudo apt install libqt5multimedia5-plugins
#sudo apt-get install libflac-dev
#sudo apt-get install espeak
# flac -c -d *flac | aplay
#espeak voice folder under /usr/lib/arm-linux-gnueabihf/espeak-data/voices/!v
#i did not use libespeak instead i use system command,
#for install libespeak and compile visit:
#https://walker.cs.grinnell.edu/MyroC/linux/myroc-installation-notes-linux.php?MyroC_release=3.2&MyroC_subrelease=a&eSpeak_release=2.0
#ffmpeg -f alsa -ar 48000 -ac 2 -i hw:0 testfile.flac



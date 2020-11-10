QT       += core gui bluetooth

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

win32:RC_ICONS += $$\PWD\icons\robot.png

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

android{

QT += androidextras

DISTFILES += \
    android/AndroidManifest.xml \
    android/build.gradle \
    android/gradle/wrapper/gradle-wrapper.jar \
    android/gradle/wrapper/gradle-wrapper.properties \
    android/gradlew \
    android/gradlew.bat \
    android/res/values/libs.xml

ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android
ANDROID_ABIS = armeabi-v7a
}



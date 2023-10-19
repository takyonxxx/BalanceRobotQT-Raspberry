QT += bluetooth

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

DISTFILES += \
    android/AndroidManifest.xml \
    android/build.gradle \
    android/gradle.properties \
    android/gradle/wrapper/gradle-wrapper.jar \
    android/gradle/wrapper/gradle-wrapper.properties \
    android/gradlew \
    android/gradlew.bat \
    android/res/drawable-hdpi/icon.png \
    android/res/drawable-ldpi/icon.png \
    android/res/drawable-mdpi/icon.png \
    android/res/drawable-xhdpi/icon.png \
    android/res/drawable-xxhdpi/icon.png \
    android/res/drawable-xxxhdpi/icon.png \
    android/res/values/libs.xml


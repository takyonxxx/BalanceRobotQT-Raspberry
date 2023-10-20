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


macos {
    QMAKE_INFO_PLIST = ./macos/Info.plist
    QMAKE_ASSET_CATALOGS = $$PWD/macos/Assets.xcassets
    QMAKE_ASSET_CATALOGS_APP_ICON = "AppIcon"
}

ios {
    QMAKE_INFO_PLIST = ./ios/Info.plist
    QMAKE_ASSET_CATALOGS = $$PWD/ios/Assets.xcassets
    QMAKE_ASSET_CATALOGS_APP_ICON = "AppIcon"
}

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
    android/res/values/libs.xml \
    ios/Assets.xcassets/AppIcon.appiconset/1024.png \
    ios/Assets.xcassets/AppIcon.appiconset/114.png \
    ios/Assets.xcassets/AppIcon.appiconset/120.png \
    ios/Assets.xcassets/AppIcon.appiconset/128.png \
    ios/Assets.xcassets/AppIcon.appiconset/16.png \
    ios/Assets.xcassets/AppIcon.appiconset/180.png \
    ios/Assets.xcassets/AppIcon.appiconset/256.png \
    ios/Assets.xcassets/AppIcon.appiconset/29.png \
    ios/Assets.xcassets/AppIcon.appiconset/32.png \
    ios/Assets.xcassets/AppIcon.appiconset/40.png \
    ios/Assets.xcassets/AppIcon.appiconset/512.png \
    ios/Assets.xcassets/AppIcon.appiconset/57.png \
    ios/Assets.xcassets/AppIcon.appiconset/58.png \
    ios/Assets.xcassets/AppIcon.appiconset/60.png \
    ios/Assets.xcassets/AppIcon.appiconset/64.png \
    ios/Assets.xcassets/AppIcon.appiconset/80.png \
    ios/Assets.xcassets/AppIcon.appiconset/87.png \
    ios/Assets.xcassets/AppIcon.appiconset/Contents.json \
    ios/Info.plist \
    macos/Assets.xcassets/AppIcon.appiconset/1024.png \
    macos/Assets.xcassets/AppIcon.appiconset/114.png \
    macos/Assets.xcassets/AppIcon.appiconset/120.png \
    macos/Assets.xcassets/AppIcon.appiconset/128.png \
    macos/Assets.xcassets/AppIcon.appiconset/16.png \
    macos/Assets.xcassets/AppIcon.appiconset/180.png \
    macos/Assets.xcassets/AppIcon.appiconset/256.png \
    macos/Assets.xcassets/AppIcon.appiconset/29.png \
    macos/Assets.xcassets/AppIcon.appiconset/32.png \
    macos/Assets.xcassets/AppIcon.appiconset/40.png \
    macos/Assets.xcassets/AppIcon.appiconset/512.png \
    macos/Assets.xcassets/AppIcon.appiconset/57.png \
    macos/Assets.xcassets/AppIcon.appiconset/58.png \
    macos/Assets.xcassets/AppIcon.appiconset/60.png \
    macos/Assets.xcassets/AppIcon.appiconset/64.png \
    macos/Assets.xcassets/AppIcon.appiconset/80.png \
    macos/Assets.xcassets/AppIcon.appiconset/87.png \
    macos/Assets.xcassets/AppIcon.appiconset/Contents.json \
    macos/Info.plist


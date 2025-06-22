QT += core widgets

CONFIG += c++17 windows
CONFIG -= console

SOURCES += \
    main.cpp \
    mouseemulator.cpp \
    trayapp.cpp \
    calibrador.cpp \
    guncon3.cpp \
    guncon3vjoybridge.cpp

HEADERS += \
    mouseemulator.h \
    trayapp.h \
    calibrador.h \
    guncon3.h \
    guncon3vjoybridge.h

win32 {
    INCLUDEPATH += $$PWD/external/vJoySDK/SDK/inc
    LIBS += -L$$PWD/external/vJoySDK/SDK/lib/x64 -lvjoyinterface
    LIBS += -lwinusb
    LIBS += -lsetupapi
    LIBS += -luser32
}

RESOURCES += \
    resource.qrc

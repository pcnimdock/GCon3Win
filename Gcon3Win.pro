QT += core widgets

CONFIG += c++17 windows
CONFIG -= console

SOURCES += \
    main.cpp \
    guncon3.cpp \
    guncon3manager.cpp \
    virtualmouse.cpp \
    calibrador.cpp \
    trayapp.cpp \
    driverinstaller.cpp

HEADERS += \
    guncon3.h \
    guncon3manager.h \
    virtualmouse.h \
    calibrador.h \
    trayapp.h \
    driverinstaller.h

win32 {
    LIBS += -lwinusb       # Comunicacion USB con la GunCon3
    LIBS += -lsetupapi     # Enumeracion de dispositivos (SetupDiGetClassDevs...)
    LIBS += -lcfgmgr32     # Arbol PnP (CM_Locate_DevNode, CM_Get_Parent, CM_Get_Device_ID)
    LIBS += -lnewdev       # UpdateDriverForPlugAndPlayDevices (instalacion WinUSB)
    LIBS += -lhid          # HID subsystem headers
    LIBS += -luser32       # Mensajes Win32 (SetWindowLongPtr...)
    LIBS += -lshell32      # ShellExecuteEx (UAC)
    LIBS += -ladvapi32     # OpenProcessToken, GetTokenInformation (detección admin)

    RC_ICONS = icono.ico
}

RESOURCES += \
    resource.qrc

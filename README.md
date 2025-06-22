# 🎮 GunCon 3 to vJoy Bridge for Windows

This project allows you to use a **GunCon 3 light gun** on **Windows 10/11**, mapping its input through **vJoy** as a virtual joystick.  
It uses **WinUSB** to communicate with the GunCon 3 and sends the interpreted data to **vJoy**.

---

## 📷 About

GunCon 3 is a light gun originally designed for PlayStation 3. This project enables its use on modern Windows systems for emulators or light gun-compatible games.

---

## 🧩 Features

- 🎯 GunCon 3 input reading via WinUSB  
- 🕹️ Sends data to vJoy as a virtual joystick  
- ✅ Compatible with Windows 10 and 11  
- 🛠️ Lightweight and minimal setup  
- 🖥️ System tray icon with context menu

---

## 🖱️ Tray Icon Menu

Right-clicking the tray icon displays the following options:

- **Calibration**  
- **Enable Mouse**  
- **Exit**

> ⚠️ **Note:** For now, when selecting **Calibration** and completing the process, the application will **automatically close**. This is a temporary behavior.

---

## 🛠 Requirements

Before running this software, install the following components:

### 🔌 1. GunCon 3 WinUSB Driver

Download and install the WinUSB driver for GunCon 3 from the sonik-br GunconUSB project:

[Guncon3_winusb_driver_installer.7z](https://github.com/sonik-br/GunconUSB/blob/main/drivers/Guncon3_winusb_driver_installer.7z)

> Unzip the archive and run the installer.

### 🕹️ 2. vJoy Signed Driver (Windows 10/11)

Download the signed vJoy driver from Brunner Innovation:

[vJoySetup_v2.2.2.0_Win10_Win11.exe](https://github.com/BrunnerInnovation/vJoy/releases/download/v2.2.2.0/vJoySetup_v2.2.2.0_Win10_Win11.exe)

> This version is required for proper driver signing on modern systems.

---

## 🚀 Getting Started

1. Install the GunCon 3 WinUSB driver.  
2. Install the vJoy driver.  
3. Run the application while the GunCon 3 is connected via USB.

---

## 🔧 Build Instructions

This project is written in C++ and uses Qt for the UI.

- Run `setup_vjoy.bat` to download the vJoy SDK  
- Open the project with Qt Creator  
- Select MSVC2022 64-bit kit and build

### Build Requirements

- Qt 6.9.0 MSVC2022 64-bit  
- Qt Creator

---

## 📌 Notes

- GunCon 3 must be connected via USB and properly detected by the driver.  
- vJoy must have a virtual joystick configured (use vJoyConfig after installation).  
- Only tested with one GunCon 3 so far.


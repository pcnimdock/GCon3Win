# 🎮 GunCon3Win — Sin drivers de terceros

Controlador para la **GunCon 3** (pistola de luz de PS3) en Windows 10/11.  
**No requiere vJoy, ViGEm ni ningún instalador externo.**  
Todos los drivers utilizados vienen incluidos en el propio Windows.

---

## ✅ Características

- 🎯 Lectura de la GunCon3 via **WinUSB** (incluido en Windows)
- 🕹️ **Joystick virtual HID** nativo (Virtual HID Framework, Windows 10 1709+)
- 🖱️ Emulación de **ratón** opcional (aim → cursor, trigger → clic)
- 🔧 **Instalación automática del driver WinUSB** la primera vez (solicita UAC una sola vez)
- 📐 Sistema de calibración integrado
- 🗂️ Icono en la bandeja del sistema

---

## 🗺️ Arquitectura

```
GunCon3 (USB)
    │
    ├── WinUSB (driver de Windows) ← se instala automáticamente
    │
    └── GunCon3Win.exe
            ├── guncon3.cpp       — Lee los datos crudos vía WinUSB
            ├── driverinstaller.cpp — Instala WinUSB si hace falta (UAC)
            ├── virtualjoystick.cpp — Joystick HID via Virtual HID Framework
            ├── mouseemulator.cpp  — Ratón via SendInput
            └── trayapp.cpp        — UI bandeja sistema + calibración
```

### Componentes de Windows usados (todos incluidos en el SO)

| Componente | Para qué |
|---|---|
| `WinUSB.sys` | Comunicación USB con la GunCon3 |
| `vhf.sys` / `VirtualHidDevice` | Crear el joystick virtual HID |
| `setupapi.dll` + `newdev.dll` | Instalar WinUSB la primera vez |
| `user32.dll` (SendInput) | Emulación de ratón |

---

## 🚀 Uso

1. Conecta la GunCon3 por USB.
2. Ejecuta `GunCon3Win.exe`.
3. La primera vez pedirá permiso de administrador (UAC) para instalar el driver WinUSB.  
   Las siguientes veces no lo pedirá.
4. Aparece el icono en la bandeja del sistema.

### Menú de la bandeja

| Opción | Descripción |
|---|---|
| **Calibrar GunCon3** | Calibración de 5 puntos |
| **Habilitar/Deshabilitar ratón** | Activa el modo cursor |
| **Joystick virtual activo/no disponible** | Estado informativo |
| **Salir** | Cierra la aplicación |

---

## 🔧 Compilación

### Requisitos

- Qt 6.x con MSVC 2022 64-bit
- Windows SDK 10.0.19041 o superior (incluye las cabeceras de VHF)

### Pasos

```bash
# En Qt Creator: seleccionar kit MSVC2022 x64 y compilar
# O desde línea de comandos (Developer Command Prompt):
qmake Gcon3Win.pro
nmake
```

> **Nota:** No se necesita descargar ningún SDK adicional. El Windows SDK incluido
> con Visual Studio tiene todos los headers necesarios (`winusb.h`, `hidsdi.h`,
> `newdev.h`, `cfgmgr32.h`).

---

## ⚙️ Compatibilidad del joystick virtual

El **Virtual HID Framework (VHF)** está disponible desde **Windows 10 versión 1709**
(Fall Creators Update, octubre 2017). Si estás en una versión anterior, el joystick
virtual no funcionará, pero el modo ratón sí.

El joystick virtual aparece en el sistema como un gamepad HID genérico con:
- 6 ejes: X/Y (apuntado), Z/Rz (Stick A), Rx/Ry (Stick B)
- 16 botones

---

## 📝 Notas

- Solo probado con una GunCon3.
- El proceso elevado (UAC) para instalar el driver es temporal: solo se ejecuta
  cuando el dispositivo no tiene WinUSB asignado.

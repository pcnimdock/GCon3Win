@echo off
set SDK_URL=https://github.com/BrunnerInnovation/vJoy/releases/download/v2.2.2.0/SDK.zip
set DEST=external/vJoySDK

echo Descargando vJoy SDK...
powershell -Command "Invoke-WebRequest %SDK_URL% -OutFile SDK.zip"

echo Descomprimiendo...
powershell -Command "Expand-Archive SDK.zip -DestinationPath %DEST%"

del SDK.zip
echo Listo.
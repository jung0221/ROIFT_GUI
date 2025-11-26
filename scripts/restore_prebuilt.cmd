@echo off
setlocal
if "%1"=="" (
  set ARCHIVE=prebuilt\vcpkg-prebuilt-x64-windows.zip
) else (
  set ARCHIVE=%1
)

if not exist "%ARCHIVE%" (
  echo Archive %ARCHIVE% not found.
  echo Place the prebuilt zip in the prebuilt\ folder or run scripts\create_prebuilt.cmd to produce it locally.
  exit /b 1
)

where tar >nul 2>&1
if %errorlevel%==0 (
  tar -xf %ARCHIVE%
) else (
  powershell -Command "Expand-Archive -Path '%ARCHIVE%' -DestinationPath '.' -Force"
)

echo Extraction complete.
endlocal
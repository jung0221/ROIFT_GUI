@echo off
REM Deploy Qt runtime files from vcpkg to the build Debug folder
SETLOCAL

REM Adjust these paths if your layout differs
set "ROOT=C:\src\test_vcpkg"
set "DEST=%ROOT%\build\Debug"

rem Detect whether destination is Debug or Release and pick matching vcpkg folder
set "BUILD_TYPE=Release"
set "_tmp=%DEST:\Debug=%"
if not "%_tmp%"=="%DEST%" set "BUILD_TYPE=Debug"
echo Detected build type: %BUILD_TYPE%

if "%BUILD_TYPE%"=="Debug" (
  set "SRC_BIN=%ROOT%\build\vcpkg_installed\x64-windows\debug\bin"
  set "SRC_PLUGINS=%ROOT%\build\vcpkg_installed\x64-windows\debug\Qt6\plugins"
) else (
  set "SRC_BIN=%ROOT%\build\vcpkg_installed\x64-windows\bin"
  set "SRC_PLUGINS=%ROOT%\build\vcpkg_installed\x64-windows\Qt6\plugins"
)

n
echo Source bin: %SRC_BIN%
echo Source plugins: %SRC_PLUGINS%\platforms
echo Destination: %DEST%

n\r
if not exist "%SRC_BIN%" (
  echo ERROR: source bin not found: %SRC_BIN%
  exit /b 1
)
if not exist "%SRC_PLUGINS%\platforms" (
  echo ERROR: platforms folder not found: %SRC_PLUGINS%\platforms
  exit /b 1
)
if not exist "%DEST%" (
  echo Creating destination: %DEST%
  mkdir "%DEST%"
)

echo Copying Qt core & gui & widgets DLLs from %SRC_BIN% ...
rem Copy Qt DLLs (release or debug set) - copy any Qt6* DLLs found in the selected bin folder
for %%f in (Qt6*.dll Qt6*d.dll) do (
  if exist "%SRC_BIN%\%%f" xcopy /Y /R /Q "%SRC_BIN%\%%f" "%DEST%\" >nul
)
+
echo Copying ICU, crypto and common image libs that Qt may need...
for %%f in (icudt*.dll icudtd*.dll icuuc*.dll icuucd*.dll icuin*.dll icuin*d*.dll libcrypto-*.dll libssl-*.dll libpng*.dll libpng*d*.dll libjpeg*.dll sqlite3.dll) do (
  if exist "%SRC_BIN%\%%f" xcopy /Y /R /Q "%SRC_BIN%\%%f" "%DEST%\" >nul
)

echo Copying VTK and other runtime DLLs used by the app (if any)...
for %%f in (vtk*.dll vtksys-*.dll *-9.3*.dll *-9.3d*.dll) do (
  if exist "%SRC_BIN%\%%f" xcopy /Y /R /Q "%SRC_BIN%\%%f" "%DEST%\" >nul
)

echo Copying entire platforms plugin folder...
if exist "%DEST%\platforms" (
  rmdir /S /Q "%DEST%\platforms"
)
+
rem Copy the matching platforms folder (debug plugins use names like qwindowsd.dll)
xcopy /E /I /Y /Q "%SRC_PLUGINS%\platforms" "%DEST%\platforms" >nul

echo Optionally create a qt.conf to point plugins path (overrides bundled Qt search):
set "QTCONF=%DEST%\qt.conf"
if not exist "%QTCONF%" (
  echo [Paths] > "%QTCONF%"
  echo Plugins = platforms >> "%QTCONF%"
  echo Created %QTCONF%
) else (
  echo qt.conf already exists at %QTCONF%
)

echo Deployment finished. Files copied to %DEST%\
echo Run: "%DEST%\MyQtVTKApp.exe"
ENDLOCAL

@echo off
setlocal
if "%1"=="" (
  set TRIPLET=x64-windows
) else (
  set TRIPLET=%1
)

if not exist prebuilt mkdir prebuilt
if not exist vcpkg_cache mkdir vcpkg_cache

set ZIP=prebuilt\vcpkg-prebuilt-%TRIPLET%.zip
echo Creating %ZIP%

rem Ensure there is something to package
if not exist vcpkg_installed if not exist packages (
  echo Error: neither vcpkg_installed nor packages directories exist. Nothing to package.
  echo Build your project or run vcpkg install first, then re-run this script.
  exit /b 1
)

rem Use tar if available (Windows 10+), else use PowerShell Compress-Archive
where tar >nul 2>&1
if %errorlevel%==0 (
  tar -a -c -f %ZIP% vcpkg_installed packages
  if %errorlevel% NEQ 0 (
    echo tar failed to create archive; falling back to PowerShell Compress-Archive
    powershell -Command "Compress-Archive -Path 'vcpkg_installed','packages' -DestinationPath '%ZIP%' -Force"
  )
) else (
  powershell -Command "Compress-Archive -Path 'vcpkg_installed','packages' -DestinationPath '%ZIP%' -Force"
)

if exist %ZIP% (
  echo Created %ZIP%
) else (
  echo Failed to create %ZIP%
  exit /b 1
)

echo To commit run: git add %ZIP% && git commit -m "Add prebuilt vcpkg cache for %TRIPLET%" && git push
endlocal
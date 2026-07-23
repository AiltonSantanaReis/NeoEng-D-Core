@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\windows\run-dcore.ps1" -BootstrapDependencies %*
set RC=%ERRORLEVEL%
if not "%RC%"=="0" (
  echo.
  echo NeoEng D-Core falhou com codigo %RC%. Consulte artifacts\local para os logs.
)
exit /b %RC%

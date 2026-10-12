@echo off
setlocal
powershell.exe -NoProfile -File "%~dp0upload.ps1" %*
set "upload_result=%ERRORLEVEL%"
if not "%upload_result%"=="0" pause
exit /b %upload_result%

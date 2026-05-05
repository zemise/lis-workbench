Unicode false

!ifndef APP_NAME
!define APP_NAME "Result Search"
!endif
!ifndef APP_VERSION
!define APP_VERSION "v2026.05.03"
!endif
!ifndef APP_PUBLISHER
!define APP_PUBLISHER "Zhao Wang"
!endif
!ifndef APP_EXE
!define APP_EXE "result_search.exe"
!endif
!ifndef BUILD_DIR
!define BUILD_DIR "..\build\windows-x64"
!endif
!ifndef OUTPUT_DIR
!define OUTPUT_DIR "..\out\windows\package-work"
!endif
!ifndef OUTPUT_NAME
!define OUTPUT_NAME "ResultSearch-Setup.exe"
!endif

Name "${APP_NAME}"
OutFile "${OUTPUT_DIR}\${OUTPUT_NAME}"
InstallDir "$PROGRAMFILES64\ResultSearch"
RequestExecutionLevel admin
CRCCheck on
XPStyle on
Icon "..\resource\app.ico"
UninstallIcon "..\resource\app.ico"

Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
  SetOutPath "$INSTDIR"
  File "${BUILD_DIR}\${APP_EXE}"
  File /nonfatal "${BUILD_DIR}\*.dll"
  File /nonfatal "${BUILD_DIR}\*.pdb"
  SetOutPath "$INSTDIR"
  SetOverwrite off
  IfFileExists "$INSTDIR\result_search.ini" +2 0
    FileOpen $0 "$INSTDIR\result_search.ini" w
    FileClose $0
  SetOverwrite on

  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk" "$INSTDIR\Uninstall.exe"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Allow normal users to update local ini/config files.
  ExecWait '"$SYSDIR\icacls.exe" "$INSTDIR" /grant *S-1-5-32-545:M /C'
  ExecWait '"$SYSDIR\icacls.exe" "$INSTDIR" /grant *S-1-5-32-545:(OI)(CI)M /T /C'
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"

  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd

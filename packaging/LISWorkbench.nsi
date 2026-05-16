Unicode false

!ifndef APP_NAME
!define APP_NAME "LIS Workbench"
!endif
!ifndef APP_VERSION
!define APP_VERSION "v2026.05.07"
!endif
!ifndef APP_PUBLISHER
!define APP_PUBLISHER "Zhao Wang"
!endif
!ifndef APP_EXE
!define APP_EXE "lis_workbench.exe"
!endif
!ifndef BUILD_DIR
!define BUILD_DIR "..\build\windows-x64"
!endif
!ifndef OUTPUT_DIR
!define OUTPUT_DIR "..\out\windows\package-work"
!endif
!ifndef OUTPUT_NAME
!define OUTPUT_NAME "LISWorkbench-Setup.exe"
!endif

Name "${APP_NAME}"
OutFile "${OUTPUT_DIR}\${OUTPUT_NAME}"
InstallDir "$PROGRAMFILES64\LISWorkbench"
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
  !ifdef VC_REDIST_DIR
    File /nonfatal "${VC_REDIST_DIR}\*.dll"
  !endif
  SetOutPath "$INSTDIR"
  SetOverwrite off
  IfFileExists "$INSTDIR\ClientConfig.ini" config_done 0
  IfFileExists "$INSTDIR\result_search.ini" copy_legacy_config create_config
copy_legacy_config:
    CopyFiles /SILENT "$INSTDIR\result_search.ini" "$INSTDIR\ClientConfig.ini"
    Goto config_done
create_config:
    FileOpen $0 "$INSTDIR\ClientConfig.ini" w
    FileClose $0
config_done:
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

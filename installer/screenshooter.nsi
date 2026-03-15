!include "MUI2.nsh"

Name "Screenshooter"
OutFile "screenshooter-setup.exe"
InstallDir "$PROGRAMFILES\Screenshooter"
RequestExecutionLevel admin

; UI
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath "$INSTDIR"

    ; All files are staged in DIST_DIR by CI
    File "${DIST_DIR}\xfce4-screenshooter.exe"
    File "${DIST_DIR}\ffmpeg.exe"
    File "${DIST_DIR}\ffprobe.exe"
    File "${DIST_DIR}\*.dll"

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Start menu shortcut
    CreateDirectory "$SMPROGRAMS\Screenshooter"
    CreateShortcut "$SMPROGRAMS\Screenshooter\Screenshooter.lnk" "$INSTDIR\xfce4-screenshooter.exe"
    CreateShortcut "$SMPROGRAMS\Screenshooter\Uninstall.lnk" "$INSTDIR\uninstall.exe"

    ; Registry for Add/Remove Programs
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenshooter" \
                     "DisplayName" "Screenshooter"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenshooter" \
                     "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenshooter" \
                     "InstallLocation" "$INSTDIR"
SectionEnd

Section "Uninstall"
    RMDir /r "$INSTDIR"
    RMDir /r "$SMPROGRAMS\Screenshooter"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenshooter"
SectionEnd

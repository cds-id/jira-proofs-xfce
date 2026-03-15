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

    ; Main executable
    File "${BUILD_DIR}\src\xfce4-screenshooter.exe"

    ; FFmpeg binaries (download separately, place in FFMPEG_DIR)
    File "${FFMPEG_DIR}\ffmpeg.exe"
    File "${FFMPEG_DIR}\ffprobe.exe"

    ; GLib/GdkPixbuf/libcurl DLLs from MSYS2
    File "${MINGW_BIN}\libglib-2.0-0.dll"
    File "${MINGW_BIN}\libgio-2.0-0.dll"
    File "${MINGW_BIN}\libgobject-2.0-0.dll"
    File "${MINGW_BIN}\libgmodule-2.0-0.dll"
    File "${MINGW_BIN}\libgdk_pixbuf-2.0-0.dll"
    File "${MINGW_BIN}\libcurl-4.dll"
    File "${MINGW_BIN}\libjson-glib-1.0-0.dll"
    File "${MINGW_BIN}\libintl-8.dll"
    File "${MINGW_BIN}\libiconv-2.dll"
    File "${MINGW_BIN}\libffi-8.dll"
    File "${MINGW_BIN}\libpcre2-8-0.dll"
    File "${MINGW_BIN}\zlib1.dll"
    File "${MINGW_BIN}\libpng16-16.dll"
    File "${MINGW_BIN}\libgcc_s_seh-1.dll"
    File "${MINGW_BIN}\libwinpthread-1.dll"
    File "${MINGW_BIN}\libstdc++-6.dll"
    File "${MINGW_BIN}\libssp-0.dll"

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
    ; Remove all files and subdirectories
    RMDir /r "$INSTDIR"

    ; Remove start menu shortcuts
    RMDir /r "$SMPROGRAMS\Screenshooter"

    ; Remove registry entry
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenshooter"
SectionEnd

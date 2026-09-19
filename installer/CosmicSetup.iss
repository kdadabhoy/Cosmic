; Inno Setup script for a single-app Cosmic distribution.
; Compiled by package_installer.bat, which passes the defines below via ISCC /D.
; Manual compile example:
;   ISCC /DAppName=SF_Telem /DAppVersion=0.9.0 installer\CosmicSetup.iss
;
; Expects the staged dist folder from `package.bat <AppName>` at dist\<AppName>\,
; in the ONE package layout (AP-P1): <App>.exe (renamed CosmicApp.exe), <App>.dll,
; Cosmic.dll, boot.cfg, assets\, licenses\, user\.
;
; FIXED (AP-P1): the shortcuts used to launch "CosmicApp.exe --project <AppName>".
; An explicit --project makes Runtime\Main.cpp skip the boot.cfg branch, which is the
; ONLY thing that calls FileSystem::SetAppIdentity — so the installed app fell back to
; the shared user root and, with a writable install dir, wrote its logs/recordings
; INSIDE the install folder. Shortcuts now run {app}\<App>.exe with no flag: boot.cfg
; names the app, sets the per-app identity, and user data goes to
; %LOCALAPPDATA%\<AppName>. (installer\AppSetup.iss is the same contract for any
; Starforge-packaged app; this file is the one package_installer.bat compiles.)

#ifndef AppName
  #define AppName "SF_Telem"
#endif
#ifndef AppDisplayName
  #define AppDisplayName AppName
#endif
#ifndef AppExe
  #define AppExe AppName + ".exe"
#endif
#ifndef AppVersion
  #define AppVersion "0.9.0"
#endif
#ifndef DistDir
  #define DistDir "..\dist\" + AppName
#endif

[Setup]
AppId={#AppName}.CosmicEngine
AppName={#AppDisplayName}
AppVersion={#AppVersion}
AppPublisher=Kaden Dadabhoy
DefaultDirName={autopf}\{#AppDisplayName}
DefaultGroupName={#AppDisplayName}
; Per-user install: no UAC prompt; {autopf} resolves to %LOCALAPPDATA%\Programs.
; User data (logs/recordings/exports/imgui.ini) goes to %LOCALAPPDATA%\{#AppName}
; via the engine's user:// mount and the boot.cfg identity — NEVER into {app}.
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename={#AppName}-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExe}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\{#AppDisplayName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppDisplayName}";  Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppDisplayName}"; Flags: nowait postinstall skipifsilent

; Uninstall leaves %LOCALAPPDATA%\{#AppName} alone on purpose — recordings, exports
; and logs are user data. Document this in the app README if it ever surprises anyone.

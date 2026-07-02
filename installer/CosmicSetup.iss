; Inno Setup script for a single-app Cosmic distribution.
; Compiled by package_installer.bat, which passes the defines below via ISCC /D.
; Manual compile example:
;   ISCC /DAppName=SF_Telem /DAppVersion=0.9.0 installer\CosmicSetup.iss
;
; Expects the staged dist folder from `package.bat <AppName>` at dist\<AppName>\.
; The desktop/start-menu shortcuts launch CosmicApp.exe with "--project <AppName>"
; so the app boots directly, skipping the engine Launcher.

#ifndef AppName
  #define AppName "SF_Telem"
#endif
#ifndef AppDisplayName
  #define AppDisplayName AppName
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
; User data (logs/recordings/imgui.ini) goes to %LOCALAPPDATA%\Cosmic via the
; engine's user:// mount — NOT into {app}.
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename={#AppName}-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\CosmicApp.exe
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\{#AppDisplayName}"; Filename: "{app}\CosmicApp.exe"; Parameters: "--project {#AppName}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppDisplayName}";  Filename: "{app}\CosmicApp.exe"; Parameters: "--project {#AppName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\CosmicApp.exe"; Parameters: "--project {#AppName}"; Description: "Launch {#AppDisplayName}"; Flags: nowait postinstall skipifsilent

; Uninstall leaves %LOCALAPPDATA%\Cosmic alone on purpose — recordings and logs
; are user data. Document this in the app README if it ever surprises anyone.

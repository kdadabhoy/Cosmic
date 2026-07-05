; Parameterized Inno Setup template for ANY Starforge-packaged app (Phase 16 / S5).
; Starforge's Package pipeline also WRITES a self-contained per-app script to
; dist\<AppName>.iss (no /D needed) — this file is the reference/manual-compile form.
;
; Compile:
;   ISCC /DAppName=MyRover /DAppVersion=0.9.0 installer\AppSetup.iss
;
; Expects the staged folder at dist\<AppName>\ (from Starforge ▸ File ▸ Package).
; A packaged app boots via boot.cfg, so shortcuts carry NO --project flag, and the
; engine isolates user data to %LOCALAPPDATA%\<AppName> (S6). Serves Starforge
; itself too (AppName=Starforge) — the editor is just another packaged product (S2).

#ifndef AppName
  #define AppName "MyApp"
#endif
#ifndef AppExe
  #define AppExe AppName + ".exe"
#endif
#ifndef AppVersion
  #define AppVersion "0.9.0"
#endif
#ifndef AppPublisher
  #define AppPublisher "Kaden Dadabhoy"
#endif
#ifndef DistDir
  #define DistDir "..\dist\" + AppName
#endif

[Setup]
AppId={#AppName}.CosmicApp
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
; Per-user install: no UAC. User data (logs/recordings/prefs) goes to
; %LOCALAPPDATA%\{#AppName} via the engine's user:// mount (S6) — NOT into {app}.
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
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; Optional --replay association: open .cham recordings with the app.
Root: HKCU; Subkey: "Software\Classes\.cham"; ValueType: string; ValueData: "{#AppName}.Replay"; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Classes\{#AppName}.Replay\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" --replay ""%1"""; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

; Uninstall leaves %LOCALAPPDATA%\{#AppName} alone on purpose — recordings and logs
; are user data.

; ESN installer (Inno Setup 6). Build: iscc /DAppVersion=1.0.0 installer\ESN.iss
#define AppName "ESN"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{6E1B7C08-2C7A-4B7A-9A5F-3C6E7D51A2B4}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=ESN
DefaultDirName={localappdata}\Programs\ESN
DefaultGroupName=ESN
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=ESN-{#AppVersion}-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\ESN.exe
SetupIconFile=..\res\esn.ico

[Files]
Source: "..\build\dist\ESN.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务："

[Icons]
Name: "{group}\ESN"; Filename: "{app}\ESN.exe"
Name: "{group}\卸载 ESN"; Filename: "{uninstallexe}"
Name: "{autodesktop}\ESN"; Filename: "{app}\ESN.exe"; Tasks: desktopicon

; 卸载时清掉开机启动项，避免残留指向已删除的 exe
[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "ESN"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\ESN.exe"; Description: "启动 ESN"; Flags: nowait postinstall skipifsilent

; TypeTide-Win Windows 安装器（Inno Setup）。
; 由 windows/scripts/build-release.ps1 调用：
;   ISCC.exe /DAppVersion=x.y.z TypeTide-Win.iss
; 按用户级安装（无需管理员/UAC），带开始菜单项、卸载器和可选开机自启。

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{AE1DA0AC-BC3F-4C4B-A7C7-A3D1078BA694}
AppName=TypeTide-Win
AppVersion={#AppVersion}
AppPublisher=wadaxiyang
AppPublisherURL=https://github.com/wadaxiyang/typetide-win
AppSupportURL=https://github.com/wadaxiyang/typetide-win/issues
AppUpdatesURL=https://github.com/wadaxiyang/typetide-win/releases
DefaultDirName={autopf}\TypeTide-Win
DefaultGroupName=TypeTide-Win
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=..\build
OutputBaseFilename=TypeTide-Win-Setup-{#AppVersion}
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\TypeTide-Win.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "autostart"; Description: "Start TypeTide-Win when I sign in"; GroupDescription: "Additional options:"

[Files]
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\TypeTide-Win.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\TypeTide-Win"; Filename: "{app}\TypeTide-Win.exe"
Name: "{group}\Uninstall TypeTide-Win"; Filename: "{uninstallexe}"

[Registry]
; 开机自启（用户勾选时写入；卸载时清掉）
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "TypeTide-Win"; ValueData: """{app}\TypeTide-Win.exe"""; \
    Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\TypeTide-Win.exe"; Description: "Launch TypeTide-Win now"; \
    Flags: nowait postinstall skipifsilent

[UninstallRun]
; 卸载前退出正在运行的实例（用户设置保留在 %APPDATA%\TypeTide-Win，重装不丢配置）
Filename: "{cmd}"; Parameters: "/C taskkill /IM TypeTide-Win.exe /F"; \
    Flags: runhidden skipifdoesntexist; RunOnceId: "KillTypeTide-Win"

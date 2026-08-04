#define AppName "Interfayce"
#ifndef AppVersion
#define AppVersion "1.0.0"
#endif
#define AppPublisher "Lag0Matic"

[Setup]
AppId={{C781A96B-79E7-4F86-B5BE-56D5970C53A8}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={localappdata}\Programs\Interfayce
DefaultGroupName=Interfayce
DisableProgramGroupPage=yes
OutputDir=out\installer
OutputBaseFilename=Interfayce-Setup-{#AppVersion}
SetupIconFile=..\assets\branding\interfayce.ico
UninstallDisplayIcon={app}\InterfayceOverlay.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern dynamic
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "out\stage\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Interfayce"; Filename: "{app}\InterfayceOverlay.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Interfayce"; Filename: "{app}\InterfayceOverlay.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\InterfayceOverlay.exe"; Description: "Launch Interfayce"; Flags: nowait postinstall skipifsilent unchecked

[UninstallRun]
Filename: "{app}\InterfayceOverlay.exe"; Parameters: "--shutdown"; RunOnceId: "StopInterfayce"; Flags: runhidden waituntilterminated skipifdoesntexist

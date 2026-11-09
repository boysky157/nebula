#ifndef MyVersion
  #define MyVersion "dev"
#endif

[Setup]
AppId={{B6E0B3A1-5E1D-4B6F-9B1A-2F6C0E7A91D4}
AppName=Nebula
AppVersion={#MyVersion}
AppPublisher=Nebula
AppPublisherURL=https://github.com/boysky157/nebula
DefaultDirName={autopf}\Nebula
DefaultGroupName=Nebula
UninstallDisplayName=Nebula
OutputDir=output
OutputBaseFilename=Nebula-Setup
Compression=lzma2/fast
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Nebula"; Filename: "{app}\{#ExeRel}"
Name: "{autodesktop}\Nebula"; Filename: "{app}\{#ExeRel}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#ExeRel}"; Description: "Start Nebula"; Flags: nowait postinstall skipifsilent

; OmniLauncher installer (Inno Setup 6). Built by scripts\package.ps1 from dist\OmniLauncher\:
;   iscc /DAppVersion=0.1.0 /DSourceDir=...\dist\OmniLauncher /DOutputDir=...\dist installer\OmniLauncher.iss
; Installs for the current user only (no administrator prompt) into
; %LOCALAPPDATA%\Programs\OmniLauncher, where the launcher's self-update can replace files.
; Games, settings and worlds live in %APPDATA%\OmniLauncher and are never touched.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\OmniLauncher"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

[Setup]
AppId={{6F2B8C1E-4A7D-4E59-9B3A-0D5C7E1F2A64}
AppName=OmniLauncher
AppVersion={#AppVersion}
AppVerName=OmniLauncher
AppPublisher=OmniLauncher
AppPublisherURL=https://github.com/GuardaChuva22/OmniLauncher
DefaultDirName={localappdata}\Programs\OmniLauncher
DefaultGroupName=OmniLauncher
DisableProgramGroupPage=yes
DisableDirPage=yes
DisableReadyPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=OmniLauncher-Setup
SetupIconFile=..\assets\brand\app-icon.ico
UninstallDisplayIcon={app}\OmniLauncher.exe
UninstallDisplayName=OmniLauncher
WizardStyle=modern
ShowLanguageDialog=no
Compression=lzma2/max
SolidCompression=yes
; A running launcher (or one left in the tray) is closed before files are replaced.
CloseApplications=force
RestartApplications=no

[Languages]
Name: "pt"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\OmniLauncher"; Filename: "{app}\OmniLauncher.exe"
Name: "{autodesktop}\OmniLauncher"; Filename: "{app}\OmniLauncher.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\OmniLauncher.exe"; Description: "{cm:LaunchProgram,OmniLauncher}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Files the self-update leaves next to the exe.
Type: filesandordirs; Name: "{app}"

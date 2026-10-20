#ifndef AppVersion
  #error AppVersion must be supplied by package.ps1
#endif
#define Root SourcePath + "..\"
[Setup]
AppId={{94E374C7-B5AA-4F93-8EB6-038CF031E8D8}
AppName=SideDeck
AppVersion={#AppVersion}
AppPublisher=SideDeck
DefaultDirName={localappdata}\Programs\SideDeck
DefaultGroupName=SideDeck
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableDirPage=no
UsePreviousAppDir=yes
OutputDir={#Root}releases\v{#AppVersion}
OutputBaseFilename=SideDeck-v{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\SideDeck.exe
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}.0
SetupLogging=yes

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式 / Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#Root}build\release\SideDeck.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}build\release\SideDeck.InputProbe.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}build\deps\ViGEmClient.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}build\deps\HidHide_1.5.230_x64.exe"; DestDir: "{app}\drivers"; Flags: ignoreversion
Source: "{#Root}build\deps\ViGEmBus_1.22.0_x64_x86_arm64.exe"; DestDir: "{app}\drivers"; Flags: ignoreversion
Source: "{#Root}README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}VERSION"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}build\deps\ViGEmClient-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#Root}build\deps\HidHide-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{group}\SideDeck"; Filename: "{app}\SideDeck.exe"
Name: "{group}\Recover controller access"; Filename: "{app}\SideDeck.exe"; Parameters: "--recover-input"
Name: "{group}\Uninstall SideDeck"; Filename: "{uninstallexe}"
Name: "{autodesktop}\SideDeck"; Filename: "{app}\SideDeck.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\SideDeck.exe"; Description: "启动 SideDeck / Launch SideDeck"; Flags: nowait postinstall skipifsilent

[Code]
function StopSideDeck: Boolean;
var
  Window: HWND;
  Count: Integer;
begin
  Window := FindWindowByClassName('SideDeckOverlay');
  if Window <> 0 then begin
    PostMessage(Window, $0111, 1006, 0);
    for Count := 1 to 150 do begin
      Sleep(100);
      if FindWindowByClassName('SideDeckOverlay') = 0 then break;
    end;
  end;
  Result := FindWindowByClassName('SideDeckOverlay') = 0;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  if not StopSideDeck then
    Result := 'Please exit SideDeck from its tray menu before updating.'
  else
    Result := '';
end;

function InitializeUninstall: Boolean;
begin
  Result := StopSideDeck;
  if not Result then MsgBox('Please exit SideDeck before uninstalling.', mbError, MB_OK);
end;

// Config, guides and recovery records are deliberately not in UninstallDelete.
// Shared drivers are installed only via the application's explicit setup flow,
// and are not removed by this uninstaller because other applications use them.

; Inno Setup 6 script for the installed (per-user) build.
;
; Built by packaging/package.ps1, which passes:
;   /DAppVersion=<x.y.z>   APP_VERSION  \
;   /DAppName=<name>       APP_NAME      } from cmake/AppIdentity.cmake
;   /DAppExeName=<name>    APP_EXE_NAME /
;   /DSourceDir=<dir>      the `cmake --install` output (exe + Qt + licenses)
;   /DOutputDir=<dir>      where the Setup.exe goes
;
; Per-user install (no admin): %LOCALAPPDATA%\Programs\<AppName>. The wizard
; asks for the UI language, Simple or Advanced mode, the Star Citizen folder
; and the data/cache folders, and writes them to the app's settings file
; (%APPDATA%\<AppName>\settings.ini). Uninstalling removes the program only;
; user data, settings and caches stay.
;
; The Setup.exe is named <AppExeName>-<version>-Setup.exe: the in-app update
; check picks the release asset by that pattern.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppName
  #define AppName "Smart Citizen++"
#endif
#ifndef AppExeName
  #define AppExeName "SmartCitizenPlusPlus"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\" + AppExeName
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#define AppExe AppExeName + ".exe"
; Settings live in %APPDATA%\<APP_ORG>; APP_ORG is the app name.
#define AppOrg AppName

[Setup]
AppId={{CD004C41-A57E-4035-AD06-36B2B6BA2836}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir={#OutputDir}
OutputBaseFilename={#AppExeName}-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExe}
LicenseFile={#SourceDir}\LICENSE
SetupLogging=yes
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[InstallDelete]
; A clean program folder on upgrade (user data lives elsewhere).
Type: filesandordirs; Name: "{app}\*"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
; In-app update: the app runs the installer with /SILENT /AUTOUPDATE=1 and
; quits; this relaunches it when the silent install is done.
Filename: "{app}\{#AppExe}"; Flags: nowait; Check: IsAutoUpdate

[Code]
const
  LanguageIds = 'english,chinese,french,german,italian,japanese,portuguese_br,spanish';

var
  LanguagePage: TInputOptionWizardPage;
  ModePage: TInputOptionWizardPage;
  ScDirPage: TInputDirWizardPage;
  DataDirPage: TInputDirWizardPage;

function IsAutoUpdate(): Boolean;
begin
  Result := ExpandConstant('{param:AUTOUPDATE|0}') = '1';
end;

function SettingsFile(): String;
begin
  Result := ExpandConstant('{userappdata}\{#AppOrg}\settings.ini');
end;

function ReadSetting(const Key, Default: String): String;
begin
  Result := GetIniString('General', Key, Default, SettingsFile());
end;

{ QSettings reads '\' in INI values as an escape: store forward slashes. }
function ForSettings(const Path: String): String;
begin
  Result := RemoveBackslash(Path);
  StringChangeEx(Result, '\', '/', True);
end;

function FromSettings(const Path: String): String;
begin
  Result := Path;
  StringChangeEx(Result, '/', '\', True);
end;

function DefaultDataDir(): String;
begin
  Result := ExpandConstant('{userdocs}\{#AppName}');
end;

function DefaultCacheDir(): String;
begin
  Result := ExpandConstant('{localappdata}\{#AppName}');
end;

function LanguageAt(Index: Integer): String;
var
  Rest: String;
  P, I: Integer;
begin
  Rest := LanguageIds + ',';
  for I := 0 to Index do
  begin
    P := Pos(',', Rest);
    Result := Copy(Rest, 1, P - 1);
    Rest := Copy(Rest, P + 1, MaxInt);
  end;
end;

{ OneDrive-synced folders make cache extraction slow and cleanup fail; the
  app warns too, but catching it here saves a move later. Mirrors
  core::onedrive::isOneDrivePath: under a OneDrive root from the
  environment, or any path segment named OneDrive / "OneDrive - Org". }
function IsOneDriveSegment(const Seg: String): Boolean;
var
  Low: String;
begin
  Low := Trim(LowerCase(Seg));
  Result := (Low = 'onedrive') or (Copy(Low, 1, 11) = 'onedrive - ') or (Copy(Low, 1, 9) = 'onedrive-');
end;

function UnderRoot(const Child, Root: String): Boolean;
begin
  Result := (Child <> '') and (Root <> '') and
    (Pos(AddBackslash(LowerCase(RemoveBackslash(Root))), AddBackslash(LowerCase(RemoveBackslash(Child)))) = 1);
end;

function IsOnOneDrive(const Path: String): Boolean;
var
  Rest, Seg: String;
  P: Integer;
begin
  Result := UnderRoot(Path, GetEnv('OneDrive')) or UnderRoot(Path, GetEnv('OneDriveConsumer')) or
            UnderRoot(Path, GetEnv('OneDriveCommercial'));
  Rest := Path;
  while (not Result) and (Rest <> '') do
  begin
    P := Pos('\', Rest);
    if P = 0 then
    begin
      Seg := Rest;
      Rest := '';
    end
    else
    begin
      Seg := Copy(Rest, 1, P - 1);
      Rest := Copy(Rest, P + 1, MaxInt);
    end;
    Result := IsOneDriveSegment(Seg);
  end;
end;

procedure InitializeWizard();
var
  Saved: String;
  I: Integer;
begin
  LanguagePage := CreateInputOptionPage(wpWelcome, 'Language', 'Choose the language for the app and the game text.',
    'You can change this later on the Config page.', True, False);
  LanguagePage.Add('English');
  LanguagePage.Add('Chinese');
  LanguagePage.Add('French');
  LanguagePage.Add('German');
  LanguagePage.Add('Italian');
  LanguagePage.Add('Japanese');
  LanguagePage.Add('Portuguese (Brazil)');
  LanguagePage.Add('Spanish');
  LanguagePage.SelectedValueIndex := 0;
  Saved := ReadSetting('selected_language', 'english');
  for I := 0 to 7 do
    if LanguageAt(I) = Saved then
      LanguagePage.SelectedValueIndex := I;

  ModePage := CreateInputOptionPage(LanguagePage.ID, 'Start Mode', 'How should the app open?',
    'Simple shows one button that generates the enhancements and applies them to the game. Advanced shows every page and option. You can switch at any time.',
    True, False);
  ModePage.Add('Simple: one button, sensible defaults');
  ModePage.Add('Advanced: the full interface');
  if ReadSetting('ui_mode', 'simple') = 'advanced' then
    ModePage.SelectedValueIndex := 1
  else
    ModePage.SelectedValueIndex := 0;

  ScDirPage := CreateInputDirPage(ModePage.ID, 'Star Citizen Folder', 'Where is Star Citizen installed?',
    'Choose the folder that contains LIVE, PTU and the other channels. The app finds it automatically when it is in the usual place; leave this as it is if unsure.',
    False, '');
  ScDirPage.Add('');
  ScDirPage.Values[0] := FromSettings(ReadSetting('sc_install_root', 'C:\Program Files\Roberts Space Industries\StarCitizen'));

  DataDirPage := CreateInputDirPage(ScDirPage.ID, 'Data Folders', 'Where should the app keep its files?',
    'Your edits, backups and generated files go in the data folder. The game-data cache (about 1.4 GB, rebuilt from the game whenever needed) goes in the cache folder. Avoid OneDrive-synced folders: they make extraction slow.',
    False, '');
  DataDirPage.Add('Data folder:');
  DataDirPage.Add('Cache folder:');
  DataDirPage.Values[0] := FromSettings(ReadSetting('user_data_dir', DefaultDataDir()));
  DataDirPage.Values[1] := FromSettings(ReadSetting('cache_dir', DefaultCacheDir()));
  if (ReadSetting('user_data_dir', '') = '') and IsOnOneDrive(DataDirPage.Values[0]) then
    DataDirPage.Values[0] := ExpandConstant('{%USERPROFILE}\Documents\{#AppName}');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  { A silent update keeps every saved choice. }
  Result := IsAutoUpdate() and ((PageID = LanguagePage.ID) or (PageID = ModePage.ID) or
    (PageID = ScDirPage.ID) or (PageID = DataDirPage.ID));
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  I: Integer;
begin
  Result := True;
  if CurPageID = DataDirPage.ID then
    for I := 0 to 1 do
      if IsOnOneDrive(DataDirPage.Values[I]) then
        if MsgBox(DataDirPage.Values[I] + #13#10#13#10 +
                  'This folder is synced by OneDrive, which makes extraction slow and can make cache cleanup fail. Use it anyway?',
                  mbConfirmation, MB_YESNO) = IDNO then
        begin
          Result := False;
          Exit;
        end;
end;

procedure WriteSetting(const Key, Value: String);
begin
  SetIniString('General', Key, Value, SettingsFile());
end;

procedure DeleteSetting(const Key: String);
begin
  DeleteIniEntry('General', Key, SettingsFile());
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep <> ssPostInstall) or IsAutoUpdate() then
    Exit;
  ForceDirectories(ExtractFileDir(SettingsFile()));
  WriteSetting('selected_language', LanguageAt(LanguagePage.SelectedValueIndex));
  if ModePage.SelectedValueIndex = 1 then
    WriteSetting('ui_mode', 'advanced')
  else
    WriteSetting('ui_mode', 'simple');
  if DirExists(ScDirPage.Values[0]) then
    WriteSetting('sc_install_root', ForSettings(ScDirPage.Values[0]));
  { Defaults are left unset, so the app's own default applies. }
  if CompareText(RemoveBackslash(DataDirPage.Values[0]), DefaultDataDir()) = 0 then
    DeleteSetting('user_data_dir')
  else
    WriteSetting('user_data_dir', ForSettings(DataDirPage.Values[0]));
  if CompareText(RemoveBackslash(DataDirPage.Values[1]), DefaultCacheDir()) = 0 then
    DeleteSetting('cache_dir')
  else
    WriteSetting('cache_dir', ForSettings(DataDirPage.Values[1]));
end;

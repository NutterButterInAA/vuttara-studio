#define AppName "Vuttara Studio"
#define AppVersion "0.0.1"
#define AppPublisher "Ward Reynolds"
#define AppURL "https://vuttarastudio.nuttabuttainaa.com/"
#define SourceRoot "C:\Users\wardd\Documents\NuttaProjects\VuttaraStudio-WorkspaceData\release-candidates\final-release-v1-fix2-20260801-132621\source"
#define AppRoot "C:\Users\wardd\Documents\NuttaProjects\VuttaraStudio-WorkspaceData\release-candidates\final-release-v1-fix2-20260801-132621\staging\app"
#define LegalRoot "C:\Users\wardd\Documents\NuttaProjects\VuttaraStudio-WorkspaceData\release-candidates\final-release-v1-fix2-20260801-132621\source\legal"
#define OutputRoot "C:\Users\wardd\Documents\NuttaProjects\VuttaraStudio-WorkspaceData\release-candidates\final-release-v1-fix2-20260801-132621\artifacts"

[Setup]
AppId={{BFD0130A-5D5B-4A04-8A79-BD9C05222784}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL=https://github.com/NutterButterInAA/vuttara-studio/issues
AppUpdatesURL={#AppURL}updates/clean-rewrite/latest.json
VersionInfoVersion=0.0.1.0
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=Vuttara Studio installer
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\Vuttara Studio
DefaultGroupName=Vuttara Studio
UninstallDisplayName=Vuttara Studio 0.0.1
UninstallDisplayIcon={app}\VuttaraStudio.exe
SetupIconFile={#SourceRoot}\resources\icons\VuttaraStudio.ico
LicenseFile={#LegalRoot}\TERMS-OF-SERVICE.txt
InfoBeforeFile={#LegalRoot}\INSTALLER-NOTICE.txt
InfoAfterFile={#LegalRoot}\RELEASE-NOTES-0.0.1.txt
OutputDir={#OutputRoot}
OutputBaseFilename=Vuttara-Studio-0.0.1-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
MinVersion=10.0.19041
WizardStyle=modern
DisableWelcomePage=no
DisableProgramGroupPage=yes
AllowNoIcons=yes
Compression=lzma2/max
SolidCompression=yes
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
UsePreviousAppDir=yes
UsePreviousGroup=yes
ChangesAssociations=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#LegalRoot}\PRIVACY-POLICY.txt"; Flags: dontcopy noencryption
Source: "{#AppRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#LegalRoot}\*"; DestDir: "{app}\legal"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Vuttara Studio"; Filename: "{app}\VuttaraStudio.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Vuttara Studio"; Filename: "{app}\VuttaraStudio.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\vc_redist.x64.exe"; Parameters: "/install /passive /norestart"; StatusMsg: "Installing the Microsoft Visual C++ runtime..."; Flags: waituntilterminated skipifdoesntexist; Check: ShouldRunVcRedist
Filename: "{app}\VuttaraStudio.exe"; Description: "Launch Vuttara Studio"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
var
  PrivacyPage: TWizardPage;
  PrivacyMemo: TNewMemo;
  PrivacyCheck: TNewCheckBox;

function HasCommandLineParameter(const ExpectedParameter: String): Boolean;
var
  Index: Integer;
begin
  Result := False;

  for Index := 1 to ParamCount do
  begin
    if SameText(ParamStr(Index), ExpectedParameter) then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

function IsAutomatedValidation(): Boolean;
begin
  Result := HasCommandLineParameter('/AUTOMATEDVALIDATION=1');
end;

function ShouldRunVcRedist(): Boolean;
begin
  Result := not IsAutomatedValidation();
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  if WizardSilent and (not IsAutomatedValidation()) then
  begin
    Log('Rejected silent install because /AUTOMATEDVALIDATION=1 was not supplied.');
    SuppressibleMsgBox(
      'Silent installation is reserved for controlled automated release validation. Run this installer normally to review and accept the Terms and acknowledge the Privacy Policy.',
      mbCriticalError,
      MB_OK,
      IDOK);
    Result := False;
  end;
end;

procedure InitializeWizard();
var
  PrivacyPolicyText: AnsiString;
begin
  ExtractTemporaryFile('PRIVACY-POLICY.txt');
  if not LoadStringFromFile(ExpandConstant('{tmp}\PRIVACY-POLICY.txt'), PrivacyPolicyText) then
    RaiseException('The embedded Privacy Policy could not be loaded.');

  PrivacyPage := CreateCustomPage(
    wpLicense,
    'Privacy Policy',
    'Review the Privacy Policy and acknowledge that you have read it.');

  PrivacyMemo := TNewMemo.Create(PrivacyPage);
  PrivacyMemo.Parent := PrivacyPage.Surface;
  PrivacyMemo.Left := 0;
  PrivacyMemo.Top := 0;
  PrivacyMemo.Width := PrivacyPage.SurfaceWidth;
  PrivacyMemo.Height := ScaleY(250);
  PrivacyMemo.ReadOnly := True;
  PrivacyMemo.ScrollBars := ssVertical;
  PrivacyMemo.WordWrap := True;
  PrivacyMemo.Text := PrivacyPolicyText;

  PrivacyCheck := TNewCheckBox.Create(PrivacyPage);
  PrivacyCheck.Parent := PrivacyPage.Surface;
  PrivacyCheck.Left := 0;
  PrivacyCheck.Top := PrivacyMemo.Top + PrivacyMemo.Height + ScaleY(12);
  PrivacyCheck.Width := PrivacyPage.SurfaceWidth;
  PrivacyCheck.Height := ScaleY(40);
  PrivacyCheck.Caption := 'I acknowledge that I have read the Vuttara Studio Privacy Policy.';
  PrivacyCheck.Checked := False;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if (PrivacyPage <> nil) and (CurPageID = PrivacyPage.ID) then
  begin
    if WizardSilent and IsAutomatedValidation() then
      Exit;

    if not PrivacyCheck.Checked then
    begin
      MsgBox(
        'You must acknowledge that you have read the Privacy Policy before continuing.',
        mbError,
        MB_OK);
      Result := False;
    end;
  end;
end;

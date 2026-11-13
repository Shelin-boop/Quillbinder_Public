; Quillbinder for Windows: the setup program (port, step W4d, 0.90.4, 2026-11-13). Inno Setup 6.3 or later.
;
; What the writer asked for (2026-11-11): an .exe that needs no admin rights, only unpacks the program into a
; folder the writer picks and writes nothing else: no uninstaller, no entry under "Apps", no registry key, no
; Start menu entry, no desktop icon. Those come later from the program's own guide (Help > This Computer), on
; request and each with a Remove button. The unpacked folder is the same, file for file, as the unpacked zip.
;
; Built by packaging/windows/win-setup.sh:
;   ISCC /DAppVer=0.90.4 /DAppLabel=beta /DSourceDir=<program folder> /DOutDir=<dir> /DOutName=<file name without .exe>
;        /DIconFile=<quillbinder.ico> quillbinder-setup.iss

#ifndef AppVer
  #define AppVer "0.0.0"
#endif
#ifndef AppLabel
  #define AppLabel ""
#endif
#ifndef SourceDir
  #define SourceDir "Quillbinder"
#endif
#ifndef OutDir
  #define OutDir "."
#endif
#ifndef OutName
  #define OutName "Quillbinder-setup"
#endif
#if AppLabel == ""
  #define AppShown AppVer
#else
  #define AppShown AppVer + " " + AppLabel
#endif

[Setup]
AppId=Quillbinder.Portable
AppName=Quillbinder
AppVersion={#AppShown}
AppVerName=Quillbinder {#AppShown}
AppPublisher=The Quillbinder project
VersionInfoVersion={#AppVer}.0
VersionInfoDescription=Quillbinder {#AppShown} setup (unpacks the program into a folder)
; no admin rights, never asks for them
PrivilegesRequired=lowest
; only unpacks: nothing that Windows' "Apps" list or an uninstaller would need
Uninstallable=no
CreateUninstallRegKey=no
UsePreviousAppDir=no
UsePreviousGroup=no
UsePreviousLanguage=no
UsePreviousTasks=no
UsePreviousSetupType=no
ChangesAssociations=no
ChangesEnvironment=no
CloseApplications=no
RestartApplications=no
; one page that matters: the folder. A short folder in the writer's home (long paths are a limit of Windows)
CreateAppDir=yes
DefaultDirName={%USERPROFILE}\Quillbinder
AppendDefaultDirName=yes
DisableDirPage=no
DirExistsWarning=no
DisableProgramGroupPage=yes
DisableWelcomePage=no
DisableReadyPage=yes
AllowNoIcons=yes
AllowUNCPath=no
ShowLanguageDialog=auto
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutDir}
OutputBaseFilename={#OutName}
#ifdef IconFile
SetupIconFile={#IconFile}
#endif
SetupLogging=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Messages]
english.WelcomeLabel2=This unpacks [name/ver] into a folder you choose.%n%nNothing else is changed on this computer: no admin rights, no entry under Apps, nothing in the registry. To remove Quillbinder later, delete its folder.%n%nYour settings and add-ons are kept inside that folder (in "data"). Unpacking a newer version into the same folder keeps them.
german.WelcomeLabel2=Dies entpackt [name/ver] in einen Ordner Ihrer Wahl.%n%nSonst wird auf diesem Computer nichts geändert: keine Administratorrechte, kein Eintrag unter Apps, nichts in der Registrierung. Um Quillbinder später zu entfernen, löschen Sie seinen Ordner.%n%nIhre Einstellungen und Add-ons liegen in diesem Ordner (in "data"). Wenn Sie eine neuere Version in denselben Ordner entpacken, bleiben sie erhalten.
english.SelectDirLabel3=Quillbinder will be unpacked into the following folder. Choose a short folder that you may write to, for example in your home folder. Do not use "Program Files".
german.SelectDirLabel3=Quillbinder wird in den folgenden Ordner entpackt. Wählen Sie einen kurzen Ordner, in den Sie schreiben dürfen, zum Beispiel in Ihrem Benutzerordner. Nicht "Programme".
english.FinishedLabelNoIcons=Quillbinder was unpacked. Start it with quillbinder.exe in its folder. At its first start a short guide offers a Start menu entry, a desktop icon and the file types.
german.FinishedLabelNoIcons=Quillbinder wurde entpackt. Starten Sie es mit quillbinder.exe in seinem Ordner. Beim ersten Start bietet eine kurze Anleitung einen Eintrag im Startmenü, ein Desktopsymbol und die Dateitypen an.
english.FinishedLabel=Quillbinder was unpacked. Start it with quillbinder.exe in its folder. At its first start a short guide offers a Start menu entry, a desktop icon and the file types.
german.FinishedLabel=Quillbinder wurde entpackt. Starten Sie es mit quillbinder.exe in seinem Ordner. Beim ersten Start bietet eine kurze Anleitung einen Eintrag im Startmenü, ein Desktopsymbol und die Dateitypen an.

[CustomMessages]
english.FolderNotWritable=Quillbinder cannot write into this folder:%n%n%1%n%nChoose a folder of your own, for example in your home folder. "Program Files" and other folders that need admin rights do not work: Quillbinder keeps its settings beside the program.
german.FolderNotWritable=Quillbinder kann in diesen Ordner nicht schreiben:%n%n%1%n%nWählen Sie einen eigenen Ordner, zum Beispiel in Ihrem Benutzerordner. "Programme" und andere Ordner, die Administratorrechte brauchen, gehen nicht: Quillbinder legt seine Einstellungen neben dem Programm ab.
english.FolderNotEmpty=This folder is not empty and holds no Quillbinder:%n%n%1%n%nUnpack Quillbinder's files into it all the same?
german.FolderNotEmpty=Dieser Ordner ist nicht leer und enthält kein Quillbinder:%n%n%1%n%nDie Dateien von Quillbinder trotzdem hinein entpacken?
english.StartNow=Start Quillbinder now
german.StartNow=Quillbinder jetzt starten

[Files]
; the program folder as it is in the zip; "data" (settings, add-ons, fonts of the writer) is never part of it
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "\data\*"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
Filename: "{app}\quillbinder.exe"; Description: "{cm:StartNow}"; Flags: nowait postinstall skipifsilent

[Code]
// Can this user write into the folder? The folder is made when it is not there (and taken away again when it was
// made here and stays empty), then a small file is written and removed.
function CanWriteInto(const Dir: String): Boolean;
var
  Made: Boolean;
  Probe: String;
begin
  Made := False;
  if not DirExists(Dir) then
  begin
    if not ForceDirectories(Dir) then
    begin
      Result := False;
      Exit;
    end;
    Made := True;
  end;
  Probe := AddBackslash(Dir) + 'quillbinder-setup-probe.tmp';
  Result := SaveStringToFile(Probe, 'x', False);
  if Result then
    DeleteFile(Probe);
  if Made then
    RemoveDir(Dir);
end;

// A folder with files in it that is no Quillbinder folder (the writer picked "Documents" itself, say)
function ForeignFolder(const Dir: String): Boolean;
var
  Rec: TFindRec;
begin
  Result := False;
  if not DirExists(Dir) then
    Exit;
  if FileExists(AddBackslash(Dir) + 'quillbinder.exe') then
    Exit;
  if FindFirst(AddBackslash(Dir) + '*', Rec) then
  begin
    try
      repeat
        if (Rec.Name <> '.') and (Rec.Name <> '..') then
        begin
          Result := True;
          Break;
        end;
      until not FindNext(Rec);
    finally
      FindClose(Rec);
    end;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir: String;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and not WizardSilent then
  begin
    Dir := WizardDirValue;
    if not CanWriteInto(Dir) then
    begin
      MsgBox(FmtMessage(CustomMessage('FolderNotWritable'), [Dir]), mbError, MB_OK);
      Result := False;
    end
    else if ForeignFolder(Dir) then
      Result := MsgBox(FmtMessage(CustomMessage('FolderNotEmpty'), [Dir]), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
  end;
end;

// Also without the pages (a silent run): a folder that cannot be written stops the setup before any file is unpacked
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Dir: String;
begin
  Result := '';
  Dir := ExpandConstant('{app}');
  if not CanWriteInto(Dir) then
    Result := FmtMessage(CustomMessage('FolderNotWritable'), [Dir]);
end;

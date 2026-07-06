; TURBO TUBES — Windows installer (Inno Setup 6)
; Installs the VST3 into the shared VST3 folder and the Standalone app into
; Program Files. Built in CI by .github/workflows/windows-installer.yml, which
; passes AppVersion and BuildDir on the ISCC command line.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build"
#endif

#define AppName    "Turbo Tubes"
#define Publisher  "JTEK Audio"
#define ArtefactDir BuildDir + "\TurboTubes_artefacts\Release"

[Setup]
; A fixed AppId keeps upgrades/uninstall coherent across versions.
AppId={{2F3C1A6E-9B44-4E1B-A7C2-9D5E7A0B1C23}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
DefaultDirName={autopf}\{#Publisher}\{#AppName}
DefaultGroupName={#Publisher}
DisableProgramGroupPage=yes
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\Turbo Tubes.exe
OutputDir=Output
OutputBaseFilename=TurboTubes-{#AppVersion}-win64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin

[Types]
Name: "full";   Description: "Full installation (VST3 + Standalone)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 plugin (for your DAW)"; Types: full custom
Name: "standalone"; Description: "Standalone application";     Types: full custom

[Files]
; VST3 is a bundle folder — copy it whole into the shared 64-bit VST3 location.
Source: "{#ArtefactDir}\VST3\Turbo Tubes.vst3\*"; \
    DestDir: "{commoncf64}\VST3\Turbo Tubes.vst3"; Components: vst3; \
    Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#ArtefactDir}\Standalone\Turbo Tubes.exe"; \
    DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#BuildDir}\..\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme; Components: standalone
Source: "{#BuildDir}\..\LICENSE";   DestDir: "{app}"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{group}\{#AppName}";            Filename: "{app}\Turbo Tubes.exe"; Components: standalone
Name: "{group}\Uninstall {#AppName}";  Filename: "{uninstallexe}"

[Run]
Filename: "{app}\Turbo Tubes.exe"; Description: "Launch {#AppName}"; \
    Flags: nowait postinstall skipifsilent; Components: standalone

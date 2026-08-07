#define AppName "Flying"
#ifndef AppVersion
#define AppVersion "0.0.0-dev"
#endif
#ifndef BuildId
#define BuildId "local-dev"
#endif
#ifndef SourceDir
#define SourceDir "..\artifacts\win64\local-dev\Package\Windows"
#endif
#ifndef OutputDir
#define OutputDir "..\artifacts\win64\local-dev\Installer"
#endif

[Setup]
AppId={{9B45F62A-F6B4-44DF-BF7A-E5837611F944}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Flying
DefaultDirName={autopf}\Flying
DefaultGroupName=Flying
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=Flying-{#AppVersion}-Win64-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\Flying.exe
VersionInfoVersion={#AppVersion}
VersionInfoProductName=Flying
VersionInfoProductVersion={#AppVersion}
VersionInfoDescription=Flying Win64 Shipping Installer {#BuildId}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Simulator with all Czech terrain packages"
Name: "compact"; Description: "Simulator with elevation terrain only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "sim"; Description: "Flying simulator"; Types: full compact custom; Flags: fixed
Name: "terrain\elevation"; Description: "Czech DMR 5G terrain elevation"; Types: full compact custom
Name: "terrain\gis"; Description: "Czech ortofoto, vector, water, vegetation, and obstacles"; Types: full custom
Name: "terrain\navigation"; Description: "Offline Czech navigation map"; Types: full custom

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: sim; Excludes: "Flying\Saved\Flying\PilotRegion\Terrain\*,Flying\Saved\Flying\PilotRegion\GIS\*,Flying\Saved\Flying\PilotRegion\Navigation\*"
Source: "{#SourceDir}\Flying\Saved\Flying\PilotRegion\Terrain\*"; DestDir: "{app}\Flying\Saved\Flying\PilotRegion\Terrain"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: terrain\elevation
Source: "{#SourceDir}\Flying\Saved\Flying\PilotRegion\GIS\*"; DestDir: "{app}\Flying\Saved\Flying\PilotRegion\GIS"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: terrain\gis
Source: "{#SourceDir}\Flying\Saved\Flying\PilotRegion\Navigation\*"; DestDir: "{app}\Flying\Saved\Flying\PilotRegion\Navigation"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: terrain\navigation
Source: "{#SourceDir}\FlyingReleaseManifest.json"; DestDir: "{app}"; Flags: ignoreversion; Components: sim
Source: "update-repair.ps1"; DestDir: "{app}\Packaging"; Flags: ignoreversion; Components: sim

[Icons]
Name: "{group}\Flying"; Filename: "{app}\Flying.exe"
Name: "{group}\Repair Flying Data"; Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\Packaging\update-repair.ps1"" -InstallRoot ""{app}"""

[Run]
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\Packaging\update-repair.ps1"" -InstallRoot ""{app}"""; Flags: runhidden; StatusMsg: "Verifying installed simulator and terrain packages..."

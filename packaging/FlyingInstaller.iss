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
#define RegionDataRootSubdir "Flying\Data\Regions\ceska-trebova-pilot-10km"

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
Name: "full"; Description: "Simulator with pilot regional terrain packages"
Name: "compact"; Description: "Simulator with elevation terrain only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "sim"; Description: "Flying simulator"; Types: full compact custom; Flags: fixed
Name: "terrain\elevation"; Description: "Regional DMR 5G terrain elevation"; Types: full compact custom
Name: "terrain\gis"; Description: "Regional ortofoto, vector, water, vegetation, and obstacles"; Types: full custom
Name: "terrain\navigation"; Description: "Offline regional navigation map"; Types: full custom

[Dirs]
Name: "{userappdata}\{#RegionDataRootSubdir}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: sim; Excludes: "Flying\Saved\Flying\PilotRegion\Terrain\*,Flying\Saved\Flying\PilotRegion\GIS\*,Flying\Saved\Flying\PilotRegion\Navigation\*"
Source: "{#SourceDir}\Flying\Saved\Flying\PilotRegion\Terrain\*"; DestDir: "{userappdata}\{#RegionDataRootSubdir}\Terrain"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: terrain\elevation
Source: "{#SourceDir}\Flying\Saved\Flying\PilotRegion\GIS\*"; DestDir: "{userappdata}\{#RegionDataRootSubdir}\GIS"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: terrain\gis
Source: "{#SourceDir}\Flying\Saved\Flying\PilotRegion\Navigation\*"; DestDir: "{userappdata}\{#RegionDataRootSubdir}\Navigation"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: terrain\navigation
Source: "..\Config\Regions\ceska-trebova-pilot-region.json"; DestDir: "{userappdata}\{#RegionDataRootSubdir}"; Flags: ignoreversion; Components: terrain\elevation
Source: "{#SourceDir}\FlyingReleaseManifest.json"; DestDir: "{app}"; Flags: ignoreversion; Components: sim
Source: "update-repair.ps1"; DestDir: "{app}\Packaging"; Flags: ignoreversion; Components: sim

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "FLYING_DATA_ROOT"; ValueData: "{userappdata}\{#RegionDataRootSubdir}"; Flags: preservestringtype

[Icons]
Name: "{group}\Flying"; Filename: "{app}\Flying.exe"
Name: "{group}\Repair Flying Data"; Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\Packaging\update-repair.ps1"" -InstallRoot ""{app}"" -DataRoot ""{userappdata}\{#RegionDataRootSubdir}"""

[Run]
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\Packaging\update-repair.ps1"" -InstallRoot ""{app}"" -DataRoot ""{userappdata}\{#RegionDataRootSubdir}"" -RequireOfflineLaunch"; Flags: runhidden; StatusMsg: "Verifying installed simulator and regional data packages..."

[Setup]
AppId=com.marcsoul.obs-dj-autogain
AppName=DJ AutoGain
AppVersion={#PluginVersion}
AppPublisher=MarcSoul
DefaultDirName={commonappdata}\obs-studio\plugins\obs-dj-autogain
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=obs-dj-autogain-{#PluginVersion}-windows-x64
UninstallDisplayName=DJ AutoGain

[Files]
Source: "{#PluginDll}"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion

[Messages]
SetupAppTitle=DJ AutoGain

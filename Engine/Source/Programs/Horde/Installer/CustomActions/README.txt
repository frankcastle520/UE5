
The custom action must be in a self extractig DLL file, which uses absolute paths, so like this:

Engine\Source\ThirdParty\WiX\3.8\sdk\MakeSfxCA.exe D:\InstallerCustomActions.CA.dll D:\P4\UE5-Workspace\Engine\Source\ThirdParty\WiX\3.8\sdk\x64\SfxCA.dll D:\P4\UE5-Workspace\Engine\Source\Programs\Horde\Installer\CustomActions\bin\Release\net20\InstallerCustomActions.dll D:\P4\UE5-Workspace\Engine\Source\Programs\Horde\Installer\CustomActions\bin\Release\net20\Microsoft.Deployment.WindowsInstaller.dll


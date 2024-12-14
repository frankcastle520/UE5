@echo off

set engine_path=%~dp0..\..\..\..\..\..

"%engine_path%\Binaries\ThirdParty\Python3\Win64\python.exe" %~dp0CreateMultilanguageInstaller.py "%engine_path%" %1

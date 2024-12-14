set /p MenuPath=<%~dp0P4VId.txt
IF "%MenuPath%" == "" set MenuPath="SubmitTool"
call "%~dp0P4vCustomInstaller/P4vCustomToolInstaller.exe" --toolpath "%~dp0/Windows/Engine/Binaries/Win64/SubmitTool.bat" --arguments "-server $p -user $u -client $c -root-dir \\\"$r\\\" -cl %%%%p" --menupath "%MenuPath%"
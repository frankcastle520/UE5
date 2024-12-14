:: Copyright Epic Games, Inc. All Rights Reserved.

@echo off
call:main "%~1\python"
goto:eof

::------------------------------------------------------------------------------
:main

set _pyver=3.12.4
set _pysha=15fea3c9367653a85086fe37216b4d1a1c78688fa5e1587e1db0b0f658856564
set _pytag=312
set _pymark=%_pyver%.version

:: temp folder for detritus that's rmdir'd at the end
set _tempdir=%~f1\$del

if exist "%~f1\current\%_pymark%" 1>nul 2>nul (
    rd /q /s "%_tempdir%"
    goto:eof
)

call:check_bin tar.exe _tar_path
call:check_bin curl.exe _curl_path
call:check_bin certutil.exe _certutil_path

1>nul 2>nul (
    mkdir "%_tempdir%"
)

1>nul 2>nul (
    :: as current/python.exe maybe locked, we move instead of rmdir
    move "%~f1\current" "%_tempdir%\cur_%random%"
)

set _destdir=%_tempdir%\%_pyver%
call:get_python "%_destdir%"

1>nul 2>nul (
    move "%_destdir%" "%~f1\current"
    rd /q /s "%_tempdir%"
)

goto:eof

::------------------------------------------------------------------------------
:check_bin
if exist "%SYSTEMROOT%\System32\%~1" (
    set %~2="%SYSTEMROOT%\System32\%~1"
    goto:check_bin_break
) else 2>nul (
    for /f "delims=" %%d in ('where %~1') do (
        set %~2="%SYSTEMROOT%\System32\%%~d"
        goto:check_bin_break
    )
)
set errorlevel=1
:check_bin_break
call:on_error "Win10 1803+ required - unable to find binary %~1"
goto:eof

::------------------------------------------------------------------------------
:get_python
set _workdir=%~1~
for /f "delims=, tokens=2" %%d in ('tasklist.exe /fo csv /fi "imagename eq cmd.exe" /nh') do (
    set _workdir=%~1_%%~d
)

1>nul 2>nul (
    mkdir "%_workdir%"
)

1>"%_workdir%\provision.log" 2>&1 (
    set prompt=-$s
    echo on
    call:get_python_impl "%_workdir%"
    echo off
)

move "%_workdir%" "%~1" 1>nul 2>nul
if not exist "%~1" (
    call:on_error "Failed finalising Python provision"
)

echo.
goto:eof

::------------------------------------------------------------------------------
:get_python_impl
rd /q /s "%~1" 2>nul
if exist "%~1" call:on_error "Unable to remove directory '%~1'"
md "%~1"
pushd "%~1"

call:echo "Working path; %~1"
call:echo "1/2 : Getting Python %_pyver%"
call:get_url https://www.python.org/ftp/python/%_pyver%/python-%_pyver%-embed-amd64.zip
call:integrity python-%_pyver%-embed-amd64.zip %_pysha%
call:unzip python-%_pyver%-embed-amd64.zip .

del *._pth
call:unzip python%_pytag%.zip Lib

md DLLs
move *.pyd DLLs
move lib*.dll DLLs
move sq*.dll DLLs

:: One particular AV provider began intervening with the python.exe process that
:: underpins a shell. Renaming it seems to be a successful workaround.
copy python.exe flow_python.exe

call:echo "2/2 : Adding Pip"
call:get_url https://bootstrap.pypa.io/get-pip.py
.\flow_python.exe get-pip.py
call:on_error "Failed to add Pip"
del get-pip.py

>%_pymark% echo %_pyver%
popd
goto:eof

::------------------------------------------------------------------------------
:get_url
%_curl_path% --output "%~nx1" "%~1"
call:on_error "Failed to get url '%~1'"
goto:eof

::------------------------------------------------------------------------------
:integrity
%_certutil_path% -hashfile "%~1" sha256 | findstr "%~2"
if errorlevel 1 (
    %_certutil_path% -hashfile "%~1" sha256
    set errorlevel=1
    call:on_error "Integrity check failed for '%~1'"
)
goto:eof

::------------------------------------------------------------------------------
:unzip
mkdir "%~2"
%_tar_path% -xf "%~1" -C "%~2"
call:on_error "Failed to unzip '%~1'"
del "%~1"
goto:eof

::------------------------------------------------------------------------------
:echo
1>con echo %~1
goto:eof

::------------------------------------------------------------------------------
:on_error
if not %errorlevel%==0 1>&2 (
    echo ERROR: %~1
    1>con (
        :: timeout's printing is buggy when 1>con is used :(
        echo.
        echo.
        echo.
        echo.
        echo.
        echo ERROR: %~1
        timeout /T 5
    )
    exit 1
)
goto:eof

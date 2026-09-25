@echo off
REM Quick syntax-only check of individual translation units (no link).
REM Usage: syntaxcheck.bat src\core\yaml.cpp [more.cpp ...]
setlocal EnableDelayedExpansion

set "ROOT=%~dp0"
set "MSVC=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "SDK=C:\Program Files (x86)\Windows Kits\10"

set "PATH=%MSVC%\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%MSVC%\include;%SDK%\Include\10.0.26100.0\ucrt;%SDK%\Include\10.0.26100.0\um;%SDK%\Include\10.0.26100.0\shared;%SDK%\Include\10.0.26100.0\winrt"
set "LIB=%MSVC%\lib\x64;%SDK%\Lib\10.0.26100.0\ucrt\x64;%SDK%\Lib\10.0.26100.0\um\x64"

if not exist "%ROOT%_syntax" mkdir "%ROOT%_syntax"
pushd "%ROOT%_syntax"
cl /nologo /c /W4 /permissive- /utf-8 /std:c++20 /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"%ROOT%src" %*
set "RC=%ERRORLEVEL%"
popd

echo SYNTAX_EXIT=%RC%
endlocal & exit /b %RC%
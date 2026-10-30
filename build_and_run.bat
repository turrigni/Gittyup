@echo off

call "%~dp0setup_windows.bat"
if errorlevel 1 exit /b %ERRORLEVEL%

call "%~dp0conan_cmake_windows.bat"
if errorlevel 1 exit /b %ERRORLEVEL%

call "%~dp0build_run_windows.bat"
exit /b %ERRORLEVEL%
@echo off

call "%~dp0setup_windows.bat"
if errorlevel 1 exit /b %ERRORLEVEL%

pushd "%~dp0"
if errorlevel 1 exit /b 1

set "PROJECT_DIR=%CD%"
set "BUILD_DIR=%PROJECT_DIR%\.build"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if errorlevel 1 goto :configuration_failed

echo Installing Conan dependencies...
conan install "%PROJECT_DIR%" --output-folder="%BUILD_DIR%" --build=missing -s build_type=Release
if errorlevel 1 goto :configuration_failed

echo Configuring CMake with the Conan preset...
cmake --preset conan-default
if errorlevel 1 goto :configuration_failed

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo ERROR: CMakeCache.txt was not created in "%BUILD_DIR%".
    goto :configuration_failed
)

popd
exit /b 0

:configuration_failed
echo ERROR: Conan installation or CMake configuration failed.
popd
exit /b 1

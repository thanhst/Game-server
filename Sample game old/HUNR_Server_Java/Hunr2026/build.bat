@echo off
setlocal
cd /d "%~dp0"

set "MVN_CMD=mvn"
where mvn >nul 2>&1
if errorlevel 1 (
    set "MVN_CMD=mvnw.cmd"
)

echo Using %MVN_CMD% to build project...
call %MVN_CMD% --no-transfer-progress clean package -DskipTests

if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo Build succeeded.
exit /b 0

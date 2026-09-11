@echo off
setlocal DisableDelayedExpansion
REM Explicit local settings only. Never commit database credentials or webhook tokens.
for %%V in (HUNR_DB_HOST HUNR_DB_USER HUNR_DB_NAME HUNR_DB_PASSWORD HUNR_GOLDBAR_WEBHOOK) do (
    if not defined %%V (
        echo ERROR: Environment variable %%V is required.
        exit /b 1
    )
)
set "MYSQL_EXE=mysql"
if defined HUNR_MYSQL_EXE set "MYSQL_EXE=%HUNR_MYSQL_EXE%"
set "MYSQL_PWD=%HUNR_DB_PASSWORD%"
for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd_HH:mm:ss"') do set "CURRENT_TIME=%%I"
set "RESULT_FILE=%TEMP%\hunr-gold-%RANDOM%-%RANDOM%.txt"
"%MYSQL_EXE%" --host="%HUNR_DB_HOST%" --user="%HUNR_DB_USER%" "%HUNR_DB_NAME%" -N -e "SELECT SUM(COALESCE(thoivang, 0)) as total_thoivang FROM nr_player where thoivang > 20" > "%RESULT_FILE%"
if errorlevel 1 (
    del "%RESULT_FILE%" 2>nul
    echo ERROR: Gold-bar query failed.
    exit /b 1
)
set /p THOIVANG=<"%RESULT_FILE%"
del "%RESULT_FILE%"
if "%THOIVANG%"=="NULL" set "THOIVANG=0"
if not defined THOIVANG set "THOIVANG=0"
curl --fail --silent --show-error -X POST ^
  -H "Content-Type: application/json" ^
  -d "{\"time\":\"%CURRENT_TIME%\",\"gold_bar\":%THOIVANG%}" ^
  "%HUNR_GOLDBAR_WEBHOOK%"
if errorlevel 1 exit /b 1
echo Request sent with gold_bar: %THOIVANG%
echo Time: %CURRENT_TIME%
exit /b 0

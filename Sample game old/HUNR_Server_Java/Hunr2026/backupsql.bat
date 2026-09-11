@echo off
setlocal DisableDelayedExpansion
REM Set connection details in the local environment; no production defaults.
for %%V in (HUNR_DB_HOST HUNR_DB_USER HUNR_DB_NAME HUNR_DB_PASSWORD) do (
    if not defined %%V (
        echo ERROR: Environment variable %%V is required.
        exit /b 1
    )
)
set "MYSQL_DUMP=mysqldump"
if defined HUNR_MYSQLDUMP_EXE set "MYSQL_DUMP=%HUNR_MYSQLDUMP_EXE%"
set "MYSQL_PWD=%HUNR_DB_PASSWORD%"
set "BACKUP_PATH=%~dp0backup_sql"
if not exist "%BACKUP_PATH%" mkdir "%BACKUP_PATH%"
if not exist "%BACKUP_PATH%" exit /b 1
for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "BACKUP_STAMP=%%I"
set "BACKUP_FILE=%BACKUP_PATH%\%HUNR_DB_NAME%_%BACKUP_STAMP%.sql"
echo Starting database backup...
"%MYSQL_DUMP%" --host="%HUNR_DB_HOST%" --user="%HUNR_DB_USER%" ^
--default-character-set=utf8mb4 ^
--skip-lock-tables ^
--single-transaction ^
--routines ^
--triggers ^
--events ^
--add-drop-database ^
--databases "%HUNR_DB_NAME%" > "%BACKUP_FILE%"
if errorlevel 1 (
    echo ERROR: Backup failed; inspect the partial file: "%BACKUP_FILE%"
    exit /b 1
)
echo Backup success: "%BACKUP_FILE%"
exit /b 0

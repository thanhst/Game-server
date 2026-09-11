@echo off
setlocal
chcp 65001 >nul

set "LOG_DIR=logs"
set "ERROR_LOG=%LOG_DIR%\oom_error.log"
set "JAR_FILE=target\HunrProvision-0.0.1-SNAPSHOT.jar"
set "HEAP_DUMP=%LOG_DIR%\heapdump_%date:~-4,4%%date:~-7,2%%date:~-10,2%_%time:~0,2%%time:~3,2%%time:~6,2%.hprof"
set "HEAP_DUMP=%HEAP_DUMP: =0%"

if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"

if not exist "%JAR_FILE%" (
    echo [ERROR] Cannot find "%JAR_FILE%". Please run build first.
    pause
    exit /b 1
)

set "MEMORY_OPTS=-Xms1G -Xmx24G -XX:+UseG1GC"
set "DEBUG_OPTS=-XX:+HeapDumpOnOutOfMemoryError -XX:HeapDumpPath=%HEAP_DUMP% -Xlog:gc*:file=%LOG_DIR%\gc.log:time,uptime,level,tags"

echo Starting application...
echo Logs directory: %LOG_DIR%

java -server %MEMORY_OPTS% %DEBUG_OPTS% ^
 -Dfile.encoding=UTF-8 ^
 -Dfile.client.encoding=UTF-8 ^
 -Dconsole.encoding=UTF-8 ^
 -jar "%JAR_FILE%" 2>> "%LOG_DIR%\error.log"

echo Application stopped. Check logs for details.
pause
endlocal

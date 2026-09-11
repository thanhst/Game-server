@echo off
echo Starting Java Server with logging...
java -jar target\HunrProvision-0.0.1-SNAPSHOT.jar > logs\manual_run_test.out.log 2>&1
echo Server stopped.
pause

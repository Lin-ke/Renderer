@echo off

:: 执行 xmake
xmake

:: 检查上一条命令的返回值 (%errorlevel%)
:: 如果不等于 0 (neq 0)，说明出错了，直接退出，不执行后面的命令
if %errorlevel% neq 0 (
    pause
    exit /b %errorlevel%
)

xmake run
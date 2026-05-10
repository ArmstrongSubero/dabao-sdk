@echo off
REM bao.bat - Unified build and flash tool for the Dabao SDK
REM
REM Usage:
REM   bao build <example>                           Build only
REM   bao flash <port> <example>                    Flash (normal, bootwait on)
REM   bao flash <port> <example> --persistent       Flash + auto-run on reset
REM   bao flash <port> <example> --icsp <dtr>       Zero-button via DTR reset
REM   bao flash <port> <example> --icsp-single      Single-port ICSP
REM   bao run <port> <example>                      Build + flash in one shot
REM   bao run <port> <example> --persistent         Build + flash persistent
REM   bao run <port> <example> --icsp <dtr>         Build + flash ICSP
REM   bao run <port> <example> --icsp-single        Build + flash single-port ICSP
REM   bao list                                      List available examples

setlocal enabledelayedexpansion

REM ---- Parse command ----
if "%~1"=="" goto :usage
set CMD=%~1

if /I "%CMD%"=="list" goto :cmd_list
if /I "%CMD%"=="build" goto :cmd_build
if /I "%CMD%"=="flash" goto :cmd_flash
if /I "%CMD%"=="run" goto :cmd_run
if /I "%CMD%"=="verify" goto :cmd_verify

echo Unknown command: %CMD%
echo.
goto :usage

REM ================================================================
:usage
echo bao.bat - Dabao SDK build and flash tool
echo.
echo Usage:
echo   bao build ^<example^>
echo   bao flash ^<port^> ^<example^> [--persistent^|--icsp ^<dtr^>^|--icsp-single]
echo   bao run   ^<port^> ^<example^> [--persistent^|--icsp ^<dtr^>^|--icsp-single]
echo   bao verify
echo   bao list
echo.
echo Flash modes:
echo   (default)       Normal flash, bootwait on. Press RESET to re-enter boot1.
echo   --persistent    Auto-run on reset/power cycle. Hold PROG to re-enter boot1.
echo   --icsp ^<dtr^>    Zero-button reflash. DTR port resets board, CDC port flashes.
echo   --icsp-single   Single-port ICSP. CP2104 DTR+TX/RX, no USB cable needed.
echo.
echo Examples:
echo   bao build blink
echo   bao flash COM9 blink
echo   bao run COM9 hello_uart --persistent
echo   bao run COM9 blink --icsp COM4
goto :end

REM ================================================================
:cmd_list
echo Available examples:
for /D %%d in (examples\*) do echo   %%~nd
goto :end

REM ================================================================
:cmd_build
if "%~2"=="" (
    echo Usage: bao build ^<example^>
    goto :end
)
set B_EXAMPLE=%~2
set B_RETURN=end
goto :do_build

REM ================================================================
:cmd_verify
python tools\sevs\verify.py
goto :end

REM ================================================================
:cmd_flash
if "%~3"=="" (
    echo Usage: bao flash ^<port^> ^<example^> [--persistent^|--icsp ^<dtr^>^|--icsp-single]
    goto :end
)
set F_PORT=%~2
set F_EXAMPLE=%~3
set F_OPT=%~4
set F_ARG=%~5
goto :do_flash

REM ================================================================
:cmd_run
if "%~3"=="" (
    echo Usage: bao run ^<port^> ^<example^> [--persistent^|--icsp ^<dtr^>^|--icsp-single]
    goto :end
)
set B_EXAMPLE=%~3
set F_PORT=%~2
set F_EXAMPLE=%~3
set F_OPT=%~4
set F_ARG=%~5
set B_RETURN=do_flash
goto :do_build

REM ================================================================
REM  BUILD
REM ================================================================
:do_build
set EXAMPLE_DIR=examples\%B_EXAMPLE%

if not exist "%EXAMPLE_DIR%" (
    echo Error: example "%B_EXAMPLE%" not found in examples\
    goto :end
)

REM ---- Toolchain detection ----
REM Prefer local xPack toolchain beside this script, then fall back to PATH.

set "SDK_ROOT=%~dp0"
set "LOCAL_CROSS=%SDK_ROOT%xpack-riscv-none-elf-gcc-15.2.0-1\bin\riscv-none-elf-"
set "LOCAL_GCC=%LOCAL_CROSS%gcc.exe"

if exist "%LOCAL_GCC%" (
    set "CROSS=%LOCAL_CROSS%"
    echo Using local xPack toolchain.
    echo   %LOCAL_GCC%
) else (
    where riscv-none-elf-gcc.exe >nul 2>nul
    if errorlevel 1 (
        echo ERROR: RISC-V toolchain not found.
        echo.
        echo Tried local toolchain:
        echo   %LOCAL_GCC%
        echo.
        echo Also tried PATH:
        echo   riscv-none-elf-gcc.exe
        echo.
        echo Fix:
        echo   1. Put xpack-riscv-none-elf-gcc-15.2.0-1 beside bao_flash.bat, or
        echo   2. Add riscv-none-elf-gcc.exe to PATH.
        echo.
        goto :fail
    )

    set "CROSS=riscv-none-elf-"
    echo Using RISC-V toolchain from PATH.
)

set ARCH=-march=rv32imac_zicsr_zifencei -mabi=ilp32
set CFLAGS=-Os -Wall -Wextra -ffreestanding -nostdlib -g

set INC=-Isrc\common\bao_base\include
set INC=!INC! -Isrc\common\bao_stdlib\include
set INC=!INC! -Isrc\bao1x\hardware_regs\include
set INC=!INC! -Isrc\bao1x\hardware_gpio\include
set INC=!INC! -Isrc\bao1x\hardware_uart\include
set INC=!INC! -Isrc\bao1x\hardware_pwm\include
set INC=!INC! -Isrc\bao1x\hardware_spi\include
set INC=!INC! -Isrc\bao1x\hardware_i2c\include
set INC=!INC! -Isrc\bao1x\hardware_adc\include
set INC=!INC! -Isrc\bao1x\hardware_trng\include
set INC=!INC! -Isrc\bao1x\hardware_wdt\include
set INC=!INC! -Isrc\bao1x\hardware_rtc\include
set INC=!INC! -Isrc\bao1x\hardware_bio\include
set INC=!INC! -Isrc\bao1x\hardware_timer\include
set INC=!INC! -Isrc\bao1x\hardware_bio_dma\include
set INC=!INC! -Isrc\bao1x\hardware_irq\include
set INC=!INC! -Isrc\bao1x\hardware_rram\include
set INC=!INC! -Isrc\bao1x\hardware_aes\include
set INC=!INC! -Isrc\bao1x\hardware_sha\include
set INC=!INC! -Isrc\bao1x\hardware_qspi\include
set INC=!INC! -Isrc\bao1x\hardware_w25q\include
set INC=!INC! -Isrc\bao1x\hardware_reset\include
set INC=!INC! -Isrc\boards\include
set INC=!INC! -Isrc\sevs
set INC=!INC! -Ithird_party\fatfs

echo.
echo ============================================
echo   Building: %B_EXAMPLE%
echo ============================================
echo.

if not exist build mkdir build

echo [1/4] Assembling startup ...
!CROSS!gcc !ARCH! -g -c -o build\crt0.o src\runtime\crt0.S
if errorlevel 1 goto :fail

echo [2/4] Compiling SDK ...
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\gpio.o src\bao1x\hardware_gpio\gpio.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\uart.o src\bao1x\hardware_uart\uart.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\pwm.o src\bao1x\hardware_pwm\pwm.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\spi.o src\bao1x\hardware_spi\spi.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\i2c.o src\bao1x\hardware_i2c\i2c.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\adc.o src\bao1x\hardware_adc\adc.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\trng.o src\bao1x\hardware_trng\trng.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\wdt.o src\bao1x\hardware_wdt\wdt.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\rtc.o src\bao1x\hardware_rtc\rtc.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\bio.o src\bao1x\hardware_bio\bio.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\bio_dma.o src\bao1x\hardware_bio_dma\bio_dma.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\irq.o src\bao1x\hardware_irq\irq.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\rram.o src\bao1x\hardware_rram\rram.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\aes.o src\bao1x\hardware_aes\aes.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\sha.o src\bao1x\hardware_sha\sha.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\qspi.o src\bao1x\hardware_qspi\qspi.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\w25q.o src\bao1x\hardware_w25q\w25q.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\stdio.o src\common\bao_stdlib\stdio.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\delay.o src\common\bao_stdlib\delay.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\stdlib.o src\common\bao_stdlib\stdlib.c
if errorlevel 1 goto :fail
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\sevs_assert_target.o src\sevs\sevs_assert_target.c
if errorlevel 1 goto :fail

echo [3/4] Compiling %B_EXAMPLE% ...
set MAIN_SRC=
for %%f in (%EXAMPLE_DIR%\*.c) do set MAIN_SRC=%%f
if "!MAIN_SRC!"=="" (
    echo Error: no .c file found in %EXAMPLE_DIR%
    goto :fail
)
!CROSS!gcc !ARCH! !CFLAGS! !INC! -c -o build\main.o !MAIN_SRC!
if errorlevel 1 goto :fail

REM Third-party libraries
set EXTRA_OBJS=
if "%B_EXAMPLE%"=="fatfs" (
    echo       Compiling FatFS ...
    !CROSS!gcc !ARCH! !CFLAGS! !INC! -Ithird_party\fatfs -c -o build\ff.o third_party\fatfs\ff.c
    if errorlevel 1 goto :fail
    !CROSS!gcc !ARCH! !CFLAGS! !INC! -Ithird_party\fatfs -c -o build\diskio.o third_party\fatfs\diskio.c
    if errorlevel 1 goto :fail
    set EXTRA_OBJS=build\ff.o build\diskio.o
)

echo [4/4] Linking %B_EXAMPLE%.uf2 ...
!CROSS!gcc !ARCH! -T bao1x.ld -nostdlib -nostartfiles -Wl,--gc-sections -o build\%B_EXAMPLE%.elf build\crt0.o build\gpio.o build\uart.o build\pwm.o build\spi.o build\i2c.o build\adc.o build\trng.o build\wdt.o build\rtc.o build\bio.o build\bio_dma.o build\irq.o build\rram.o build\aes.o build\sha.o build\qspi.o build\w25q.o build\stdio.o build\delay.o build\stdlib.o build\sevs_assert_target.o build\main.o !EXTRA_OBJS! -lgcc
if errorlevel 1 goto :fail

!CROSS!objcopy -O binary build\%B_EXAMPLE%.elf build\%B_EXAMPLE%.bin
if errorlevel 1 goto :fail

python tools\sign_and_uf2.py build\%B_EXAMPLE%.bin build\%B_EXAMPLE%.uf2 0x60060000 0xa7d76373
if errorlevel 1 goto :fail

echo.
!CROSS!size build\%B_EXAMPLE%.elf
echo.
echo ============================================
echo   BUILD SUCCESS: build\%B_EXAMPLE%.uf2
echo ============================================
echo.

goto :%B_RETURN%

REM ================================================================
REM  FLASH
REM ================================================================
:do_flash
set UF2=build\%F_EXAMPLE%.uf2

if not exist "%UF2%" (
    echo Error: %UF2% not found. Run "bao build %F_EXAMPLE%" first.
    goto :end
)

if "%F_OPT%"=="--persistent" goto :flash_persistent
if "%F_OPT%"=="--icsp" goto :flash_icsp
if "%F_OPT%"=="--icsp-single" goto :flash_icsp_single

echo Flashing %UF2% via %F_PORT% ...
python tools\serial_flash.py %F_PORT% %UF2%
goto :end

:flash_persistent
echo Flashing %UF2% via %F_PORT% (persistent mode) ...
python tools\serial_flash.py --persistent %F_PORT% %UF2%
goto :end

:flash_icsp
if "%F_ARG%"=="" (
    echo Error: --icsp requires a DTR port argument
    echo Usage: bao flash ^<cdc_port^> ^<example^> --icsp ^<dtr_port^>
    goto :end
)
echo Flashing %UF2% via %F_ARG% + %F_PORT% (ICSP mode) ...
python tools\serial_flash.py --icsp %F_ARG% %F_PORT% %UF2%
goto :end

:flash_icsp_single
echo Flashing %UF2% via %F_PORT% (single-port ICSP) ...
python tools\serial_flash.py --icsp-single %F_PORT% %UF2%
goto :end

REM ================================================================
:fail
echo.
echo *** FAILED ***
echo.

:end
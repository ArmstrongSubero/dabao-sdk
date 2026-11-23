@echo off
REM bao.bat - Unified build and flash tool for the Dabao SDK
REM
REM Usage:
REM   bao_flash build <example>                           Build only
REM   bao_flash flash <port> <example>                    Flash (normal, bootwait on)
REM   bao_flash flash <port> <example> --persistent       Flash + auto-run on reset
REM   bao_flash flash <port> <example> --icsp <dtr>       Zero-button via DTR reset
REM   bao_flash flash <port> <example> --icsp-single      Single-port ICSP
REM   bao_flash run <port> <example>                      Build + flash in one shot
REM   bao_flash run <port> <example> --persistent         Build + flash persistent
REM   bao_flash run <port> <example> --icsp <dtr>         Build + flash ICSP
REM   bao_flash run <port> <example> --icsp-single        Build + flash single-port ICSP
REM   bao list                                      List available examples
REM
REM Sandbox builds (MMU):
REM   set BAO_MMU=1, then build or run as above            Build with the MMU sandbox
REM   a file named bao_mmu in the example folder           Same, for that example only
REM   set BAO_MMU=                                          Back to normal builds

setlocal enabledelayedexpansion

REM ---- SDK root ----
REM Run all commands from the folder where this batch file lives.
REM This preserves the original working relative paths while allowing the
REM script to be launched from another directory.
set "SDK_ROOT=%~dp0"
pushd "%SDK_ROOT%" >nul

REM ---- Parse command ----
if "%~1"=="" goto :usage
set "CMD=%~1"

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
echo   bao_flash build ^<example^>
echo   bao_flash flash ^<port^> ^<example^> [--persistent^|--icsp ^<dtr^>^|--icsp-single]
echo   bao_flash run   ^<port^> ^<example^> [--persistent^|--icsp ^<dtr^>^|--icsp-single]
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
echo   bao_flash build blink
echo   bao_flash flash COM9 blink
echo   bao_flash run COM9 hello_uart --persistent
echo   bao_flash run COM9 blink --icsp COM4
echo.
echo MMU sandbox:
echo   set BAO_MMU=1 before build or run, or put a file named bao_mmu in the example folder.
echo   set BAO_MMU= to go back to normal builds.
goto :end

REM ================================================================
:cmd_list
echo Available examples:
if not exist "examples\" (
    echo   No examples directory found.
    goto :end
)

set "FOUND_EXAMPLE=0"
for /D %%d in (examples\*) do (
    echo   %%~nd
    set "FOUND_EXAMPLE=1"
)

if "!FOUND_EXAMPLE!"=="0" echo   No examples found.
goto :end

REM ================================================================
:cmd_build
if "%~2"=="" (
    echo Usage: bao_flash build ^<example^>
    goto :end
)
set "B_EXAMPLE=%~2"
set "B_RETURN=end"
goto :do_build

REM ================================================================
:cmd_verify
python tools\sevs\verify.py
goto :end

REM ================================================================
:cmd_flash
if "%~3"=="" (
    echo Usage: bao_flash flash ^<port^> ^<example^> [--persistent^|--icsp ^<dtr^>^|--icsp-single]
    goto :end
)
set "F_PORT=%~2"
set "F_EXAMPLE=%~3"
set "F_OPT=%~4"
set "F_ARG=%~5"
goto :do_flash

REM ================================================================
:cmd_run
if "%~3"=="" (
    echo Usage: bao_flash run ^<port^> ^<example^> [--persistent^|--icsp ^<dtr^>^|--icsp-single]
    goto :end
)
set "B_EXAMPLE=%~3"
set "F_PORT=%~2"
set "F_EXAMPLE=%~3"
set "F_OPT=%~4"
set "F_ARG=%~5"
set "B_RETURN=do_flash"
goto :do_build

REM ================================================================
REM  BUILD
REM ================================================================
:do_build
set "EXAMPLE_DIR=examples\%B_EXAMPLE%"

if not exist "%EXAMPLE_DIR%\" (
    echo Error: example "%B_EXAMPLE%" not found in examples\
    goto :end
)

REM ---- Toolchain detection ----
REM Prefer local xPack toolchain beside this script, then fall back to PATH.
REM This keeps the original working build layout and only changes how CROSS is selected.

set "CROSS="
set "LOCAL_CROSS=xpack-riscv-none-elf-gcc-15.2.0-1\bin\riscv-none-elf-"
set "LOCAL_GCC=%LOCAL_CROSS%gcc.exe"

if exist "%LOCAL_GCC%" (
    set "CROSS=%LOCAL_CROSS%"
    echo Using local xPack toolchain.
    echo   %SDK_ROOT%%LOCAL_GCC%
) else (
    REM If the archive extracted into an extra nested folder, find it.
    for /D %%d in (xpack-riscv-none-elf-gcc-*) do (
        if not defined CROSS (
            if exist "%%~fd\bin\riscv-none-elf-gcc.exe" (
                set "CROSS=%%~fd\bin\riscv-none-elf-"
                echo Using local xPack toolchain.
                echo   %%~fd\bin\riscv-none-elf-gcc.exe
            )
        )

        if not defined CROSS (
            for /D %%e in ("%%~fd\xpack-riscv-none-elf-gcc-*") do (
                if not defined CROSS (
                    if exist "%%~fe\bin\riscv-none-elf-gcc.exe" (
                        set "CROSS=%%~fe\bin\riscv-none-elf-"
                        echo Using local xPack toolchain.
                        echo   %%~fe\bin\riscv-none-elf-gcc.exe
                    )
                )
            )
        )
    )
)

if not defined CROSS (
    where riscv-none-elf-gcc.exe >nul 2>nul
    if errorlevel 1 (
        echo ERROR: RISC-V toolchain not found.
        echo.
        echo Tried local xPack:
        echo   %SDK_ROOT%%LOCAL_GCC%
        echo   %SDK_ROOT%xpack-riscv-none-elf-gcc-*\bin\riscv-none-elf-gcc.exe
        echo.
        echo Also tried PATH:
        echo   riscv-none-elf-gcc.exe
        echo.
        echo Fix:
        echo   1. Put xpack-riscv-none-elf-gcc-15.2.0-1 beside this batch file, or
        echo   2. Add riscv-none-elf-gcc.exe to PATH.
        echo.
        goto :fail
    )

    set "CROSS=riscv-none-elf-"
    echo Using RISC-V toolchain from PATH.
)

set "ARCH=-march=rv32imac_zicsr_zifencei -mabi=ilp32"
REM -fno-tree-loop-distribute-patterns: at -O2 and above GCC turns the SDK's
REM own memset/memcpy loops into calls to themselves, the stack runs through
REM SRAM, and that build killed a board. Rovari Studio passes the same flag.
set "CFLAGS=-Os -Wall -Wextra -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns -g"

set "INC=-Isrc\common\bao_base\include"
set "INC=!INC! -Isrc\common\bao_stdlib\include"
set "INC=!INC! -Isrc\bao1x\hardware_regs\include"
set "INC=!INC! -Isrc\bao1x\hardware_gpio\include"
set "INC=!INC! -Isrc\bao1x\hardware_uart\include"
set "INC=!INC! -Isrc\bao1x\hardware_pwm\include"
set "INC=!INC! -Isrc\bao1x\hardware_spi\include"
set "INC=!INC! -Isrc\bao1x\hardware_i2c\include"
set "INC=!INC! -Isrc\bao1x\hardware_adc\include"
set "INC=!INC! -Isrc\bao1x\hardware_trng\include"
set "INC=!INC! -Isrc\bao1x\hardware_wdt\include"
set "INC=!INC! -Isrc\bao1x\hardware_rtc\include"
set "INC=!INC! -Isrc\bao1x\hardware_power\include"
set "INC=!INC! -Isrc\bao1x\hardware_bio\include"
set "INC=!INC! -Isrc\bao1x\hardware_timer\include"
set "INC=!INC! -Isrc\bao1x\hardware_bio_dma\include"
set "INC=!INC! -Isrc\bao1x\hardware_irq\include"
set "INC=!INC! -Isrc\bao1x\hardware_rram\include"
set "INC=!INC! -Isrc\bao1x\hardware_aes\include"
set "INC=!INC! -Isrc\bao1x\hardware_sha\include"
set "INC=!INC! -Isrc\bao1x\hardware_pke\include"
set "INC=!INC! -Isrc\bao1x\hardware_alu\include"
set "INC=!INC! -Isrc\bao1x\hardware_sign\include"
set "INC=!INC! -Isrc\bao1x\hardware_qspi\include"
set "INC=!INC! -Isrc\bao1x\hardware_w25q\include"
set "INC=!INC! -Isrc\bao1x\hardware_reset\include"
set "INC=!INC! -Isrc\boards\include"
set "INC=!INC! -Isrc\sevs"
set "INC=!INC! -Ithird_party\fatfs"

set "MMU_BUILD=0"
if "%BAO_MMU%"=="1" set "MMU_BUILD=1"
if exist "%EXAMPLE_DIR%\bao_mmu" set "MMU_BUILD=1"
if "!MMU_BUILD!"=="1" goto :do_build_mmu

echo.
echo ============================================
echo   Building: %B_EXAMPLE%
echo ============================================
echo.

if not exist build mkdir build

echo [0/4] Checking %B_EXAMPLE% for forbidden addresses ...
python tools\audit_addresses.py "%EXAMPLE_DIR%"
if errorlevel 1 goto :fail

echo [1/4] Assembling startup ...
"!CROSS!gcc" !ARCH! -g -c -o build\crt0.o src\runtime\crt0.S
if errorlevel 1 goto :fail

echo [2/4] Compiling SDK ...
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\gpio.o src\bao1x\hardware_gpio\gpio.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\uart.o src\bao1x\hardware_uart\uart.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\pwm.o src\bao1x\hardware_pwm\pwm.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\spi.o src\bao1x\hardware_spi\spi.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\i2c.o src\bao1x\hardware_i2c\i2c.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\adc.o src\bao1x\hardware_adc\adc.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\trng.o src\bao1x\hardware_trng\trng.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\wdt.o src\bao1x\hardware_wdt\wdt.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\rtc.o src\bao1x\hardware_rtc\rtc.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\power.o src\bao1x\hardware_power\power.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\bio.o src\bao1x\hardware_bio\bio.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\bio_dma.o src\bao1x\hardware_bio_dma\bio_dma.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\irq.o src\bao1x\hardware_irq\irq.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\rram.o src\bao1x\hardware_rram\rram.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\aes.o src\bao1x\hardware_aes\aes.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\sha.o src\bao1x\hardware_sha\sha.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\sha_stream.o src\bao1x\hardware_sha\sha_stream.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\pke.o src\bao1x\hardware_pke\pke.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\pke_ecc.o src\bao1x\hardware_pke\pke_ecc.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\alu.o src\bao1x\hardware_alu\alu.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\ecdsa.o src\bao1x\hardware_sign\ecdsa.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\ed25519.o src\bao1x\hardware_sign\ed25519.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\qspi.o src\bao1x\hardware_qspi\qspi.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\w25q.o src\bao1x\hardware_w25q\w25q.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\stdio.o src\common\bao_stdlib\stdio.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\delay.o src\common\bao_stdlib\delay.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\stdlib.o src\common\bao_stdlib\stdlib.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\events.o src\common\bao_stdlib\events.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\forbidden.o src\common\bao_stdlib\forbidden.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\sevs_assert_target.o src\sevs\sevs_assert_target.c
if errorlevel 1 goto :fail

echo [3/4] Compiling %B_EXAMPLE% ...
set "MAIN_SRC="
for %%f in ("%EXAMPLE_DIR%\*.c") do (
    if exist "%%~f" (
        set "MAIN_SRC=%%~f"
        goto :main_src_found
    )
)

:main_src_found
if "!MAIN_SRC!"=="" (
    echo Error: no .c file found in %EXAMPLE_DIR%
    goto :fail
)

if "%B_EXAMPLE%"=="usb_cdc" set "INC=!INC! -I%EXAMPLE_DIR% -Ithird_party\tinyusb\src -Ithird_party\tinyusb\src\portable\corigine\udc"

"!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\main.o "!MAIN_SRC!"
if errorlevel 1 goto :fail

REM Third-party libraries
set "EXTRA_OBJS="
if "%B_EXAMPLE%"=="usb_cdc" (
    echo       Compiling TinyUSB ...
    "!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\tusb_tusb.o third_party\tinyusb\src\tusb.c
    if errorlevel 1 goto :fail
    "!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\tusb_tusb_fifo.o third_party\tinyusb\src\common\tusb_fifo.c
    if errorlevel 1 goto :fail
    "!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\tusb_usbd.o third_party\tinyusb\src\device\usbd.c
    if errorlevel 1 goto :fail
    "!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\tusb_usbd_control.o third_party\tinyusb\src\device\usbd_control.c
    if errorlevel 1 goto :fail
    "!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\tusb_cdc_device.o third_party\tinyusb\src\class\cdc\cdc_device.c
    if errorlevel 1 goto :fail
    "!CROSS!gcc" !ARCH! !CFLAGS! !INC! -c -o build\tusb_dcd_corigine_udc.o third_party\tinyusb\src\portable\corigine\udc\dcd_corigine_udc.c
    if errorlevel 1 goto :fail
    set "EXTRA_OBJS=build\tusb_tusb.o build\tusb_tusb_fifo.o build\tusb_usbd.o build\tusb_usbd_control.o build\tusb_cdc_device.o build\tusb_dcd_corigine_udc.o"
)
set "USE_FATFS="
if "%B_EXAMPLE%"=="fatfs" set "USE_FATFS=1"
findstr /C:"ff.h" /C:"diskio.h" "!MAIN_SRC!" >nul 2>&1 && set "USE_FATFS=1"
if defined USE_FATFS (
    echo       Compiling FatFS ...
    "!CROSS!gcc" !ARCH! !CFLAGS! !INC! -Ithird_party\fatfs -c -o build\ff.o third_party\fatfs\ff.c
    if errorlevel 1 goto :fail
    "!CROSS!gcc" !ARCH! !CFLAGS! !INC! -Ithird_party\fatfs -c -o build\diskio.o third_party\fatfs\diskio.c
    if errorlevel 1 goto :fail
    set "EXTRA_OBJS=build\ff.o build\diskio.o"
)

echo [4/4] Linking %B_EXAMPLE%.uf2 ...
"!CROSS!gcc" !ARCH! -T bao1x.ld -nostdlib -nostartfiles -Wl,--gc-sections -o build\%B_EXAMPLE%.elf build\crt0.o build\gpio.o build\uart.o build\pwm.o build\spi.o build\i2c.o build\adc.o build\trng.o build\wdt.o build\rtc.o build\power.o build\bio.o build\bio_dma.o build\irq.o build\rram.o build\aes.o build\sha.o build\sha_stream.o build\pke.o build\pke_ecc.o build\alu.o build\ecdsa.o build\ed25519.o build\qspi.o build\w25q.o build\stdio.o build\delay.o build\stdlib.o build\events.o build\forbidden.o build\sevs_assert_target.o build\main.o !EXTRA_OBJS! -lgcc
if errorlevel 1 goto :fail

"!CROSS!objcopy" -O binary build\%B_EXAMPLE%.elf build\%B_EXAMPLE%.bin
if errorlevel 1 goto :fail

python tools\sign_and_uf2.py build\%B_EXAMPLE%.bin build\%B_EXAMPLE%.uf2 0x60060000 0xa7d76373
if errorlevel 1 goto :fail

echo.
"!CROSS!size" build\%B_EXAMPLE%.elf
echo.
echo ============================================
echo   BUILD SUCCESS: build\%B_EXAMPLE%.uf2
echo ============================================
echo.

goto :%B_RETURN%

REM ================================================================
REM  BUILD WITH THE MMU SANDBOX
REM ================================================================
REM main runs in supervisor mode under the page table built by
REM src\mmu\bao_mmu.c. The drivers that reach the SCE, the RRAM controller,
REM the CGU, the WDT or the RTC are compiled as machine mode objects
REM (msvc_*.o, renamed by bao_mmu_mside.h) and reached through the ecall
REM stubs in bao_mmu_stubs.S. Same steps as do_build_mmu in bao_flash.sh.
:do_build_mmu
set "MCFLAGS=!CFLAGS! -DBAO_MMU=1"
set "MINC=!INC! -Isrc\mmu"
set "MSIDE=-include src\mmu\bao_mmu_mside.h"

echo.
echo ============================================
echo   Building: %B_EXAMPLE% (BAO_MMU sandbox)
echo ============================================
echo.

if not exist build mkdir build

echo [0/4] Checking %B_EXAMPLE% for forbidden addresses ...
python tools\audit_addresses.py "%EXAMPLE_DIR%"
if errorlevel 1 goto :fail

echo [1/4] Assembling startup and service stubs ...
"!CROSS!gcc" !ARCH! -g -DBAO_MMU=1 -c -o build/mmu_crt0.o src\runtime\crt0_mmu.S
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! -g -DBAO_MMU=1 -Isrc\mmu -c -o build/mmu_stubs.o src\mmu\bao_mmu_stubs.S
if errorlevel 1 goto :fail

echo [2/4] Compiling SDK (machine mode drivers as msvc_*.o) ...
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_gpio.o src\bao1x\hardware_gpio\gpio.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_uart.o src\bao1x\hardware_uart\uart.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_pwm.o src\bao1x\hardware_pwm\pwm.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_spi.o src\bao1x\hardware_spi\spi.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_i2c.o src\bao1x\hardware_i2c\i2c.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_adc.o src\bao1x\hardware_adc\adc.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_trng.o src\bao1x\hardware_trng\trng.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_wdt.o src\bao1x\hardware_wdt\wdt.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_rtc.o src\bao1x\hardware_rtc\rtc.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_power.o src\bao1x\hardware_power\power.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_bio.o src\bao1x\hardware_bio\bio.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_bio_dma.o src\bao1x\hardware_bio_dma\bio_dma.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_irq.o src\bao1x\hardware_irq\irq.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_rram.o src\bao1x\hardware_rram\rram.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_aes.o src\bao1x\hardware_aes\aes.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_sha.o src\bao1x\hardware_sha\sha.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_sha_stream.o src\bao1x\hardware_sha\sha_stream.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_pke.o src\bao1x\hardware_pke\pke.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_pke_ecc.o src\bao1x\hardware_pke\pke_ecc.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_alu.o src\bao1x\hardware_alu\alu.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_ecdsa.o src\bao1x\hardware_sign\ecdsa.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_ed25519.o src\bao1x\hardware_sign\ed25519.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_qspi.o src\bao1x\hardware_qspi\qspi.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_w25q.o src\bao1x\hardware_w25q\w25q.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_stdio.o src\common\bao_stdlib\stdio.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_delay.o src\common\bao_stdlib\delay.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_stdlib.o src\common\bao_stdlib\stdlib.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_events.o src\common\bao_stdlib\events.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_forbidden.o src\common\bao_stdlib\forbidden.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_s_sevs_assert_target.o src\sevs\sevs_assert_target.c
if errorlevel 1 goto :fail
"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! !MSIDE! -c -o build/msvc_bao_mmu.o src\mmu\bao_mmu.c
if errorlevel 1 goto :fail

set "MMU_OBJS=build/mmu_s_gpio.o build/mmu_s_uart.o build/mmu_s_pwm.o build/mmu_s_spi.o build/mmu_s_i2c.o build/mmu_s_adc.o build/msvc_trng.o build/msvc_wdt.o build/msvc_rtc.o build/msvc_power.o build/mmu_s_bio.o build/mmu_s_bio_dma.o build/mmu_s_irq.o build/msvc_rram.o build/msvc_aes.o build/msvc_sha.o build/msvc_sha_stream.o build/msvc_pke.o build/msvc_pke_ecc.o build/msvc_alu.o build/msvc_ecdsa.o build/msvc_ed25519.o build/mmu_s_qspi.o build/mmu_s_w25q.o build/mmu_s_stdio.o build/mmu_s_delay.o build/mmu_s_stdlib.o build/mmu_s_events.o build/mmu_s_forbidden.o build/mmu_s_sevs_assert_target.o build/msvc_bao_mmu.o"

echo [3/4] Compiling %B_EXAMPLE% ...
set "MAIN_SRC="
for %%f in ("%EXAMPLE_DIR%\*.c") do (
    if exist "%%~f" (
        set "MAIN_SRC=%%~f"
        goto :mmu_main_src_found
    )
)

:mmu_main_src_found
if "!MAIN_SRC!"=="" (
    echo Error: no .c file found in %EXAMPLE_DIR%
    goto :fail
)

"!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -c -o build/mmu_main.o "!MAIN_SRC!"
if errorlevel 1 goto :fail

set "EXTRA_OBJS="
set "USE_FATFS="
if "%B_EXAMPLE%"=="fatfs" set "USE_FATFS=1"
findstr /C:"ff.h" /C:"diskio.h" "!MAIN_SRC!" >nul 2>&1 && set "USE_FATFS=1"
if defined USE_FATFS (
    echo       Compiling FatFS ...
    "!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -Ithird_party\fatfs -c -o build/mmu_ff.o third_party\fatfs\ff.c
    if errorlevel 1 goto :fail
    "!CROSS!gcc" !ARCH! !MCFLAGS! !MINC! -Ithird_party\fatfs -c -o build/mmu_diskio.o third_party\fatfs\diskio.c
    if errorlevel 1 goto :fail
    set "EXTRA_OBJS=build/mmu_ff.o build/mmu_diskio.o"
)

echo [4/4] Linking %B_EXAMPLE%.uf2 ...
"!CROSS!gcc" !ARCH! -T bao1x_mmu.ld -nostdlib -nostartfiles -Wl,--gc-sections -o build\%B_EXAMPLE%.elf build/mmu_crt0.o build/mmu_stubs.o !MMU_OBJS! build/mmu_main.o !EXTRA_OBJS! -lgcc
if errorlevel 1 goto :fail

"!CROSS!objcopy" -O binary build\%B_EXAMPLE%.elf build\%B_EXAMPLE%.bin
if errorlevel 1 goto :fail

python tools\sign_and_uf2.py build\%B_EXAMPLE%.bin build\%B_EXAMPLE%.uf2 0x60060000 0xa7d76373
if errorlevel 1 goto :fail

echo.
"!CROSS!size" build\%B_EXAMPLE%.elf
echo.
echo ============================================
echo   BUILD SUCCESS: build\%B_EXAMPLE%.uf2 (BAO_MMU sandbox)
echo ============================================
echo.

goto :%B_RETURN%

REM ================================================================
REM  FLASH
REM ================================================================
:do_flash
set "UF2=build\%F_EXAMPLE%.uf2"

if not exist "%UF2%" (
    echo Error: %UF2% not found. Run "bao_flash build %F_EXAMPLE%" first.
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
    echo Usage: bao_flash flash ^<cdc_port^> ^<example^> --icsp ^<dtr_port^>
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
popd >nul
endlocal

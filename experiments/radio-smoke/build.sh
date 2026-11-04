#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p logs/radio-smoke
for mode in 0 1; do
 arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -Os -g -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -fstack-usage -DTEST_TX="$mode" -Iexperiments/radio-smoke -Ifirmware/include -Ivendor/cx32l003-dfp/Device/Include -Ivendor/CMSIS_5/CMSIS/Core/Include -Ivendor/sx126x_driver/src -nostdlib -Wl,--gc-sections,--build-id=none,-Texperiments/radio-smoke/ram.ld,-Map=logs/radio-smoke/test-$mode.map experiments/radio-smoke/start.S experiments/radio-smoke/test.c experiments/radio-smoke/platform.c firmware/src/memory.c vendor/sx126x_driver/src/sx126x.c -lgcc -o "logs/radio-smoke/test-$mode.elf"
 arm-none-eabi-objcopy -O binary "logs/radio-smoke/test-$mode.elf" "logs/radio-smoke/test-$mode.bin"
 arm-none-eabi-size "logs/radio-smoke/test-$mode.elf"
done

#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p logs/radio-smoke
for mode in 0 1; do
 cc -std=c11 -O1 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
   -Wl,--gc-sections -fsanitize=address,undefined -DTEST_TX="$mode" \
   -Iexperiments/radio-smoke -Ivendor/sx126x_driver/src \
   experiments/radio-smoke/test.c experiments/radio-smoke/host_test.c \
   vendor/sx126x_driver/src/sx126x.c -o "logs/radio-smoke/host-$mode"
 "logs/radio-smoke/host-$mode"
done

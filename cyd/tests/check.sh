#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p logs/cyd
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -DUSE_DXLR_UART -DDXLR_RX_PIN=22 -DDXLR_TX_PIN=27 \
 -Icyd/tests/stubs -Icyd/radio cyd/radio/DxlrUartInterface.cpp cyd/tests/test_adapter.cpp \
 -o logs/cyd/test-adapter
logs/cyd/test-adapter

c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -Icyd/ui cyd/tests/test_touch.cpp -o logs/cyd/test-touch
logs/cyd/test-touch

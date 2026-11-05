#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p logs/cyd/upstream-queue
# Compile the pinned upstream queue unchanged against the same core stubs.
cp .local/meshtastic-cyd/src/mesh/MeshPacketQueue.h .local/meshtastic-cyd/src/mesh/MeshPacketQueue.cpp logs/cyd/upstream-queue/
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -DUSE_DXLR_UART -DDXLR_RX_PIN=22 -DDXLR_TX_PIN=27 \
 -Icyd/tests/stubs -Ilogs/cyd/upstream-queue -Icyd/radio cyd/radio/DxlrUartInterface.cpp logs/cyd/upstream-queue/MeshPacketQueue.cpp cyd/tests/test_adapter.cpp \
 -o logs/cyd/test-adapter
logs/cyd/test-adapter

c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -Icyd/ui cyd/tests/test_touch.cpp -o logs/cyd/test-touch
logs/cyd/test-touch

c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -Icyd/ui cyd/tests/test_ui.cpp -o logs/cyd/test-ui
logs/cyd/test-ui

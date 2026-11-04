CROSS_COMPILE ?= arm-none-eabi-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE := $(CROSS_COMPILE)size
VENDOR := ../vendor
INCLUDES := -Iinclude -I$(VENDOR)/cx32l003-dfp/Device/Include -I$(VENDOR)/CMSIS_5/CMSIS/Core/Include -I$(VENDOR)/sx126x_driver/src
ARCH := -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
CFLAGS := $(ARCH) $(INCLUDES) -std=c11 -Os -g3 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-common -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -fstack-usage -MMD -MP
LDFLAGS := $(ARCH) -nostdlib -Tcx32l003f8.ld -Wl,-z,noexecstack,--gc-sections,--fatal-warnings,-Map=build/controller.map,--print-memory-usage
OBJECTS := build/startup.o build/main.o build/board.o build/sx126x_hal.o build/memory.o build/system_cx32l003.o build/sx126x.o

.PHONY: all check
all: build/controller.elf build/controller.bin build/controller.hex

build:
	mkdir -p $@
build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@
build/startup.o: src/startup.S | build
	$(CC) $(ARCH) -g3 -c $< -o $@
build/system_cx32l003.o: $(VENDOR)/cx32l003-dfp/Device/Source/system_cx32l003.c | build
	$(CC) $(CFLAGS) -c $< -o $@
build/sx126x.o: $(VENDOR)/sx126x_driver/src/sx126x.c | build
	$(CC) $(CFLAGS) -c $< -o $@
build/controller.elf: $(OBJECTS) cx32l003f8.ld
	$(CC) $(LDFLAGS) $(OBJECTS) -lgcc -o $@
	$(SIZE) $@
build/controller.bin: build/controller.elf
	$(OBJCOPY) -O binary $< $@
build/controller.hex: build/controller.elf
	$(OBJCOPY) -O ihex $< $@
build/driver-check.elf: $(OBJECTS) cx32l003f8.ld
	$(CC) $(LDFLAGS) -Wl,--no-gc-sections,-Map=build/driver-check.map $(OBJECTS) -lgcc -o $@
	$(SIZE) $@
check: all build/driver-check.elf
	python3 tools/verify_sources.py
	CROSS_COMPILE="$(CROSS_COMPILE)" python3 tests/check_image.py
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude -I$(VENDOR)/sx126x_driver/src tests/test_hal.c src/sx126x_hal.c -o build/test_hal
	./build/test_hal

HOSTCC ?= cc
-include $(OBJECTS:.o=.d)

# Separate first-hardware image: reuses vectors/linker, excludes all radio code.
UART_OBJECTS := build/uartdiag/startup.o build/uartdiag/uart_system.o build/uartdiag/uart_diag.o build/uartdiag/diagnostic_protocol.o build/uartdiag/memory.o
.PHONY: uartdiag uart-check
build/uartdiag:
	mkdir -p $@
build/uartdiag/%.o: src/%.c | build/uartdiag
	$(CC) $(CFLAGS) -c $< -o $@
build/uartdiag/startup.o: src/startup.S | build/uartdiag
	$(CC) $(ARCH) -g3 -c $< -o $@
build/uartdiag/controller.elf: $(UART_OBJECTS) cx32l003f8.ld
	$(CC) $(LDFLAGS) -Wl,-Map=build/uartdiag/controller.map $(UART_OBJECTS) -lgcc -o $@
	$(SIZE) $@
build/uartdiag/controller.bin: build/uartdiag/controller.elf
	$(OBJCOPY) -O binary $< $@
build/uartdiag/controller.hex: build/uartdiag/controller.elf
	$(OBJCOPY) -O ihex $< $@
uartdiag: build/uartdiag/controller.bin build/uartdiag/controller.hex
uart-check: uartdiag
	python3 tools/verify_sources.py
	CROSS_COMPILE="$(CROSS_COMPILE)" python3 tests/check_image.py build/uartdiag/controller
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_diagnostic_protocol.c src/diagnostic_protocol.c -o build/uartdiag/test_protocol
	./build/uartdiag/test_protocol
-include $(UART_OBJECTS:.o=.d)

# Radio bring-up keeps the verified 115200 UART/clock; uartdiag remains recovery.
RADIO_OBJECTS := build/radiodiag/startup.o build/radiodiag/uart_system.o build/radiodiag/uart_diag.o build/radiodiag/diagnostic_protocol.o build/radiodiag/radio_io.o build/radiodiag/radio_transport.o build/radiodiag/radio_diag.o build/radiodiag/memory.o build/sx126x.o
.PHONY: radiodiag radio-check
build/radiodiag:
	mkdir -p $@
build/radiodiag/%.o: src/%.c | build/radiodiag
	$(CC) $(CFLAGS) -DRADIO_DIAGNOSTICS -c $< -o $@
build/radiodiag/startup.o: src/startup.S | build/radiodiag
	$(CC) $(ARCH) -g3 -c $< -o $@
build/radiodiag/controller.elf: $(RADIO_OBJECTS) cx32l003f8.ld
	$(CC) $(LDFLAGS) -Wl,-Map=build/radiodiag/controller.map $(RADIO_OBJECTS) -lgcc -o $@
	$(SIZE) $@
build/radiodiag/controller.bin: build/radiodiag/controller.elf
	$(OBJCOPY) -O binary $< $@
build/radiodiag/controller.hex: build/radiodiag/controller.elf
	$(OBJCOPY) -O ihex $< $@
radiodiag: build/radiodiag/controller.bin build/radiodiag/controller.hex
radio-check: radiodiag
	python3 tools/verify_sources.py
	CROSS_COMPILE="$(CROSS_COMPILE)" python3 tests/check_image.py build/radiodiag/controller
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror -DRADIO_DIAGNOSTICS -Iinclude -I$(VENDOR)/sx126x_driver/src tests/test_radio_diag.c src/radio_transport.c src/radio_diag.c src/diagnostic_protocol.c $(VENDOR)/sx126x_driver/src/sx126x.c -o build/radiodiag/test_radio
	./build/radiodiag/test_radio
-include $(RADIO_OBJECTS:.o=.d)

# Versioned binary UART controller. Recovery/legacy targets remain separate.
CONTROL_OBJECTS := $(addprefix build/uartctrl/,startup.o uart_system.o uart_control.o radio_protocol.o radio_control.o rf_switch.o radio_io.o radio_transport.o memory.o) build/sx126x.o
.PHONY: uartctrl protocol-check
build/uartctrl:
	mkdir -p $@
build/uartctrl/%.o: src/%.c | build/uartctrl
	$(CC) $(CFLAGS) -DUART_RADIO_PROTOCOL -c $< -o $@
build/uartctrl/startup.o: src/startup.S | build/uartctrl
	$(CC) $(ARCH) -g3 -c $< -o $@
build/uartctrl/controller.elf: $(CONTROL_OBJECTS) cx32l003f8.ld
	$(CC) $(LDFLAGS) -Wl,-Map=build/uartctrl/controller.map $(CONTROL_OBJECTS) -lgcc -o $@
	$(SIZE) $@
build/uartctrl/controller.bin: build/uartctrl/controller.elf
	$(OBJCOPY) -O binary $< $@
build/uartctrl/controller.hex: build/uartctrl/controller.elf
	$(OBJCOPY) -O ihex $< $@
uartctrl: build/uartctrl/controller.bin build/uartctrl/controller.hex
protocol-check: uartctrl
	python3 tools/verify_sources.py
	CROSS_COMPILE="$(CROSS_COMPILE)" python3 tests/check_image.py build/uartctrl/controller
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror -DUART_RADIO_PROTOCOL -Iinclude -I$(VENDOR)/sx126x_driver/src tests/test_radio_protocol.c src/radio_protocol.c src/radio_control.c src/rf_switch.c src/radio_transport.c $(VENDOR)/sx126x_driver/src/sx126x.c -o build/uartctrl/test_protocol
	./build/uartctrl/test_protocol
	python3 tests/test_uart_radio.py
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/test_rf_switch.c src/rf_switch.c -o build/uartctrl/test_rf_switch
	./build/uartctrl/test_rf_switch
-include $(CONTROL_OBJECTS:.o=.d)

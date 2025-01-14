
OUT_DIR += /src

OBJS += \
$(OUT_PATH)/src/utils.o \
$(OUT_PATH)/src/uclock.o \
$(OUT_PATH)/src/app.o \
$(OUT_PATH)/src/display.o \
$(OUT_PATH)/src/display_13seg_cell.o \
$(OUT_PATH)/src/display_3cell_line.o \
$(OUT_PATH)/src/display_drv_common.o \
$(OUT_PATH)/src/display_drv_cgdk2.o \
$(OUT_PATH)/src/display_drv_cgg1.o \
$(OUT_PATH)/src/sensors.o \
$(OUT_PATH)/src/button.o \
$(OUT_PATH)/src/trigger.o \
$(OUT_PATH)/src/app_att.o \
$(OUT_PATH)/src/battery.o \
$(OUT_PATH)/src/ble.o \
$(OUT_PATH)/src/i2c.o \
$(OUT_PATH)/src/cmd_parser.o \
$(OUT_PATH)/src/flash_eep.o \
$(OUT_PATH)/src/logger.o \
$(OUT_PATH)/src/blt_common.o\
$(OUT_PATH)/src/ccm.o \
$(OUT_PATH)/src/mi_beacon.o \
$(OUT_PATH)/src/main.o


# Each subdirectory must supply rules for building sources it contributes
$(OUT_PATH)/src/%.o: $(PROJECT_PATH)/%.c
	@echo 'Building file: $<'
	@$(TC32_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o"$@" "$<"

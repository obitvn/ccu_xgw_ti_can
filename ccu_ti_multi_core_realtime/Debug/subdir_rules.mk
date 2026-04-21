################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Arm Compiler - building file: "$<"'
	"C:/ti/ccs2031/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c -mcpu=cortex-r5 -mfloat-abi=hard -mfpu=vfpv3-d16 -mlittle-endian -mthumb -I"C:/ti/ccs2031/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/include/c" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source" -DSOC_AM263PX -DOS_NORTOS -D_DEBUG_=1 -g -Wall -Wno-gnu-variable-sized-type-not-at-end -Wno-unused-function -MMD -MP -MF"$(basename $(<F)).d_raw" -MT"$(@)" -I"D:/VinDynamics/Project/CCU_Robot_Central_Control_Unit/2.firmware/ccu_ti_mutilcore/ccu_ti_multi_core_realtime/Debug/syscfg"  $(GEN_OPTS__FLAG) -o"$@" "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

build-1352039491: ../example.syscfg
	@echo 'SysConfig - building file: "$<"'
	"C:/ti/ccs2031/ccs/utils/sysconfig_1.26.0/sysconfig_cli.bat" -s "C:/ti/mcu_plus_sdk_am263px_11_01_00_19/.metadata/product.json" -p "ZCZ_C" -r "AM263P4" --script "D:/VinDynamics/Project/CCU_Robot_Central_Control_Unit/2.firmware/ccu_ti_mutilcore/ccu_ti_multi_core_freertos/example.syscfg" --context "r5fss0-0" --script "D:/VinDynamics/Project/CCU_Robot_Central_Control_Unit/2.firmware/ccu_ti_mutilcore/ccu_ti_multi_core_realtime/example.syscfg" --context "r5fss1-1" -o "syscfg" --compiler ticlang
	@echo 'Finished building: "$<"'
	@echo ' '

syscfg/ti_dpl_config.c: build-1352039491 ../example.syscfg
syscfg/ti_dpl_config.h: build-1352039491
syscfg/ti_drivers_config.c: build-1352039491
syscfg/ti_drivers_config.h: build-1352039491
syscfg/ti_drivers_open_close.c: build-1352039491
syscfg/ti_drivers_open_close.h: build-1352039491
syscfg/ti_pinmux_config.c: build-1352039491
syscfg/pinmux.csv: build-1352039491
syscfg/ti_power_clock_config.c: build-1352039491
syscfg/ti_board_config.c: build-1352039491
syscfg/ti_board_config.h: build-1352039491
syscfg/ti_board_open_close.c: build-1352039491
syscfg/ti_board_open_close.h: build-1352039491
syscfg/ti_enet_config.c: build-1352039491
syscfg/ti_enet_config.h: build-1352039491
syscfg/ti_enet_init.c: build-1352039491
syscfg/ti_enet_dma_init.h: build-1352039491
syscfg/ti_enet_dma_init.c: build-1352039491
syscfg/ti_enet_open_close.c: build-1352039491
syscfg/ti_enet_open_close.h: build-1352039491
syscfg/ti_enet_soc.c: build-1352039491
syscfg/ti_enet_lwipif.c: build-1352039491
syscfg/ti_enet_lwipif.h: build-1352039491
syscfg/linker.cmd: build-1352039491
syscfg/linker_defines.h: build-1352039491
syscfg/ti_sdl_config.c: build-1352039491
syscfg/ti_sdl_config.h: build-1352039491
syscfg: build-1352039491

syscfg/%.o: ./syscfg/%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Arm Compiler - building file: "$<"'
	"C:/ti/ccs2031/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c -mcpu=cortex-r5 -mfloat-abi=hard -mfpu=vfpv3-d16 -mlittle-endian -mthumb -I"C:/ti/ccs2031/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/include/c" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source" -DSOC_AM263PX -DOS_NORTOS -D_DEBUG_=1 -g -Wall -Wno-gnu-variable-sized-type-not-at-end -Wno-unused-function -MMD -MP -MF"syscfg/$(basename $(<F)).d_raw" -MT"$(@)" -I"D:/VinDynamics/Project/CCU_Robot_Central_Control_Unit/2.firmware/ccu_ti_mutilcore/ccu_ti_multi_core_realtime/Debug/syscfg"  $(GEN_OPTS__FLAG) -o"$@" "$<"
	@echo 'Finished building: "$<"'
	@echo ' '



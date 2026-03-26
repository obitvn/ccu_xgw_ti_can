################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
build-677062176: ../example.syscfg
	@echo 'Building file: "$<"'
	@echo 'Invoking: SysConfig'
	"C:/ti/ccs2031/ccs/utils/sysconfig_1.26.0/sysconfig_cli.bat" -s "C:/ti/mcu_plus_sdk_am263px_11_01_00_19/.metadata/product.json" -p "ZCZ_C" -r "AM263P4" --script "D:/VinDynamics/Project/CCU_TI_RS485_CAN/2.Firmware/ccu_ti_mutilcore/ccu_ti_multi_core_realtime/example.syscfg" --context "r5fss1-1" --script "D:/VinDynamics/Project/CCU_TI_RS485_CAN/2.Firmware/ccu_ti_mutilcore/ccu_ti_multi_core_freertos/example.syscfg" --context "r5fss0-0" -o "syscfg" --compiler ticlang
	@echo 'Finished building: "$<"'
	@echo ' '

syscfg/ti_dpl_config.c: build-677062176 ../example.syscfg
syscfg/ti_dpl_config.h: build-677062176
syscfg/ti_drivers_config.c: build-677062176
syscfg/ti_drivers_config.h: build-677062176
syscfg/ti_drivers_open_close.c: build-677062176
syscfg/ti_drivers_open_close.h: build-677062176
syscfg/ti_pinmux_config.c: build-677062176
syscfg/pinmux.csv: build-677062176
syscfg/ti_power_clock_config.c: build-677062176
syscfg/ti_board_config.c: build-677062176
syscfg/ti_board_config.h: build-677062176
syscfg/ti_board_open_close.c: build-677062176
syscfg/ti_board_open_close.h: build-677062176
syscfg/ti_enet_config.c: build-677062176
syscfg/ti_enet_config.h: build-677062176
syscfg/ti_enet_init.c: build-677062176
syscfg/ti_enet_dma_init.h: build-677062176
syscfg/ti_enet_dma_init.c: build-677062176
syscfg/ti_enet_open_close.c: build-677062176
syscfg/ti_enet_open_close.h: build-677062176
syscfg/ti_enet_soc.c: build-677062176
syscfg/ti_enet_lwipif.c: build-677062176
syscfg/ti_enet_lwipif.h: build-677062176
syscfg/linker.cmd: build-677062176
syscfg/linker_defines.h: build-677062176
syscfg/ti_sdl_config.c: build-677062176
syscfg/ti_sdl_config.h: build-677062176
syscfg: build-677062176

syscfg/%.o: ./syscfg/%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Building file: "$<"'
	@echo 'Invoking: Arm Compiler'
	"C:/ti/ccs2031/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c -mcpu=cortex-r5 -mfloat-abi=hard -mfpu=vfpv3-d16 -mlittle-endian -mthumb -I"C:/ti/ccs2031/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/include/c" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/board/ethphy/enet/rtos_drivers/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/board/ethphy/port" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/kernel/freertos/FreeRTOS-Kernel/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/kernel/freertos/portable/TI_ARM_CLANG/ARM_CR5F" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/kernel/freertos/config/am263px/r5f" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core/utils/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core/include/phy" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core/include/core" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/hw_include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/soc/am263px" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/hw_include/mdio/V4" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/lwip/lwip-stack/src/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/lwip/lwip-port/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/lwip/lwip-port/freertos/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core/lwipif/inc" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/lwip/lwip-stack/contrib" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/lwip/lwip-config/am263px/enet" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core/examples/lwip/enet_lwip_cpsw/extPhyMgmt" -I"D:/VinDynamics/Project/CCU_TI_RS485_CAN/2.Firmware/ccu_ti_mutilcore/ccu_ti_multi_core_freertos/enet" -I"D:/VinDynamics/Project/CCU_TI_RS485_CAN/2.Firmware/ccu_ti_mutilcore/ccu_ti_multi_core_freertos/common" -DSOC_AM263PX -DOS_FREERTOS -D_DEBUG_=1 -g -Wall -Wno-gnu-variable-sized-type-not-at-end -Wno-unused-function -MMD -MP -MF"syscfg/$(basename $(<F)).d_raw" -MT"$(@)" -I"D:/VinDynamics/Project/CCU_TI_RS485_CAN/2.Firmware/ccu_ti_mutilcore/ccu_ti_multi_core_freertos/Debug/syscfg"  $(GEN_OPTS__FLAG) -o"$@" "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

%.o: ../%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Building file: "$<"'
	@echo 'Invoking: Arm Compiler'
	"C:/ti/ccs2031/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -c -mcpu=cortex-r5 -mfloat-abi=hard -mfpu=vfpv3-d16 -mlittle-endian -mthumb -I"C:/ti/ccs2031/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/include/c" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/board/ethphy/enet/rtos_drivers/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/board/ethphy/port" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/kernel/freertos/FreeRTOS-Kernel/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/kernel/freertos/portable/TI_ARM_CLANG/ARM_CR5F" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/kernel/freertos/config/am263px/r5f" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core/utils/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core/include/phy" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core/include/core" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/hw_include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/soc/am263px" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/hw_include/mdio/V4" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/lwip/lwip-stack/src/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/lwip/lwip-port/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/lwip/lwip-port/freertos/include" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core/lwipif/inc" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/lwip/lwip-stack/contrib" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/lwip/lwip-config/am263px/enet" -I"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/core/examples/lwip/enet_lwip_cpsw/extPhyMgmt" -I"D:/VinDynamics/Project/CCU_TI_RS485_CAN/2.Firmware/ccu_ti_mutilcore/ccu_ti_multi_core_freertos/enet" -I"D:/VinDynamics/Project/CCU_TI_RS485_CAN/2.Firmware/ccu_ti_mutilcore/ccu_ti_multi_core_freertos/common" -DSOC_AM263PX -DOS_FREERTOS -D_DEBUG_=1 -g -Wall -Wno-gnu-variable-sized-type-not-at-end -Wno-unused-function -MMD -MP -MF"$(basename $(<F)).d_raw" -MT"$(@)" -I"D:/VinDynamics/Project/CCU_TI_RS485_CAN/2.Firmware/ccu_ti_mutilcore/ccu_ti_multi_core_freertos/Debug/syscfg"  $(GEN_OPTS__FLAG) -o"$@" "$<"
	@echo 'Finished building: "$<"'
	@echo ' '



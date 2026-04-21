################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

CG_TOOL_ROOT := C:/ti/ccs2031/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS

GEN_OPTS__FLAG := 
GEN_CMDS__FLAG := -Wl,-l"syscfg/linker.cmd" 

ORDERED_OBJS += \
"./ccu_log.o" \
"./syscfg/ti_dpl_config.o" \
"./syscfg/ti_drivers_config.o" \
"./syscfg/ti_drivers_open_close.o" \
"./syscfg/ti_pinmux_config.o" \
"./syscfg/ti_power_clock_config.o" \
"./syscfg/ti_board_config.o" \
"./syscfg/ti_board_open_close.o" \
"./syscfg/ti_enet_config.o" \
"./syscfg/ti_enet_init.o" \
"./syscfg/ti_enet_dma_init.o" \
"./syscfg/ti_enet_open_close.o" \
"./syscfg/ti_enet_soc.o" \
"./syscfg/ti_enet_lwipif.o" \
"./syscfg/ti_sdl_config.o" \
"./freertos_hooks.o" \
"./gateway_shared.o" \
"./ipc_spinlock_sharedmem.o" \
"./log_reader_task.o" \
"./main.o" \
"./motor_mapping.o" \
"./syslog.o" \
"./test.o" \
"./common/crc32.o" \
"./common/xgw_protocol.o" \
"./enet/test_enet.o" \
"./enet/test_enet_cpsw.o" \
"./enet/xgw_udp_interface.o" \
"./extPhyMgmt/dp83867.o" \
"./extPhyMgmt/dp83869.o" \
"./extPhyMgmt/enetextphy.o" \
"./extPhyMgmt/enetextphy_phymdio_dflt.o" \
"./extPhyMgmt/generic_phy.o" \
$(GEN_CMDS__FLAG) \
-Wl,-lfreertos.am263px.r5f.ti-arm-clang.debug.lib \
-Wl,-ldrivers.am263px.r5f.ti-arm-clang.freertos.debug.lib \
-Wl,-lenet-cpsw.am263px.r5f.ti-arm-clang.freertos.debug.lib \
-Wl,-llwipif-cpsw-freertos.am263px.r5f.ti-arm-clang.debug.lib \
-Wl,-llwip-freertos.am263px.r5f.ti-arm-clang.debug.lib \
-Wl,-lboard.am263px.r5f.ti-arm-clang.freertos.debug.lib \
-Wl,-llibc.a \
-Wl,-llibsysbm.a \

-include ../makefile.init

RM := DEL /F
RMDIR := RMDIR /S/Q

# All of the sources participating in the build are defined here
-include sources.mk
-include subdir_vars.mk
-include common/subdir_vars.mk
-include enet/subdir_vars.mk
-include extPhyMgmt/subdir_vars.mk
-include subdir_rules.mk
-include common/subdir_rules.mk
-include enet/subdir_rules.mk
-include extPhyMgmt/subdir_rules.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C55_DEPS)),)
-include $(C55_DEPS)
endif
ifneq ($(strip $(C_UPPER_DEPS)),)
-include $(C_UPPER_DEPS)
endif
ifneq ($(strip $(S67_DEPS)),)
-include $(S67_DEPS)
endif
ifneq ($(strip $(S62_DEPS)),)
-include $(S62_DEPS)
endif
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(OPT_DEPS)),)
-include $(OPT_DEPS)
endif
ifneq ($(strip $(C??_DEPS)),)
-include $(C??_DEPS)
endif
ifneq ($(strip $(ASM_UPPER_DEPS)),)
-include $(ASM_UPPER_DEPS)
endif
ifneq ($(strip $(S??_DEPS)),)
-include $(S??_DEPS)
endif
ifneq ($(strip $(C64_DEPS)),)
-include $(C64_DEPS)
endif
ifneq ($(strip $(CXX_DEPS)),)
-include $(CXX_DEPS)
endif
ifneq ($(strip $(S64_DEPS)),)
-include $(S64_DEPS)
endif
ifneq ($(strip $(CLA_DEPS)),)
-include $(CLA_DEPS)
endif
ifneq ($(strip $(S55_DEPS)),)
-include $(S55_DEPS)
endif
ifneq ($(strip $(SV7A_DEPS)),)
-include $(SV7A_DEPS)
endif
ifneq ($(strip $(C62_DEPS)),)
-include $(C62_DEPS)
endif
ifneq ($(strip $(C67_DEPS)),)
-include $(C67_DEPS)
endif
ifneq ($(strip $(K_DEPS)),)
-include $(K_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
ifneq ($(strip $(CC_DEPS)),)
-include $(CC_DEPS)
endif
ifneq ($(strip $(C++_DEPS)),)
-include $(C++_DEPS)
endif
ifneq ($(strip $(C43_DEPS)),)
-include $(C43_DEPS)
endif
ifneq ($(strip $(S43_DEPS)),)
-include $(S43_DEPS)
endif
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(CPP_DEPS)),)
-include $(CPP_DEPS)
endif
ifneq ($(strip $(SA_DEPS)),)
-include $(SA_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
EXE_OUTPUTS += \
ccu_ti_multi_core_freertos.out 

EXE_OUTPUTS__QUOTED += \
"ccu_ti_multi_core_freertos.out" 


# All Target
all: $(OBJS) $(GEN_CMDS)
	@$(MAKE) --no-print-directory -Onone "ccu_ti_multi_core_freertos.out"

# Tool invocations
ccu_ti_multi_core_freertos.out: $(OBJS) $(GEN_CMDS)
	@echo 'Arm Linker - building target: "$@"'
	"C:/ti/ccs2031/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/tiarmclang.exe" -mcpu=cortex-r5 -mfloat-abi=hard -mfpu=vfpv3-d16 -mlittle-endian -mthumb -O0 -DSOC_AM263PX -DOS_FREERTOS -D_DEBUG_=1 -g -Wall -Wno-gnu-variable-sized-type-not-at-end -Wno-unused-function -Wl,-m"ipc_spinlock_sharedmem.Debug.map" -Wl,-i"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/kernel/freertos/lib" -Wl,-i"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/drivers/lib" -Wl,-i"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/board/lib" -Wl,-i"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/enet/lib" -Wl,-i"C:/ti/mcu_plus_sdk_am263px_11_01_00_19/source/networking/lwip/lib" -Wl,-i"C:/ti/ccs2031/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/lib" -Wl,-i"D:/VinDynamics/Project/CCU_Robot_Central_Control_Unit/2.firmware/ccu_ti_mutilcore/ccu_ti_multi_core_freertos/Debug/syscfg" -Wl,--reread_libs -Wl,--diag_suppress=10063 -Wl,--diag_wrap=off -Wl,--display_error_number -Wl,--warn_sections -Wl,--gen_xml_func_hash -Wl,--xml_link_info="ccu_ti_multi_core_freertos_linkInfo.xml" -Wl,--ram_model -o "ccu_ti_multi_core_freertos.out" $(ORDERED_OBJS)
	@echo 'Finished building target: "$@"'
	@echo ' '
	@$(MAKE) --no-print-directory post-build

# Other Targets
clean:
	-$(RM) $(GEN_MISC_FILES__QUOTED)$(GEN_FILES__QUOTED)$(EXE_OUTPUTS__QUOTED)
	-$(RM) "ccu_log.o" "syscfg\ti_dpl_config.o" "syscfg\ti_drivers_config.o" "syscfg\ti_drivers_open_close.o" "syscfg\ti_pinmux_config.o" "syscfg\ti_power_clock_config.o" "syscfg\ti_board_config.o" "syscfg\ti_board_open_close.o" "syscfg\ti_enet_config.o" "syscfg\ti_enet_init.o" "syscfg\ti_enet_dma_init.o" "syscfg\ti_enet_open_close.o" "syscfg\ti_enet_soc.o" "syscfg\ti_enet_lwipif.o" "syscfg\ti_sdl_config.o" "freertos_hooks.o" "gateway_shared.o" "ipc_spinlock_sharedmem.o" "log_reader_task.o" "main.o" "motor_mapping.o" "syslog.o" "test.o" "common\crc32.o" "common\xgw_protocol.o" "enet\test_enet.o" "enet\test_enet_cpsw.o" "enet\xgw_udp_interface.o" "extPhyMgmt\dp83867.o" "extPhyMgmt\dp83869.o" "extPhyMgmt\enetextphy.o" "extPhyMgmt\enetextphy_phymdio_dflt.o" "extPhyMgmt\generic_phy.o" 
	-$(RM) "ccu_log.d" "syscfg\ti_dpl_config.d" "syscfg\ti_drivers_config.d" "syscfg\ti_drivers_open_close.d" "syscfg\ti_pinmux_config.d" "syscfg\ti_power_clock_config.d" "syscfg\ti_board_config.d" "syscfg\ti_board_open_close.d" "syscfg\ti_enet_config.d" "syscfg\ti_enet_init.d" "syscfg\ti_enet_dma_init.d" "syscfg\ti_enet_open_close.d" "syscfg\ti_enet_soc.d" "syscfg\ti_enet_lwipif.d" "syscfg\ti_sdl_config.d" "freertos_hooks.d" "gateway_shared.d" "ipc_spinlock_sharedmem.d" "log_reader_task.d" "main.d" "motor_mapping.d" "syslog.d" "test.d" "common\crc32.d" "common\xgw_protocol.d" "enet\test_enet.d" "enet\test_enet_cpsw.d" "enet\xgw_udp_interface.d" "extPhyMgmt\dp83867.d" "extPhyMgmt\dp83869.d" "extPhyMgmt\enetextphy.d" "extPhyMgmt\enetextphy_phymdio_dflt.d" "extPhyMgmt\generic_phy.d" 
	-$(RMDIR) $(GEN_MISC_DIRS__QUOTED)
	-@echo ' '

post-build:
	-$(MAKE) -C D:\VinDynamics\Project\CCU_Robot_Central_Control_Unit\2.firmware\ccu_ti_mutilcore\ccu_ti_multi_core_freertos -f makefile_ccs_bootimage_gen OUTNAME=ccu_ti_multi_core_freertos PROFILE=Debug MCU_PLUS_SDK_PATH=C:/ti/mcu_plus_sdk_am263px_11_01_00_19 CG_TOOL_ROOT=C:/ti/ccs2031/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS CCS_INSTALL_DIR=C:\ti\ccs2031\ccs\ CCS_IDE_MODE=desktop DEVICE=am263px
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets


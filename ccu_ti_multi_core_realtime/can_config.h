/**
 * @file can_config.h
 * @brief CAN Configuration for Core 1 (NoRTOS)
 *
 * Temporary configuration until SysConfig is properly set up
 *
 * @author CCU Multicore Project
 * @date 2026-03-18
 */

#ifndef CAN_CONFIG_H_
#define CAN_CONFIG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * MCAN Base Addresses (AM263Px)
 * ========================================================================== */

#define CSL_MCANSS0_BASE_ADDR             0x2700000U
#define CSL_MCAN0_BASE                   (CSL_MCANSS0_BASE_ADDR + 0x0000U)
#define CSL_MCAN1_BASE                   (CSL_MCANSS0_BASE_ADDR + 0x1000U)
#define CSL_MCAN2_BASE                   (CSL_MCANSS0_BASE_ADDR + 0x2000U)
#define CSL_MCAN3_BASE                   (CSL_MCANSS0_BASE_ADDR + 0x3000U)
#define CSL_MCAN4_BASE                   (CSL_MCANSS0_BASE_ADDR + 0x4000U)
#define CSL_MCAN5_BASE                   (CSL_MCANSS0_BASE_ADDR + 0x5000U)
#define CSL_MCAN6_BASE                   (CSL_MCANSS0_BASE_ADDR + 0x6000U)
#define CSL_MCAN7_BASE                   (CSL_MCANSS0_BASE_ADDR + 0x7000U)

/* ==========================================================================
 * Interrupt Numbers
 * ========================================================================== */

#define CSL_R5FSS0_1_INT_VIMAG            (73U)
#define CSL_INT_PRG_INT_EVENT_IRQ_NUM    (384U)

/* MCAN Interrupt vectors */
#define MCUPLUS_CPU_ID                  (1U)  /* Core 1 */

/* Combine to get interrupt numbers */
#define CONFIG_MCAN0_INTR                (CSL_R5FSS0_1_INT_VIMAG)
#define CONFIG_MCAN1_INTR                (CSL_R5FSS0_1_INT_VIMAG)
#define CONFIG_MCAN2_INTR                (CSL_R5FSS0_1_INT_VIMAG)
#define CONFIG_MCAN3_INTR                (CSL_R5FSS0_1_INT_VIMAG)
#define CONFIG_MCAN4_INTR                (CSL_R5FSS0_1_INT_VIMAG)
#define CONFIG_MCAN5_INTR                (CSL_R5FSS0_1_INT_VIMAG)
#define CONFIG_MCAN6_INTR                (CSL_R5FSS0_1_INT_VIMAG)
#define CONFIG_MCAN7_INTR                (CSL_R5FSS0_1_INT_VIMAG)

/* ==========================================================================
 * GPIO Standby Pins for CAN Transceivers
 * ========================================================================== */

/* MCAN0 STB - MMC_SDCD */
#define CONFIG_GPIO_STB_MCAN0_BASE_ADDR  0xE0005000U
#define CONFIG_GPIO_STB_MCAN0_PIN        9U
#define CONFIG_GPIO_STB_MCAN0_DIR        0U

/* MCAN1 STB - MMC_SDWP */
#define CONFIG_GPIO_STB_MCAN1_BASE_ADDR  0xE0005000U
#define CONFIG_GPIO_STB_MCAN1_PIN        8U
#define CONFIG_GPIO_STB_MCAN1_DIR        0U

/* MCAN2 STB - EQEP0_A */
#define CONFIG_GPIO_STB_MCAN2_BASE_ADDR  0xE0005000U
#define CONFIG_GPIO_STB_MCAN2_PIN        0U
#define CONFIG_GPIO_STB_MCAN2_DIR        0U

/* MCAN3 STB - EPWM1_A */
#define CONFIG_GPIO_STB_MCAN3_BASE_ADDR  0xE0005000U
#define CONFIG_GPIO_STB_MCAN3_PIN        18U
#define CONFIG_GPIO_STB_MCAN3_DIR        0U

/* MCAN4 STB - EPWM0_A */
#define CONFIG_GPIO_STB_MCAN4_BASE_ADDR  0xE0005000U
#define CONFIG_GPIO_STB_MCAN4_PIN        0U
#define CONFIG_GPIO_STB_MCAN4_DIR        0U

/* MCAN5 STB - EPWM9_B */
#define CONFIG_GPIO_STB_MCAN5_BASE_ADDR  0xE0005000U
#define CONFIG_GPIO_STB_MCAN5_PIN        17U
#define CONFIG_GPIO_STB_MCAN5_DIR        0U

/* MCAN6 STB - EPWM0_B */
#define CONFIG_GPIO_STB_MCAN6_BASE_ADDR  0xE0005000U
#define CONFIG_GPIO_STB_MCAN6_PIN        13U
#define CONFIG_GPIO_STB_MCAN6_DIR        0U

/* MCAN7 STB - EPWM13_B */
#define CONFIG_GPIO_STB_MCAN7_BASE_ADDR  0xE0005000U
#define CONFIG_GPIO_STB_MCAN7_PIN        23U
#define CONFIG_GPIO_STB_MCAN7_DIR        0U

/* Map to our defines */
#define CONFIG_MCAN0_BASE_ADDR           CSL_MCAN0_BASE
#define CONFIG_MCAN1_BASE_ADDR           CSL_MCAN1_BASE
#define CONFIG_MCAN2_BASE_ADDR           CSL_MCAN2_BASE
#define CONFIG_MCAN3_BASE_ADDR           CSL_MCAN3_BASE
#define CONFIG_MCAN4_BASE_ADDR           CSL_MCAN4_BASE
#define CONFIG_MCAN5_BASE_ADDR           CSL_MCAN5_BASE
#define CONFIG_MCAN6_BASE_ADDR           CSL_MCAN6_BASE
#define CONFIG_MCAN7_BASE_ADDR           CSL_MCAN7_BASE

#ifdef __cplusplus
}
#endif

#endif /* CAN_CONFIG_H_ */

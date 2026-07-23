#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include "driver/gpio.h"

/* =====================================================================
 * 1. Intel 8080 8-Bit Parallel LCD Interface Pins (Page 1 & Page 3)
 * ===================================================================== */
#define LCD_DATA0_GPIO         GPIO_NUM_1   // Net: D0
#define LCD_DATA1_GPIO         GPIO_NUM_2   // Net: D1
#define LCD_DATA2_GPIO         GPIO_NUM_4   // Net: D2
#define LCD_DATA3_GPIO         GPIO_NUM_5   // Net: D3
#define LCD_DATA4_GPIO         GPIO_NUM_6   // Net: D4
#define LCD_DATA5_GPIO         GPIO_NUM_7   // Net: D5
#define LCD_DATA6_GPIO         GPIO_NUM_8   // Net: D6
#define LCD_DATA7_GPIO         GPIO_NUM_9   // Net: D7

// Control Lines
#define LCD_RD_GPIO            GPIO_NUM_13  // Net: RD (Kept HIGH during parallel writes)
#define LCD_WR_GPIO            GPIO_NUM_12  // Net: WRX / WR
#define LCD_DC_GPIO            GPIO_NUM_11  // Net: D/CX (Data/Command Selection)
#define LCD_CS_GPIO            GPIO_NUM_10  // Net: CS (Chip Select)
#define LCD_RESET_GPIO         GPIO_NUM_3   // Net: RESET (Active LOW)

// Backlight Control
#define LCD_BL_CONTROL_GPIO    GPIO_NUM_40  // Net: LEDK (Gated via BSS138 Q1 circuit)

/* =====================================================================
 * 2. System I2C Bus Pins (Page 1 & Page 3)
 * Shared between TCA9534PWR I/O Expander & TSC2007IPWR Touch Controller
 * ===================================================================== */
#define I2C_MASTER_SDA_GPIO    GPIO_NUM_41  // Net: SDA
#define I2C_MASTER_SCL_GPIO    GPIO_NUM_42  // Net: SCL

/* =====================================================================
 * 3. Peripheral & Interrupt Pins (Page 1, 3 & 4)
 * ===================================================================== */
#define TOUCH_PENIRQ_GPIO      GPIO_NUM_47  // Net: PENIRQ (Touch screen interrupt)
// #define IO_EXPANDER_INT_GPIO   GPIO_NUM_39  // Net: INTSW (TCA9534 Interrupt)
#define BUZZER_GPIO            GPIO_NUM_46  // Net: BUZZER (Driven via Q2)

/* =====================================================================
 * 4. Industrial Input / Output Signals (Page 4 Optocouplers)
 * ===================================================================== */
#define FOOT_SIGNAL_GPIO       GPIO_NUM_15  // Net: FOOTSIG
#define TRIMMER_SIGNAL_GPIO    GPIO_NUM_16  // Net: TRIMSIG
#define PULSE_OUT_GPIO         GPIO_NUM_14  // Net: PULSEOUT
#define WIPER_SIGNAL_GPIO      GPIO_NUM_17  // Net: WIPSIG
#define CON_STH_SIGNAL_GPIO    GPIO_NUM_18  // Net: CONSIG
#define TENSION_SIGNAL_GPIO    GPIO_NUM_21  // Net: TENSIG

#define NPLED_GPIO             GPIO_NUM_38  // Net: NPLED
#define NFC_IOX_GPIO           GPIO_NUM_39  // Net: INTNFC & TCA9534

#endif // HARDWARE_CONFIG_H

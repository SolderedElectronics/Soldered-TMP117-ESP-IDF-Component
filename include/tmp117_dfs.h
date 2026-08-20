/**
 * @file tmp117_dfs.h
 * @brief Register map, bit definitions and enumerations for the TMP117
 *
 * Everything in here comes straight from the TMP117 datasheet:
 * https://www.ti.com/lit/ds/symlink/tmp117.pdf
 *
 * @author Soldered Electronics
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// *****************************************************************************
// Section: I2C addresses
//
// The address is selected with the ADD0 and ADD1 pins. The Soldered breakout
// ties them so that the sensor answers on 0x49, and the onboard jumpers can be
// cut and bridged to move it to any of the other three.

#define TMP117_ADDRESS_0x48    0x48 /**< ADD1 low, ADD0 low */
#define TMP117_ADDRESS_0x49    0x49 /**< ADD1 low, ADD0 high - the Soldered breakout default */
#define TMP117_ADDRESS_0x4A    0x4A /**< ADD1 high, ADD0 low */
#define TMP117_ADDRESS_0x4B    0x4B /**< ADD1 high, ADD0 high */
#define TMP117_DEFAULT_ADDRESS TMP117_ADDRESS_0x49 /**< Address the Soldered breakout ships with */

// *****************************************************************************
// Section: Register map

#define TMP117_REG_TEMPERATURE        0x00 /**< Temperature result, read only, two's complement */
#define TMP117_REG_CONFIGURATION      0x01 /**< Configuration and status */
#define TMP117_REG_TEMP_HIGH_LIMIT    0x02 /**< High limit compared against the temperature result */
#define TMP117_REG_TEMP_LOW_LIMIT     0x03 /**< Low limit compared against the temperature result */
#define TMP117_REG_EEPROM_UNLOCK      0x04 /**< EEPROM unlock and EEPROM busy status */
#define TMP117_REG_EEPROM1            0x05 /**< General purpose EEPROM word 1 */
#define TMP117_REG_EEPROM2            0x06 /**< General purpose EEPROM word 2 */
#define TMP117_REG_TEMPERATURE_OFFSET 0x07 /**< Offset added to the temperature result */
#define TMP117_REG_EEPROM3            0x08 /**< General purpose EEPROM word 3 */
#define TMP117_REG_DEVICE_ID          0x0F /**< Revision and device ID */

// *****************************************************************************
// Section: Configuration register, address 0x01

#define TMP117_CONFIG_HIGH_ALERT_BIT  15 /**< High alert flag, cleared by reading the register */
#define TMP117_CONFIG_LOW_ALERT_BIT   14 /**< Low alert flag, cleared by reading the register */
#define TMP117_CONFIG_DATA_READY_BIT  13 /**< Set when a conversion finishes, cleared by reading */
#define TMP117_CONFIG_EEPROM_BUSY_BIT 12 /**< Set while an EEPROM word is being programmed */
#define TMP117_CONFIG_MOD_SHIFT       10 /**< Conversion mode, MOD[1:0] */
#define TMP117_CONFIG_MOD_MASK        0x03
#define TMP117_CONFIG_CONV_SHIFT      7 /**< Conversion cycle time, CONV[2:0] */
#define TMP117_CONFIG_CONV_MASK       0x07
#define TMP117_CONFIG_AVG_SHIFT       5 /**< Averaging mode, AVG[1:0] */
#define TMP117_CONFIG_AVG_MASK        0x03
#define TMP117_CONFIG_THERM_BIT       4 /**< 1 selects therm mode, 0 selects alert mode */
#define TMP117_CONFIG_POL_BIT         3 /**< ALERT pin polarity, 0 is active low */
#define TMP117_CONFIG_DR_ALERT_BIT    2 /**< 1 makes the ALERT pin a data ready flag */
#define TMP117_CONFIG_SOFT_RESET_BIT  1 /**< Write 1 to reload the default configuration */

// *****************************************************************************
// Section: EEPROM unlock register, address 0x04

#define TMP117_EEPROM_UNLOCK_BIT 15 /**< 1 removes the EEPROM write protection */
#define TMP117_EEPROM_BUSY_BIT   14 /**< Set while an EEPROM word is being programmed */

// *****************************************************************************
// Section: Device ID register, address 0x0F

#define TMP117_DEVICE_ID_MASK  0x0FFF /**< DID[11:0], always 0x117 */
#define TMP117_DEVICE_ID_VALUE 0x0117 /**< Value a working TMP117 reports */
#define TMP117_DEVICE_REV_MASK 0x000F /**< REV[3:0], read out of bits [15:12] */

// *****************************************************************************
// Section: Conversion

/** Weight of one LSB of the temperature, limit and offset registers, in °C */
#define TMP117_RESOLUTION 0.0078125f

/** Largest temperature the 16 bit registers can hold, in °C */
#define TMP117_TEMP_MAX (32767 * TMP117_RESOLUTION)

/** Most negative temperature the 16 bit registers can hold, in °C */
#define TMP117_TEMP_MIN (-32768 * TMP117_RESOLUTION)

// *****************************************************************************
// Section: Enumerations

/**
 * @brief What the ALERT pin reports
 */
typedef enum {
    TMP117_PMODE_THERMAL = 0, /**< Therm mode: the pin follows a hysteresis comparator on the high limit */
    TMP117_PMODE_ALERT,       /**< Alert mode: the pin asserts while the result is outside the limits */
    TMP117_PMODE_DATA,        /**< Data ready mode: the pin asserts when a new conversion is available */
} tmp117_pmode_t;

/**
 * @brief Conversion mode
 */
typedef enum {
    TMP117_CMODE_CONTINUOUS = 0, /**< Convert forever, one conversion per conversion cycle */
    TMP117_CMODE_SHUTDOWN = 1,   /**< Stop converting and drop to about 250 nA */
    TMP117_CMODE_ONESHOT = 3,    /**< Run one conversion, then shut down */
} tmp117_cmode_t;

/**
 * @brief Conversion cycle time
 *
 * The cycle time is the longer of this setting and the time the averaging takes,
 * so the two settings interact:
 *
 *      CONV                 AVG=NOAVE   AVG=AVE8   AVG=AVE32   AVG=AVE64
 *      TMP117_CONVT_C15mS5    15.5 ms     125 ms      500 ms        1 s
 *      TMP117_CONVT_C125mS     125 ms     125 ms      500 ms        1 s
 *      TMP117_CONVT_C250mS     250 ms     250 ms      500 ms        1 s
 *      TMP117_CONVT_C500mS     500 ms     500 ms      500 ms        1 s
 *      TMP117_CONVT_C1S           1 s        1 s         1 s        1 s
 *      TMP117_CONVT_C4S           4 s        4 s         4 s        4 s
 *      TMP117_CONVT_C8S           8 s        8 s         8 s        8 s
 *      TMP117_CONVT_C16S         16 s       16 s        16 s       16 s
 */
typedef enum {
    TMP117_CONVT_C15mS5 = 0, /**< 15.5 ms */
    TMP117_CONVT_C125mS,     /**< 125 ms */
    TMP117_CONVT_C250mS,     /**< 250 ms */
    TMP117_CONVT_C500mS,     /**< 500 ms */
    TMP117_CONVT_C1S,        /**< 1 s */
    TMP117_CONVT_C4S,        /**< 4 s */
    TMP117_CONVT_C8S,        /**< 8 s */
    TMP117_CONVT_C16S,       /**< 16 s */
} tmp117_convt_t;

/**
 * @brief How many conversions get averaged into one result
 *
 * More averaging means less noise but a longer conversion cycle.
 */
typedef enum {
    TMP117_AVE_NOAVE = 0, /**< No averaging */
    TMP117_AVE_8,         /**< Average 8 conversions */
    TMP117_AVE_32,        /**< Average 32 conversions */
    TMP117_AVE_64,        /**< Average 64 conversions */
} tmp117_ave_t;

/**
 * @brief Which alert flag was set the last time the configuration register was read
 */
typedef enum {
    TMP117_ALERT_NONE = 0, /**< Neither limit was crossed */
    TMP117_ALERT_HIGH,     /**< The result went above the high limit */
    TMP117_ALERT_LOW,      /**< The result went below the low limit */
} tmp117_alert_t;

#ifdef __cplusplus
}
#endif

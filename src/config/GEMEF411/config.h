/*
 * This file is part of Betaflight.
 *
 * Betaflight is free software. You can redistribute this software
 * and/or modify this software under the terms of the GNU General
 * Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later
 * version.
 *
 * Betaflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

/*
   This file has been auto generated from unified-targets repo.

   The auto generation is transitional only, please remove this comment once the file is edited.
*/

#define FC_TARGET_MCU     STM32F411

#define BOARD_NAME        GEMEF411
#define MANUFACTURER_ID   GFPV

#define USE_GYRO_SPI_ICM42688P
#define USE_ACC_SPI_ICM42688P
#define USE_ACCGYRO_BMI270
#define USE_FLASH_M25P16
#define USE_MAX7456

#define BEEPER_PIN           PC14
#define MOTOR1_PIN           PA8
#define MOTOR2_PIN           PB4
#define MOTOR3_PIN           PB0
#define MOTOR4_PIN           PB5
#define MOTOR5_PIN           PA9
#define MOTOR6_PIN           PA10
#define RX_PPM_PIN           PA2
#define LED_STRIP_PIN        PA0
#define UART1_TX_PIN         PA15
#define UART2_TX_PIN         PA2
#define UART11_TX_PIN        PB10
#define UART12_TX_PIN        PB6
#define UART1_RX_PIN         PB3
#define UART2_RX_PIN         PA3
#define UART11_RX_PIN        PB10
#define UART12_RX_PIN        PB6
#define I2C1_SCL_PIN         PB8
#define I2C1_SDA_PIN         PB9
#define LED0_PIN             PC13
#define SPI1_SCK_PIN         PA5
#define SPI2_SCK_PIN         PB13
#define SPI1_MISO_PIN        PA6
#define SPI2_MISO_PIN        PB14
#define SPI1_MOSI_PIN        PA7
#define SPI2_MOSI_PIN        PB15
#define ADC_VBAT_PIN         PA1
#define ADC_CURR_PIN         PB1
#define FLASH_CS_PIN         PB2
#define MAX7456_SPI_CS_PIN   PB12
#define GYRO_1_EXTI_PIN      PC15
#define GYRO_1_CS_PIN        PA4

#define TIMER_PIN_MAPPING \
    TIMER_PIN_MAP( 0, PA2 , 3, -1) \
    TIMER_PIN_MAP( 1, PA8 , 1,  0) \
    TIMER_PIN_MAP( 2, PA9 , 1,  0) \
    TIMER_PIN_MAP( 3, PA10, 1,  0) \
    TIMER_PIN_MAP( 4, PB0 , 2,  0) \
    TIMER_PIN_MAP( 5, PB4 , 1,  0) \
    TIMER_PIN_MAP( 6, PB5 , 1,  0) \
    TIMER_PIN_MAP( 7, PA0 , 2,  0) \
    TIMER_PIN_MAP( 8, PB6 , 1,  0) \
    TIMER_PIN_MAP( 9, PB10, 1,  0) \



#define ADC1_DMA_OPT        0

//TODO #define DSHOT_BITBANG ON
//TODO #define MAG_BUSTYPE I2C
#define MAG_I2C_INSTANCE (I2CDEV_1)
//TODO #define BARO_BUSTYPE I2C
#define BARO_I2C_INSTANCE (I2CDEV_1)
//TODO #define BLACKBOX_DEVICE SPIFLASH
//TODO #define SERIALRX_PROVIDER SBUS
//TODO #define DSHOT_BURST AUTO
//TODO #define CURRENT_METER ADC
//TODO #define BATTERY_METER ADC
//TODO #define IBATA_SCALE 800
#define BEEPER_INVERTED
//TODO #define BEEPER_OD OFF
#define SYSTEM_HSE_MHZ 8
#define MAX7456_SPI_INSTANCE SPI2
#define FLASH_SPI_INSTANCE SPI2
#define USE_SPI_GYRO
#define GYRO_1_SPI_INSTANCE SPI1
#define GYRO_1_ALIGN CW180_DEG
#define GYRO_1_ALIGN_YAW 1800

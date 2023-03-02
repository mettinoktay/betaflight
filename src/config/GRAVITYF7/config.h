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

#define FC_TARGET_MCU     STM32F7X2

#define BOARD_NAME        GRAVITYF7
#define MANUFACTURER_ID   FLMO

#define USE_GYRO_SPI_MPU6000
#define USE_ACC_SPI_MPU6000
#define USE_BARO_BMP280
#define USE_FLASH_W25Q128FV
#define USE_MAX7456

#define BEEPER_PIN           PC13
#define MOTOR1_PIN           PA9
#define MOTOR2_PIN           PA8
#define MOTOR3_PIN           PC8
#define MOTOR4_PIN           PC9
#define RX_PPM_PIN           PB7
#define LED_STRIP_PIN        PA15
#define UART1_TX_PIN         PB6
#define UART2_TX_PIN         PA2
#define UART3_TX_PIN         PC10
#define UART4_TX_PIN         PA0
#define UART5_TX_PIN         PC12
#define UART6_TX_PIN         PC6
#define UART1_RX_PIN         PB7
#define UART2_RX_PIN         PA3
#define UART3_RX_PIN         PC11
#define UART4_RX_PIN         PA1
#define UART5_RX_PIN         PD2
#define UART6_RX_PIN         PC7
#define I2C1_SCL_PIN         PB8
#define I2C1_SDA_PIN         PB9
#define LED0_PIN             PC14
#define SPI1_SCK_PIN         PA5
#define SPI2_SCK_PIN         PB13
#define SPI3_SCK_PIN         PB3
#define SPI1_MISO_PIN        PA6
#define SPI2_MISO_PIN        PB14
#define SPI3_MISO_PIN        PB4
#define SPI1_MOSI_PIN        PA7
#define SPI2_MOSI_PIN        PB15
#define SPI3_MOSI_PIN        PB5
#define CAMERA_CONTROL_PIN   PB1
#define ADC_VBAT_PIN         PC1
#define ADC_CURR_PIN         PC0
#define PINIO1_PIN           PC2
#define FLASH_CS_PIN         PB2
#define MAX7456_SPI_CS_PIN   PB12
#define GYRO_1_EXTI_PIN      PB10
#define GYRO_1_CS_PIN        PB0
#define USB_DETECT_PIN       PA10

#define TIMER_PIN_MAPPING \
    TIMER_PIN_MAP( 0, PB7 , 1,  0) \
    TIMER_PIN_MAP( 1, PA9 , 1,  0) \
    TIMER_PIN_MAP( 2, PA8 , 1,  0) \
    TIMER_PIN_MAP( 3, PC8 , 2,  0) \
    TIMER_PIN_MAP( 4, PC9 , 2,  0) \
    TIMER_PIN_MAP( 5, PA15, 1,  0) \
    TIMER_PIN_MAP( 6, PB1 , 2,  0) \



#define ADC1_DMA_OPT        1

//TODO #define MAG_BUSTYPE I2C
#define MAG_I2C_INSTANCE (I2CDEV_1)
//TODO #define BARO_BUSTYPE I2C
#define BARO_I2C_INSTANCE (I2CDEV_1)
//TODO #define BARO_I2C_ADDRESS 119
//TODO #define ADC_DEVICE 1
//TODO #define BLACKBOX_DEVICE SPIFLASH
//TODO #define DSHOT_BURST ON
//TODO #define CURRENT_METER ADC
//TODO #define BATTERY_METER ADC
//TODO #define VBAT_SCALE 111
//TODO #define IBATA_SCALE 182
#define BEEPER_INVERTED
//TODO #define BEEPER_OD OFF
//TODO #define OSD_DISPLAYPORT_DEVICE MAX7456
#define MAX7456_SPI_INSTANCE SPI2
#define FLASH_SPI_INSTANCE SPI3
#define USE_SPI_GYRO
#define GYRO_1_SPI_INSTANCE SPI1
#define GYRO_1_ALIGN CW0_DEG_FLIP
#define GYRO_1_ALIGN_PITCH 1800
//TODO #define PINIO_CONFIG 129,1,1,1
//TODO #define PINIO_BOX 40,255,255,255

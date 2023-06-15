/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "platform.h"

#ifdef USE_TIMER

#include "common/utils.h"

#include "timer_def.h"
#include "drivers/dma.h"
#include "drivers/io.h"
#include "drivers/rcc.h"
#include "drivers/timer.h"


const timerDef_t timerDefinitions[HARDWARE_TIMER_DEFINITION_COUNT] = {
    { .TIMx = TMR1,  .rcc = RCC_APB2(TMR1),  .inputIrq = TMR1_CH_IRQn },
    { .TIMx = TMR2,  .rcc = RCC_APB1(TMR2),  .inputIrq = TMR2_GLOBAL_IRQn },
    { .TIMx = TMR3,  .rcc = RCC_APB1(TMR3),  .inputIrq = TMR3_GLOBAL_IRQn },
    { .TIMx = TMR4,  .rcc = RCC_APB1(TMR4),  .inputIrq = TMR4_GLOBAL_IRQn },
    { .TIMx = TMR5,  .rcc = RCC_APB1(TMR5),  .inputIrq = TMR5_GLOBAL_IRQn },
    { .TIMx = TMR6,  .rcc = RCC_APB1(TMR6),  .inputIrq = TMR6_DAC_GLOBAL_IRQn },
    { .TIMx = TMR7,  .rcc = RCC_APB1(TMR7),  .inputIrq = TMR7_GLOBAL_IRQn} ,
    { .TIMx = TMR8,  .rcc = RCC_APB2(TMR8),  .inputIrq = TMR8_CH_IRQn },
    { .TIMx = TMR9,  .rcc = RCC_APB2(TMR9),  .inputIrq = TMR1_BRK_TMR9_IRQn },
    { .TIMx = TMR10, .rcc = RCC_APB2(TMR10), .inputIrq = TMR1_OVF_TMR10_IRQn },
    { .TIMx = TMR11, .rcc = RCC_APB2(TMR11), .inputIrq = TMR1_TRG_HALL_TMR11_IRQn },
    { .TIMx = TMR12, .rcc = RCC_APB1(TMR12), .inputIrq = TMR8_BRK_TMR12_IRQn },
    { .TIMx = TMR13, .rcc = RCC_APB1(TMR13), .inputIrq = TMR8_OVF_TMR13_IRQn },
    { .TIMx = TMR14, .rcc = RCC_APB1(TMR14), .inputIrq = TMR8_TRG_HALL_TMR14_IRQn },
    { .TIMx = TMR20, .rcc = RCC_APB2(TMR20), .inputIrq = TMR20_CH_IRQn },
};

#if defined(USE_TIMER_MGMT)
const timerHardware_t fullTimerHardware[FULL_TIMER_CHANNEL_COUNT] = {
// Port A
    DEF_TIM(TMR2,  CH1,  PA0,  0, 0,  0),
    DEF_TIM(TMR2,  CH2,  PA1,  0, 0,  0),
    DEF_TIM(TMR2,  CH3,  PA2,  0, 0,  0),
    DEF_TIM(TMR2,  CH4,  PA3,  0, 0,  0),
    DEF_TIM(TMR2,  CH1,  PA5,  0, 0,  0),
    DEF_TIM(TMR2,  CH1,  PA15, 0, 0,  0),
    DEF_TIM(TMR5,  CH1,  PA0,  0, 0,  0),
    DEF_TIM(TMR5,  CH2,  PA1,  0, 0,  0),
    DEF_TIM(TMR5,  CH3,  PA2,  0, 0,  0),
    DEF_TIM(TMR5,  CH4,  PA3,  0, 0,  0),
    DEF_TIM(TMR3,  CH1,  PA6,  0, 0,  0),
    DEF_TIM(TMR3,  CH2,  PA7,  0, 0,  0),
    DEF_TIM(TMR8,  CH1N, PA5,  0, 0,  0),
    DEF_TIM(TMR8,  CH1N, PA7,  0, 0,  0),
    DEF_TIM(TMR1,  CH1N, PA7,  0, 0,  0),
    DEF_TIM(TMR1,  CH1,  PA8,  0, 0,  0),
    DEF_TIM(TMR1,  CH2,  PA9,  0, 0,  0),
    DEF_TIM(TMR1,  CH3,  PA10, 0, 0,  0),
    DEF_TIM(TMR1,  CH4,  PA11, 0, 0,  0),

// Port B ORDER BY MUX 1 2 3
//MUX1
    DEF_TIM(TMR1,  CH2N, PB0,  0, 0,  0),
    DEF_TIM(TMR1,  CH3N, PB1,  0, 0,  0),
    DEF_TIM(TMR2,  CH4,  PB2,  0, 0,  0),
    DEF_TIM(TMR2,  CH2,  PB3,  0, 0,  0),
    DEF_TIM(TMR2,  CH1,  PB8,  0, 0,  0),
    DEF_TIM(TMR2,  CH2,  PB9,  0, 0,  0),
    DEF_TIM(TMR2,  CH3,  PB10, 0, 0,  0),
    DEF_TIM(TMR2,  CH4,  PB11, 0, 0,  0),
    DEF_TIM(TMR1,  CH1N, PB13, 0, 0,  0),
    DEF_TIM(TMR1,  CH2N, PB14, 0, 0,  0),
    DEF_TIM(TMR1,  CH3N, PB15, 0, 0,  0),
//MUX2
    DEF_TIM(TMR3,  CH3,  PB0,  0, 0,  0),
    DEF_TIM(TMR3,  CH4,  PB1,  0, 0,  0),
    DEF_TIM(TMR20, CH1,  PB2,  0, 0,  0),
    DEF_TIM(TMR3,  CH1,  PB4,  0, 0,  0),
    DEF_TIM(TMR3,  CH2,  PB5,  0, 0,  0),
    DEF_TIM(TMR4,  CH1,  PB6,  0, 13, 9),
    DEF_TIM(TMR4,  CH2,  PB7,  0, 12, 9),
    DEF_TIM(TMR4,  CH3,  PB8,  0, 11, 9),
    DEF_TIM(TMR4,  CH4,  PB9,  0, 10, 9),
    DEF_TIM(TMR5,  CH4,  PB11, 0, 0,  0),
    DEF_TIM(TMR5,  CH1,  PB12, 0, 0,  0),
//MUX3
    DEF_TIM(TMR8,  CH2N, PB0,  0, 0,  0),
    DEF_TIM(TMR8,  CH3N, PB1,  0, 0,  0),
    DEF_TIM(TMR8,  CH2N, PB14, 0, 0,  0),
    DEF_TIM(TMR8,  CH3N, PB15, 0, 0,  0),

// Port C ORDER BY MUX 1 2 3
//MUX2
    DEF_TIM(TMR20, CH2,  PC2,  0, 0,  0),
    DEF_TIM(TMR3,  CH1,  PC6,  0, 0,  12),
    DEF_TIM(TMR3,  CH2,  PC7,  0, 0,  12),
    DEF_TIM(TMR3,  CH3,  PC8,  0, 0,  12),
    DEF_TIM(TMR3,  CH4,  PC9,  0, 0,  12),
    DEF_TIM(TMR5,  CH2,  PC10, 0, 0,  0),
    DEF_TIM(TMR5,  CH3,  PC11, 0, 0,  0),
//MUX 3
    DEF_TIM(TMR8,  CH1,  PC6,  0, 0,  0),
    DEF_TIM(TMR8,  CH2,  PC7,  0, 0,  0),
    DEF_TIM(TMR8,  CH3,  PC8,  0, 0,  0),
    DEF_TIM(TMR8,  CH4,  PC9,  0, 0,  0),
  };
#endif

uint32_t timerClock(tmr_type *tim)
{
    UNUSED(tim);
    return system_core_clock;
}
#endif

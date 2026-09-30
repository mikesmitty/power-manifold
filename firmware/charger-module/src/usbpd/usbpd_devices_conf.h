#pragma once

// What ST's STM32G0 device layer wants to know about the board: which UCPD,
// which DMA channels carry its messages, which timer times them. The file
// name and every macro name are the device layer's.

#include "stm32g0xx_ll_bus.h"
#include "stm32g0xx_ll_dma.h"
#include "stm32g0xx_ll_gpio.h"
#include "stm32g0xx_ll_pwr.h"
#include "stm32g0xx_ll_rcc.h"
#include "stm32g0xx_ll_tim.h"
#include "stm32g0xx_ll_ucpd.h"

#include "usbpd_pwr_if.h"
#include "usbpd_pwr_user.h"

// the device layer takes these from ST's HAL, which is not part of the build
#ifndef UNUSED
#define UNUSED(X) (void)(X)
#endif
uint32_t HAL_GetTick(void);
void     HAL_Delay(uint32_t Delay);

// UCPD1: CC1 on PA8, CC2 on PB15
#define UCPD_INSTANCE0 UCPD1

#define UCPDDMA_INSTANCE0_CLOCKENABLE_RX  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1)
#define UCPDDMA_INSTANCE0_DMA_RX          DMA1
#define UCPDDMA_INSTANCE0_REQUEST_RX      LL_DMAMUX_REQ_UCPD1_RX
#define UCPDDMA_INSTANCE0_LL_CHANNEL_RX   LL_DMA_CHANNEL_4
#define UCPDDMA_INSTANCE0_CHANNEL_RX      DMA1_Channel4

#define UCPDDMA_INSTANCE0_CLOCKENABLE_TX  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1)
#define UCPDDMA_INSTANCE0_DMA_TX          DMA1
#define UCPDDMA_INSTANCE0_REQUEST_TX      LL_DMAMUX_REQ_UCPD1_TX
#define UCPDDMA_INSTANCE0_LL_CHANNEL_TX   LL_DMA_CHANNEL_2
#define UCPDDMA_INSTANCE0_CHANNEL_TX      DMA1_Channel2

// no fast role swap on a source-only port: PB14 (UCPD1_FRSTX) is unconnected
#define UCPDFRS_INSTANCE0_FRSCC1
#define UCPDFRS_INSTANCE0_FRSCC2

// above every other interrupt of the firmware: GoodCRC goes out from here
#define UCPD_INSTANCE0_ENABLEIRQ  do {                                 \
                                      NVIC_SetPriority(UCPD1_2_IRQn, 0); \
                                      NVIC_EnableIRQ(UCPD1_2_IRQn);      \
                                  } while (0)

// TIM1, free running at 1 MHz, its four compare channels as one-shot timers
#define TIMX                    TIM1
#define TIMX_CLK_ENABLE         LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_TIM1)
#define TIMX_CLK_DISABLE        LL_APB2_GRP1_DisableClock(LL_APB2_GRP1_PERIPH_TIM1)
#define TIMX_CHANNEL_CH1        LL_TIM_CHANNEL_CH1
#define TIMX_CHANNEL_CH2        LL_TIM_CHANNEL_CH2
#define TIMX_CHANNEL_CH3        LL_TIM_CHANNEL_CH3
#define TIMX_CHANNEL_CH4        LL_TIM_CHANNEL_CH4
#define TIMX_CHANNEL1_SETEVENT  do {                                                                    \
                                    LL_TIM_OC_SetCompareCH1(TIMX, (TimeUs + TIMX->CNT) % TIM_MAX_TIME); \
                                    LL_TIM_ClearFlag_CC1(TIMX);                                         \
                                } while (0)
#define TIMX_CHANNEL2_SETEVENT  do {                                                                    \
                                    LL_TIM_OC_SetCompareCH2(TIMX, (TimeUs + TIMX->CNT) % TIM_MAX_TIME); \
                                    LL_TIM_ClearFlag_CC2(TIMX);                                         \
                                } while (0)
#define TIMX_CHANNEL3_SETEVENT  do {                                                                    \
                                    LL_TIM_OC_SetCompareCH3(TIMX, (TimeUs + TIMX->CNT) % TIM_MAX_TIME); \
                                    LL_TIM_ClearFlag_CC3(TIMX);                                         \
                                } while (0)
#define TIMX_CHANNEL4_SETEVENT  do {                                                                    \
                                    LL_TIM_OC_SetCompareCH4(TIMX, (TimeUs + TIMX->CNT) % TIM_MAX_TIME); \
                                    LL_TIM_ClearFlag_CC4(TIMX);                                         \
                                } while (0)
#define TIMX_CHANNEL1_GETFLAG   LL_TIM_IsActiveFlag_CC1
#define TIMX_CHANNEL2_GETFLAG   LL_TIM_IsActiveFlag_CC2
#define TIMX_CHANNEL3_GETFLAG   LL_TIM_IsActiveFlag_CC3
#define TIMX_CHANNEL4_GETFLAG   LL_TIM_IsActiveFlag_CC4

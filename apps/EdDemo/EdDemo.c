/**
 * \file EdDemo.c
 *
 * \brief Energy Detection Demo application implementation
 *
 * Copyright (C) 2012-2014, Atmel Corporation. All rights reserved.
 *
 * \asf_license_start
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. The name of Atmel may not be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. This software may only be redistributed and used in connection with an
 *    Atmel microcontroller product.
 *
 * THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * EXPRESSLY AND SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \asf_license_stop
 *
 * Modification and other use of this code is subject to Atmel's Limited
 * License Agreement (license.txt).
 *
 * $Id: EdDemo.c 9267 2014-03-18 21:46:19Z ataradov $
 *
 */

/*- Includes ---------------------------------------------------------------*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "hal.h"
#include "phy.h"
#include "sys.h"
#include "nwk.h"
#include "sysTimer.h"
#include "halBoard.h"
#include "halUart.h"

/*- Types ------------------------------------------------------------------*/
typedef enum AppState_t
{
  APP_STATE_INITIAL,
  APP_STATE_MEASURE_ED,
  APP_STATE_WAIT_SCAN_TIMER,
} AppState_t;

/*- Variables --------------------------------------------------------------*/
static AppState_t appState = APP_STATE_INITIAL;
static uint8_t appEdValue[APP_LAST_CHANNEL - APP_FIRST_CHANNEL + 1];
static SYS_Timer_t appScanTimer;

/*- Implementations --------------------------------------------------------*/

/*************************************************************************//**
*****************************************************************************/
void HAL_UartBytesReceived(uint16_t bytes)
{
  for (uint16_t i = 0; i < bytes; i++)
    HAL_UartReadByte();
}

/*************************************************************************//**
*****************************************************************************/
static void appPrintEdValues(void)
{
  char hex[] = "0123456789abcdef";

  for (uint8_t i = 0; i < sizeof(appEdValue); i++)
  {
    uint8_t v = appEdValue[i] - PHY_RSSI_BASE_VAL;
    HAL_UartWriteByte(hex[(v >> 4) & 0x0f]);
    HAL_UartWriteByte(hex[v & 0x0f]);
    HAL_UartWriteByte(' ');
  }

  HAL_UartWriteByte('\r');
  HAL_UartWriteByte('\n');
}

/*************************************************************************//**
*****************************************************************************/
static void appScanTimerHandler(SYS_Timer_t *timer)
{
  appState = APP_STATE_MEASURE_ED;
  (void)timer;
}

/*************************************************************************//**
*****************************************************************************/
static void appInit(void)
{
  HAL_BoardInit();

  PHY_SetRxState(true);

  appScanTimer.interval = APP_SCAN_INTERVAL;
  appScanTimer.mode = SYS_TIMER_PERIODIC_MODE;
  appScanTimer.handler = appScanTimerHandler;
  SYS_TimerStart(&appScanTimer);

  appState = APP_STATE_MEASURE_ED;
}

/*************************************************************************//**
*****************************************************************************/
static void APP_TaskHandler(void)
{
  switch (appState)
  {
    case APP_STATE_INITIAL:
    {
      appInit();
    } break;

    case APP_STATE_MEASURE_ED:
    {
      for (uint8_t i = 0; i < sizeof(appEdValue); i++)
      {
        PHY_SetChannel(APP_FIRST_CHANNEL + i);
        appEdValue[i] = PHY_EdReq();
      }

      appPrintEdValues();

      appState = APP_STATE_WAIT_SCAN_TIMER;
    } break;

    default:
      break;
  }
}

/*************************************************************************//**
*****************************************************************************/
int main(void)
{
  SYS_Init();
  HAL_UartInit(38400);

  while (1)
  {
    SYS_TaskHandler();
    HAL_UartTaskHandler();
    APP_TaskHandler();
  }
}

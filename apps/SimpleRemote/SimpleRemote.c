/**
 * \file SimpleRemote.c
 *
 * \brief SimpleRemote application
 *
 * Copyright (C) 2012-2013, Atmel Corporation. All rights reserved.
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
 * $Id: SimpleRemote.c 9267 2014-03-18 21:46:19Z ataradov $
 *
 */

/*- Includes ---------------------------------------------------------------*/
#include "config.h"
#include "hal.h"
#include "phy.h"
#include "sys.h"
#include "nwk.h"
#include "halGpio.h"

/*- Definitions ------------------------------------------------------------*/
#if defined(PLATFORM_ZIGBIT)
  HAL_GPIO_PIN(LED, B, 5);
  HAL_GPIO_PIN(BUTTON, E, 6);

#elif defined(PLATFORM_ZIGBIT_X0)
  HAL_GPIO_PIN(LED, A, 5);
  HAL_GPIO_PIN(BUTTON, E, 5);

#elif defined(PLATFORM_RCB128RFA1) || defined(PLATFORM_RCB256RFR2)
  HAL_GPIO_PIN(LED, E, 2);
  HAL_GPIO_PIN(BUTTON, E, 5);

#elif defined(PLATFORM_XPLAINED_PRO_ATMEGA256RFR2)
  HAL_GPIO_PIN(LED, B, 4);
  HAL_GPIO_PIN(BUTTON, E, 4);

#elif defined(PLATFORM_XPLAINED_PRO_SAMD20_RZ600) || \
      defined(PLATFORM_XPLAINED_PRO_SAMD20_REB)
  HAL_GPIO_PIN(LED, A, 14);
  HAL_GPIO_PIN(BUTTON, A, 15);

#elif defined(PLATFORM_XPLAINED_PRO_SAMR21)
  HAL_GPIO_PIN(LED, A, 19);
  HAL_GPIO_PIN(BUTTON, A, 28);

#else
  #error Unsupported platform
#endif

/*- Types ------------------------------------------------------------------*/
typedef enum AppState_t
{
  APP_STATE_INITIAL,
  APP_STATE_IDLE,
  APP_STATE_WAIT_CONF,
} AppState_t;

typedef struct AppMessage_t
{
  uint8_t buttonState;
} AppMessage_t;

/*- Variables --------------------------------------------------------------*/
static AppState_t appState = APP_STATE_INITIAL;
static AppMessage_t appMessage;
static NWK_DataReq_t appNwkDataReq;
static bool appButtonState = false;

/*- Implementations --------------------------------------------------------*/

/*************************************************************************//**
*****************************************************************************/
static void appDataConf(NWK_DataReq_t *req)
{
  appState = APP_STATE_IDLE;
  (void)req;
}

/*************************************************************************//**
*****************************************************************************/
static void appSendMessage(uint8_t state)
{
  appMessage.buttonState = state;

  appNwkDataReq.dstAddr = 1 - APP_ADDR;
  appNwkDataReq.dstEndpoint = APP_ENDPOINT;
  appNwkDataReq.srcEndpoint = APP_ENDPOINT;
  appNwkDataReq.options = NWK_OPT_ACK_REQUEST;
  appNwkDataReq.data = (uint8_t *)&appMessage;
  appNwkDataReq.size = sizeof(appMessage);
  appNwkDataReq.confirm = appDataConf;

  NWK_DataReq(&appNwkDataReq);

  appState = APP_STATE_WAIT_CONF;
}

/*************************************************************************//**
*****************************************************************************/
static bool appDataInd(NWK_DataInd_t *ind)
{
  AppMessage_t *msg = (AppMessage_t *)ind->data;

  if (msg->buttonState)
    HAL_GPIO_LED_set();
  else
    HAL_GPIO_LED_clr();

  return true;
}

/*************************************************************************//**
*****************************************************************************/
static void APP_TaskHandler(void)
{
  switch (appState)
  {
    case APP_STATE_INITIAL:
    {
      // Hardware initialization
      HAL_GPIO_BUTTON_in();
      HAL_GPIO_BUTTON_pullup();

      HAL_GPIO_LED_out();
      HAL_GPIO_LED_set();

      // Network initialization
      NWK_SetAddr(APP_ADDR);
      NWK_SetPanId(APP_PANID);
      PHY_SetChannel(APP_CHANNEL);
      PHY_SetRxState(true);

      NWK_OpenEndpoint(APP_ENDPOINT, appDataInd);

      appState = APP_STATE_IDLE;
    } break;

    case APP_STATE_IDLE:
    {
      if (appButtonState != HAL_GPIO_BUTTON_read())
      {
        appButtonState = HAL_GPIO_BUTTON_read();
        appSendMessage(appButtonState);
      }
    } break;

    case APP_STATE_WAIT_CONF:
      break;
  }
}

/*************************************************************************//**
*****************************************************************************/
int main(void)
{
  SYS_Init();

  while (1)
  {
    SYS_TaskHandler();
    APP_TaskHandler();
  }
}

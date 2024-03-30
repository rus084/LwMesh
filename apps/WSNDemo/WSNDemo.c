/**
 * \file WSNDemo.c
 *
 * \brief WSNDemo application implementation
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
 * $Id: WSNDemo.c 9267 2014-03-18 21:46:19Z ataradov $
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
#include "nwkRoute.h"
#include "nwkSecurity.h"
#include "sysTimer.h"
#include "halUart.h"
#include "halSleep.h"
#include "halBoard.h"
#include "halLed.h"
#include "commands.h"

/*- Definitions ------------------------------------------------------------*/
#if defined(APP_COORDINATOR)
  #define APP_NODE_TYPE     0
#elif defined(APP_ROUTER)
  #define APP_NODE_TYPE     1
#else
  #define APP_NODE_TYPE     2
#endif

#define APP_CAPTION_SIZE    (sizeof(APP_CAPTION) - 1)
#define APP_COMMAND_PENDING 0x01

#define APP_ENDPOINT        1

#define APP_LED_NETWORK     0
#define APP_LED_DATA        1

/*- Types ------------------------------------------------------------------*/
typedef struct PACK
{
  uint8_t      commandId;
  uint8_t      nodeType;
  uint64_t     extAddr;
  uint16_t     shortAddr;
  uint32_t     softVersion;
  uint32_t     channelMask;
  uint16_t     panId;
  uint8_t      workingChannel;
  uint16_t     parentShortAddr;
  uint8_t      lqi;
  int8_t       rssi;

  struct PACK
  {
    uint8_t    type;
    uint8_t    size;
    int32_t    battery;
    int32_t    temperature;
    int32_t    light;
  } sensors;

  struct PACK
  {
    uint8_t    type;
    uint8_t    size;
    char       text[APP_CAPTION_SIZE];
  } caption;
} AppMessage_t;

typedef enum AppState_t
{
  APP_STATE_INITIAL,
  APP_STATE_SEND,
  APP_STATE_WAIT_CONF,
  APP_STATE_SENDING_DONE,
  APP_STATE_WAIT_SEND_TIMER,
  APP_STATE_WAIT_COMMAND_TIMER,
  APP_STATE_PREPARE_TO_SLEEP,
  APP_STATE_SLEEP,
  APP_STATE_WAKEUP,
} AppState_t;

/*- Variables --------------------------------------------------------------*/
static AppState_t appState = APP_STATE_INITIAL;

#if defined(APP_ROUTER) || defined(APP_ENDDEVICE)
static NWK_DataReq_t appNwkDataReq;
static SYS_Timer_t appNetworkStatusTimer;
static SYS_Timer_t appCommandWaitTimer;
static bool appNetworkStatus;
#endif

static AppMessage_t appMsg;
static SYS_Timer_t appDataSendingTimer;

/*- Implementations --------------------------------------------------------*/

/*************************************************************************//**
*****************************************************************************/
void HAL_UartBytesReceived(uint16_t bytes)
{
  for (uint16_t i = 0; i < bytes; i++)
  {
    uint8_t byte = HAL_UartReadByte();
    APP_CommandsByteReceived(byte);
  }
}

/*************************************************************************//**
*****************************************************************************/
static void appUartSendMessage(uint8_t *data, uint8_t size)
{
  uint8_t cs = 0;

  HAL_UartWriteByte(0x10);
  HAL_UartWriteByte(0x02);

  for (uint8_t i = 0; i < size; i++)
  {
    if (data[i] == 0x10)
    {
      HAL_UartWriteByte(0x10);
      cs += 0x10;
    }
    HAL_UartWriteByte(data[i]);
    cs += data[i];
  }

  HAL_UartWriteByte(0x10);
  HAL_UartWriteByte(0x03);
  cs += 0x10 + 0x02 + 0x10 + 0x03;

  HAL_UartWriteByte(cs);
}

/*************************************************************************//**
*****************************************************************************/
static bool appDataInd(NWK_DataInd_t *ind)
{
  AppMessage_t *msg = (AppMessage_t *)ind->data;

  HAL_LedToggle(APP_LED_DATA);

  msg->lqi = ind->lqi;
  msg->rssi = ind->rssi;

  appUartSendMessage(ind->data, ind->size);

  if (APP_CommandsPending(ind->srcAddr))
    NWK_SetAckControl(APP_COMMAND_PENDING);

  return true;
}

/*************************************************************************//**
*****************************************************************************/
static void appDataSendingTimerHandler(SYS_Timer_t *timer)
{
  if (APP_STATE_WAIT_SEND_TIMER == appState)
    appState = APP_STATE_SEND;
  else
    SYS_TimerStart(&appDataSendingTimer);

  (void)timer;
}

#if defined(APP_ROUTER) || defined(APP_ENDDEVICE)
/*************************************************************************//**
*****************************************************************************/
static void appNetworkStatusTimerHandler(SYS_Timer_t *timer)
{
  HAL_LedToggle(APP_LED_NETWORK);
  (void)timer;
}

/*************************************************************************//**
*****************************************************************************/
static void appCommandWaitTimerHandler(SYS_Timer_t *timer)
{
  appState = APP_STATE_SENDING_DONE;
  (void)timer;
}
#endif

/*************************************************************************//**
*****************************************************************************/
#if defined(APP_ROUTER) || defined(APP_ENDDEVICE)
static void appDataConf(NWK_DataReq_t *req)
{
  HAL_LedOff(APP_LED_DATA);

  if (NWK_SUCCESS_STATUS == req->status)
  {
    if (!appNetworkStatus)
    {
      HAL_LedOn(APP_LED_NETWORK);
      SYS_TimerStop(&appNetworkStatusTimer);
      appNetworkStatus = true;
    }
  }
  else
  {
    if (appNetworkStatus)
    {
      HAL_LedOff(APP_LED_NETWORK);
      SYS_TimerStart(&appNetworkStatusTimer);
      appNetworkStatus = false;
    }
  }

  if (APP_COMMAND_PENDING == req->control)
  {
    SYS_TimerStart(&appCommandWaitTimer);
    appState = APP_STATE_WAIT_COMMAND_TIMER;
  }
  else
  {
    appState = APP_STATE_SENDING_DONE;
  }
}
#endif

/*************************************************************************//**
*****************************************************************************/
static void appSendData(void)
{
#ifdef NWK_ENABLE_ROUTING
  appMsg.parentShortAddr = NWK_RouteNextHop(0, 0);
#else
  appMsg.parentShortAddr = 0;
#endif

  appMsg.sensors.battery     = rand() & 0xffff;
  appMsg.sensors.temperature = rand() & 0x7f;
  appMsg.sensors.light       = rand() & 0xff;

#if defined(APP_COORDINATOR)
  appUartSendMessage((uint8_t *)&appMsg, sizeof(appMsg));
  SYS_TimerStart(&appDataSendingTimer);
  appState = APP_STATE_WAIT_SEND_TIMER;
#else
  appNwkDataReq.dstAddr = 0;
  appNwkDataReq.dstEndpoint = APP_ENDPOINT;
  appNwkDataReq.srcEndpoint = APP_ENDPOINT;
  appNwkDataReq.options = NWK_OPT_ACK_REQUEST | NWK_OPT_ENABLE_SECURITY;
  appNwkDataReq.data = (uint8_t *)&appMsg;
  appNwkDataReq.size = sizeof(appMsg);
  appNwkDataReq.confirm = appDataConf;

  HAL_LedOn(APP_LED_DATA);
  NWK_DataReq(&appNwkDataReq);

  appState = APP_STATE_WAIT_CONF;
#endif
}

/*************************************************************************//**
*****************************************************************************/
static void appInit(void)
{
  appMsg.commandId            = APP_COMMAND_ID_NETWORK_INFO;
  appMsg.nodeType             = APP_NODE_TYPE;
  appMsg.extAddr              = APP_ADDR;
  appMsg.shortAddr            = APP_ADDR;
  appMsg.softVersion          = 0x01010100;
  appMsg.channelMask          = (1L << APP_CHANNEL);
  appMsg.panId                = APP_PANID;
  appMsg.workingChannel       = APP_CHANNEL;
  appMsg.parentShortAddr      = 0;
  appMsg.lqi                  = 0;
  appMsg.rssi                 = 0;

  appMsg.sensors.type        = 1;
  appMsg.sensors.size        = sizeof(int32_t) * 3;
  appMsg.sensors.battery     = 0;
  appMsg.sensors.temperature = 0;
  appMsg.sensors.light       = 0;

  appMsg.caption.type         = 32;
  appMsg.caption.size         = APP_CAPTION_SIZE;
  memcpy(appMsg.caption.text, APP_CAPTION, APP_CAPTION_SIZE);

  HAL_BoardInit();
  HAL_LedInit();

  NWK_SetAddr(APP_ADDR);
  NWK_SetPanId(APP_PANID);
  PHY_SetChannel(APP_CHANNEL);
#ifdef PHY_AT86RF212
  PHY_SetBand(APP_BAND);
  PHY_SetModulation(APP_MODULATION);
#endif
  PHY_SetRxState(true);

#ifdef NWK_ENABLE_SECURITY
  NWK_SetSecurityKey((uint8_t *)APP_SECURITY_KEY);
#endif

  NWK_OpenEndpoint(APP_ENDPOINT, appDataInd);

  appDataSendingTimer.interval = APP_SENDING_INTERVAL;
  appDataSendingTimer.mode = SYS_TIMER_INTERVAL_MODE;
  appDataSendingTimer.handler = appDataSendingTimerHandler;

#if defined(APP_ROUTER) || defined(APP_ENDDEVICE)
  appNetworkStatus = false;
  appNetworkStatusTimer.interval = 500;
  appNetworkStatusTimer.mode = SYS_TIMER_PERIODIC_MODE;
  appNetworkStatusTimer.handler = appNetworkStatusTimerHandler;
  SYS_TimerStart(&appNetworkStatusTimer);

  appCommandWaitTimer.interval = NWK_ACK_WAIT_TIME;
  appCommandWaitTimer.mode = SYS_TIMER_INTERVAL_MODE;
  appCommandWaitTimer.handler = appCommandWaitTimerHandler;
#else
  HAL_LedOn(APP_LED_NETWORK);
#endif

#ifdef PHY_ENABLE_RANDOM_NUMBER_GENERATOR
  srand(PHY_RandomReq());
#endif

  APP_CommandsInit();

  appState = APP_STATE_SEND;
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

    case APP_STATE_SEND:
    {
      appSendData();
    } break;

    case APP_STATE_SENDING_DONE:
    {
#if defined(APP_ENDDEVICE)
      appState = APP_STATE_PREPARE_TO_SLEEP;
#else
      SYS_TimerStart(&appDataSendingTimer);
      appState = APP_STATE_WAIT_SEND_TIMER;
#endif
    } break;

    case APP_STATE_PREPARE_TO_SLEEP:
    {
      if (!NWK_Busy())
      {
        NWK_SleepReq();
        appState = APP_STATE_SLEEP;
      }
    } break;

    case APP_STATE_SLEEP:
    {
      HAL_LedClose();
      HAL_Sleep(APP_SENDING_INTERVAL);
      appState = APP_STATE_WAKEUP;
    } break;

    case APP_STATE_WAKEUP:
    {
      NWK_WakeupReq();

      HAL_LedInit();
      HAL_LedOn(APP_LED_NETWORK);

      appState = APP_STATE_SEND;
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

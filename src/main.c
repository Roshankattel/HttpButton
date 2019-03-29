/*
 * Copyright (c) 2014-2018 Cesanta Software Limited
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mgos.h"
#include "mgos_adc.h"
#include "mgos_http_server.h"
#include "mgos_ro_vars.h"
#include "mgos_rpc.h"
#include "user_interface.h"
#include "mgos_gpio.h"

bool conn_closed = true;
mgos_timer_id led_timer_cb_id, retry_timer_cb_id;
int retryCount;

static void send_request();

static void led_timer_cb(void *arg)
{
  (void)arg;
  LOG(LL_INFO, ("*****Turing led off*****"));
  mgos_gpio_write(mgos_sys_config_get_hardware_led_pin(), false);
  mgos_clear_timer(led_timer_cb_id);
}

static void retry_timer_cb(void *arg)
{
  LOG(LL_INFO, ("*****sending request*****"));
  (void)arg;
  send_request();
  retryCount--;
  if (retryCount == 0)
  {
    retryCount = mgos_sys_config_get_hardware_retry_count();
    mgos_clear_timer(retry_timer_cb_id);
  }
}

void button_cb(int pin, void *arg)
{
  (void)arg;
  (void)pin;
  mgos_gpio_write(mgos_sys_config_get_hardware_led_pin(), false);
  LOG(LL_INFO, ("*****button Pressed*****"));
  send_request();
  retry_timer_cb_id = mgos_set_timer(mgos_sys_config_get_hardware_retry_interval(), MGOS_TIMER_REPEAT, retry_timer_cb, NULL);
}

static void ev_handler(struct mg_connection *c, int ev, void *ev_data, void *ud)
{
  (void)ud;
  struct http_message *hm = (struct http_message *)ev_data;

  switch (ev)
  {
  case MG_EV_CONNECT:
    conn_closed = false;
    break;
  case MG_EV_HTTP_REPLY:
    /* Only when we successfully got full reply, set the status. */
    if (hm->resp_code >= 200 && hm->resp_code < 299)
    {
      mgos_gpio_write(mgos_sys_config_get_hardware_led_pin(), true);
      led_timer_cb_id = mgos_set_timer(mgos_sys_config_get_hardware_led_ontime(), 0, led_timer_cb, NULL);
      mgos_clear_timer(retry_timer_cb_id);
    }
    struct http_message *hm = (struct http_message *)ev_data;
    c->flags |= MG_F_CLOSE_IMMEDIATELY;
    LOG(LL_INFO, ("Message is: %.*s", (int)hm->message.len, hm->message.p));
    LOG(LL_INFO, ("*****Finished fetching*****"));
    break;
  case MG_EV_CLOSE:
    LOG(LL_DEBUG, ("*****Closing Connection*****"));
    conn_closed = true;
    break;
  }
}

static void send_request()
{
  LOG(LL_INFO, ("%s - Sending Request! [%f secs]", mgos_sys_config_get_device_id(), mgos_uptime()));
  mgos_clear_timer(led_timer_cb_id);
  mgos_clear_timer(retry_timer_cb_id);
  retryCount = mgos_sys_config_get_hardware_retry_count();
  char json_msg[1000];
  struct json_out out = JSON_OUT_BUF(json_msg, sizeof(json_msg));
  json_printf(&out, "{group: %Q, subGroup: %Q, name: %Q, deviceId: %Q, message: %Q, battery: %d, sleepTimer: %d}",
              mgos_sys_config_get_app_location_group(),
              mgos_sys_config_get_app_location_sub_group(),
              mgos_sys_config_get_app_location_name(),
              mgos_sys_config_get_device_id(),
              "Attention Required", mgos_adc_read(0), mgos_sys_config_get_app_sleep_timer());

  if (mgos_wifi_get_status() == MGOS_WIFI_IP_ACQUIRED && conn_closed == true)
  {
    LOG(LL_INFO, ("%s - Wifi Connected, Sending Request! [%f secs]", mgos_sys_config_get_device_id(), mgos_uptime()));
    mg_connect_http(mgos_get_mgr(), ev_handler, NULL, mgos_sys_config_get_app_server(), "Content-Type: application/json\r\n", json_msg);
  }
}

static void init_hardware()
{
  mgos_gpio_set_button_handler(mgos_sys_config_get_hardware_callpin(), MGOS_GPIO_PULL_UP,
                               MGOS_GPIO_INT_EDGE_POS,
                               30, button_cb,
                               (void *)0);
  mgos_gpio_setup_output(mgos_sys_config_get_hardware_led_pin(), false);
}

static void mgos_config_get_mac_handler(struct mg_rpc_request_info *ri,
                                        void *cb_arg, struct mg_rpc_frame_info *fi,
                                        struct mg_str args)
{
  uint8_t ap_addr[6];
  uint8_t sta_addr[6];
  wifi_get_macaddr(SOFTAP_IF, ap_addr);
  wifi_get_macaddr(STATION_IF, sta_addr);

  char str1[20];
  char str2[20];
  sprintf(str1, "%02X.%02X.%02X.%02X.%02X.%02X", ap_addr[0], ap_addr[1], ap_addr[2], ap_addr[3], ap_addr[4], ap_addr[5]);
  sprintf(str2, "%02X.%02X.%02X.%02X.%02X.%02X", sta_addr[0], sta_addr[1], sta_addr[2], sta_addr[3], sta_addr[4], sta_addr[5]);

  mg_rpc_send_responsef(ri, "{macAddressAp: %Q, macAddress: %Q}", str1, str2);

  ri = NULL;
  (void)cb_arg;
  (void)args;
  (void)fi;
}

enum mgos_app_init_result mgos_app_init(void)
{
  LOG(LL_INFO, ("*****Application initialized started*****"));
  mg_rpc_add_handler(mgos_rpc_get_global(), "Config.GetMac", "", mgos_config_get_mac_handler, NULL);
  init_hardware();

  LOG(LL_INFO, ("*****Application initialized completed*****"));
  return MGOS_APP_INIT_SUCCESS;
}

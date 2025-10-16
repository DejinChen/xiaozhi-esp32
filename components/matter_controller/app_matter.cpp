/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <app/server/Server.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_ota.h>
#include <esp_rmaker_scenes.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_types.h>
#include <app_matter_controller.h>
#include <esp_matter_rainmaker.h>
#include <app_priv.h>
#include <esp_matter_controller_console.h>
#include <matter_controller_cluster.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <esp_ot_config.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#endif
#if CONFIG_CUSTOM_COMMISSIONABLE_DATA_PROVIDER
#include "dynamic_qrcode.h"
#include <esp_matter_providers.h>
#endif

#include "esp_rmaker_utils.h"

static const char *TAG = "app_main";

using namespace esp_matter;

bool rmaker_init_done = false;

/* Callback to handle commands received from the RainMaker cloud */
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                          const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (ctx) {
        ESP_LOGI(TAG, "Received write request via : %s", esp_rmaker_device_cb_src_to_str(ctx->src));
    }

    const char *param_name = esp_rmaker_param_get_name(param);
    if (strcmp(param_name, ESP_RMAKER_DEF_POWER_NAME) == 0) {
        app_matter_report_power(val.val.b);
    }
    esp_rmaker_param_update(param, val);
    return ESP_OK;
}

static bool initialized = false;

void start_esp_matter()
{
    if (initialized) {
        ESP_LOGW(TAG, "ESP Matter already initialized");
        return;
    }

    /* Initialize Matter */
    node::config_t node_config;
    node_t *matter_node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    if (!matter_node) {
        ESP_LOGE(TAG, "Matter node creation failed");
        abort();
    }

    // The data model for Matter Controller is the same as normal Matter end-devices. We should create root_node
    // endpoint for the controller to support network commissioning and other basic features of Matter.
    /* Add custom matter controller cluster */
    endpoint_t *root_endpoint = esp_matter::endpoint::get(matter_node, 0);
    esp_matter::cluster::matter_controller::create(root_endpoint, CLUSTER_FLAG_SERVER);

    esp_matter::rainmaker::init();

    app_matter_endpoint_create();

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif // CHIP_DEVICE_CONFIG_ENABLE_THREAD

#if CONFIG_CUSTOM_COMMISSIONABLE_DATA_PROVIDER
    esp_matter::set_custom_commissionable_data_provider(&DynamicPasscodeCommissionableDataProvider::GetInstance());
#endif

    /* Matter start */
    esp_err_t err = esp_matter::start(app_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Matter start failed: %d", err);
        abort();
    }

    /* Initialize the ESP RainMaker Agent.
     */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP RainMaker Device", "Controller");
    if (!node) {
        ESP_LOGE(TAG, "Could not initialise node.");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        abort();
    }

    esp_rmaker_device_t *device = esp_rmaker_device_create(CONTROLLER_DEVICE_NAME, ESP_RMAKER_DEVICE_SOCKET, NULL);
    if (device) {
        esp_rmaker_device_add_param(device,
                                    esp_rmaker_name_param_create(ESP_RMAKER_DEF_NAME_PARAM, CONTROLLER_DEVICE_NAME));
        esp_rmaker_param_t *primary = esp_rmaker_power_param_create(ESP_RMAKER_DEF_POWER_NAME, DEFAULT_POWER);
        esp_rmaker_device_add_param(device, primary);
        esp_rmaker_device_assign_primary_param(device, primary);
    }
    esp_rmaker_device_add_cb(device, write_cb, NULL);

    esp_rmaker_node_add_device(node, device);

    /* Enable timezone service which will be require for setting appropriate timezone
     * from the phone apps for scheduling to work correctly.
     * For more information on the various ways of setting timezone, please check
     * https://rainmaker.espressif.com/docs/time-service.html.
     */
    esp_rmaker_timezone_service_enable();

    /* Enable scheduling. */
    esp_rmaker_schedule_enable();

    /* Enable Scenes */
    esp_rmaker_scenes_enable();

    /* Enable Insights. Requires CONFIG_ESP_INSIGHTS_ENABLED=y */
    // app_insights_enable();

    /* Pre start */
    ESP_ERROR_CHECK(esp_matter::rainmaker::start());

    /* Start the ESP RainMaker Agent */
    esp_rmaker_start();
    rmaker_init_done = true;

    /* Enable Matter console*/
#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::init();
#if CONFIG_ESP_MATTER_CONTROLLER_ENABLE
    esp_matter::console::controller_register_commands();
#endif
#if CONFIG_OPENTHREAD_BORDER_ROUTER && CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER && CONFIG_OPENTHREAD_CLI
#endif
    initialized = true;
}

bool get_esp_matter_commissioning_state()
{
    return chip::Server::GetInstance().GetFabricTable().FabricCount() > 0;
}

bool esp_matter_factoryreset()
{
    // esp_rmaker_factory_reset(0, 10);
    return esp_matter::factory_reset() == ESP_OK;
}

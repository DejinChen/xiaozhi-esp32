/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_matter_controller_read_command.h"
#include "esp_matter_controller_utils.h"
#include <esp_check.h>
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include "app_esp_matter.h"
#include <app_matter_controller.h>
#include <app_priv.h>
#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_cluster_command.h>
#include <esp_matter_controller_subscribe_command.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <matter_controller_device_mgr.h>

using namespace esp_matter;
using namespace chip::app::Clusters;
using esp_matter::controller::device_mgr::matter_device_t;
using namespace chip::TLV;
using namespace esp_matter::controller;

static const char *TAG = "app_driver";
extern uint16_t app_endpoint_id;
static matter_device_t *s_device_list = NULL;
static matter_device_t *s_selected_device = NULL;
static uint16_t device_update_timer = 20;
static uint32_t min_interval = 0;
static uint32_t max_interval = 60;

struct light_state {
    bool onoff;
    bool is_subscribed = false;
    uint8_t current_level;
    uint8_t current_hue;
    uint8_t current_saturation;
};

static std::unordered_map<uint64_t, light_state> device_map;

static void attribute_data_cb(uint64_t remote_node_id, const chip::app::ConcreteDataAttributePath &path,
                              chip::TLV::TLVReader *data)
{
    ESP_LOGI(TAG, "subscribe attribute callback");
    switch (path.mClusterId) {
    case OnOff::Id: {
        switch (path.mAttributeId) {
        case OnOff::Attributes::OnOff::Id: {
            bool value;
            chip::app::DataModel::Decode(*data, value);
            device_map[remote_node_id].onoff = value;
            /* for online device, refresh the device on/off state */
            ESP_LOGE(TAG, "subscribe attribute onoff:%s", value ? "true" : "false");
        }
        default:
            break;
        }
        break;
    }
    case LevelControl::Id: {
        switch (path.mAttributeId) {
        case LevelControl::Attributes::CurrentLevel::Id: {
            uint8_t level;
            chip::app::DataModel::Decode(*data, level);
            device_map[remote_node_id].current_level = level;
            ESP_LOGE(TAG, "subscribe attribute current level:%d", level);
        }
        default:
            break;
        }
        break;
    }
    case ColorControl::Id: {
        switch (path.mAttributeId) {
        case ColorControl::Attributes::CurrentHue::Id: {
            uint8_t hue;
            chip::app::DataModel::Decode(*data, hue);
            device_map[remote_node_id].current_hue = hue;
            ESP_LOGE(TAG, "subscribe attribute current hue:%d", hue);
        } break;
        case ColorControl::Attributes::CurrentSaturation::Id: {
            uint8_t saturation;
            chip::app::DataModel::Decode(*data, saturation);
            device_map[remote_node_id].current_saturation = saturation;
            ESP_LOGE(TAG, "subscribe attribute current saturation:%d", saturation);
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

static void Layer_timer_cb(chip::System::Layer *aLayer, void *appState);

static void subscribe_failed_cb(void *subscribe_cmd, uint64_t remote_node_id)
{
    ESP_LOGE(TAG, "subscribe command failed for node 0x%" PRIX32 "%" PRIX32, (uint32_t)(remote_node_id >> 32),
             (uint32_t)(remote_node_id & 0xFFFFFFFF));
    device_map[remote_node_id].is_subscribed = false;
    chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds32(device_update_timer), Layer_timer_cb,
                                                nullptr);
}

static void subscribe_done_cb(uint64_t remote_node_id, uint32_t subscription_id)
{
    ESP_LOGI(TAG, "subscribe command done for node 0x%" PRIX32 "%" PRIX32 " subscription id: 0x%" PRIX32,
             (uint32_t)(remote_node_id >> 32), (uint32_t)(remote_node_id & 0xFFFFFFFF), subscription_id);
    device_map[remote_node_id].is_subscribed = false;
    chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds32(device_update_timer), Layer_timer_cb,
                                                nullptr);
}

static void Layer_timer_cb(chip::System::Layer *aLayer, void *appState)
{
    bool is_all_devices_subscribed = true;
    for (matter_device_t *device = s_selected_device; device; device = device->next) {
        if (device->endpoint_count != 1) {
            continue;
        }
        if (device->endpoints[0].device_type_id != ESP_MATTER_EXTENDED_COLOR_LIGHT_DEVICE_TYPE_ID) {
            continue;
        }
        if (device_map[device->node_id].is_subscribed) {
            continue;
        }
        is_all_devices_subscribed = false;
        device_map[device->node_id].is_subscribed = true;
        ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
        ScopedMemoryBufferWithSize<EventPathParams> event_paths;
        attr_paths.Alloc(4);
        if (!attr_paths.Get()) {
            ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
            return;
        }
        attr_paths[0] = AttributePathParams(1, 0x6, 0); // OnOff
        attr_paths[1] = AttributePathParams(1, 0x8, 0); // CurrentLevel
        attr_paths[2] = AttributePathParams(1, 0x300, 0); // CurrentHue
        attr_paths[3] = AttributePathParams(1, 0x300, 1); // CurrentSaturation
        esp_matter::controller::subscribe_command *cmd = chip::Platform::New<subscribe_command>(
            device->node_id, std::move(attr_paths), std::move(event_paths), min_interval, max_interval, true,
            attribute_data_cb, nullptr, subscribe_done_cb, subscribe_failed_cb);

        if (!cmd) {
            ESP_LOGE(TAG, "Failed to alloc memory for subscribe-rainmaker-device command");
            return;
        }

        cmd->send_command();
    }
    if (is_all_devices_subscribed) {
        ESP_LOGI(TAG, "All devices subscribed");
    } else {
        chip::DeviceLayer::SystemLayer().ScheduleLambda([]() {
            chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds32(device_update_timer),
                                                        Layer_timer_cb, nullptr);
        });
    }
}

void on_device_list_update()
{
    if (s_device_list) {
        esp_matter::controller::device_mgr::free_device_list(s_device_list);
    }
    s_device_list = esp_matter::controller::device_mgr::get_device_list_clone();
    s_selected_device = s_device_list;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([]() {
        chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds32(5), Layer_timer_cb,
                                                    nullptr);
    });
}

matter_device_t *esp_matter_get_device_list()
{
    return s_device_list;
}

typedef struct cluster_comand {
    uint64_t node_id_;
    uint16_t endpoint_id_;
    uint32_t cluster_id_;
    uint32_t command_id_;
    uint32_t attribute_id_;
    std::string command_args_;
    std::string endpoint_ids_;
    std::string cluster_ids_;
    std::string attribute_ids_;
    std::function<void(std::string &str)> success_callback_;
    std::function<void(std::string &str)> error_callback_;
    cluster_comand(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t command_id,
                   std::string &command_args, std::function<void(std::string &str)> success_callback,
                   std::function<void(std::string &str)> error_callback)
        : node_id_(node_id)
        , endpoint_id_(endpoint_id)
        , cluster_id_(cluster_id)
        , command_id_(command_id)
        , command_args_(command_args)
        , success_callback_(success_callback)
        , error_callback_(error_callback)
    {}

    cluster_comand(uint64_t node_id, uint32_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                   std::function<void(std::string &str)> callback)
        : node_id_(node_id)
        , endpoint_id_(endpoint_id)
        , cluster_id_(cluster_id)
        , attribute_id_(attribute_id)
        , success_callback_(callback)
    {}
} cluster_comand_t;

static void send_cluster_command_cb(intptr_t arg)
{
    cluster_comand_t *ptr = (cluster_comand_t *)arg;

    ESP_LOGE(TAG,
             "node id 0x%" PRIX32 "%" PRIX32 " endpoint id:%d cluster id: %" PRIX32 " command: %" PRIX32 " args:%s",
             (uint32_t)(ptr->node_id_ >> 32), (uint32_t)(ptr->node_id_ & 0xFFFFFFFF), ptr->endpoint_id_,
             ptr->cluster_id_, ptr->command_id_, ptr->command_args_.c_str());
    if (ptr) {
        esp_matter::controller::send_invoke_cluster_command(ptr->node_id_, ptr->endpoint_id_, ptr->cluster_id_,
                                                            ptr->command_id_, ptr->command_args_.c_str());

        cluster_command *cmd = chip::Platform::New<cluster_command>(
            ptr->node_id_, ptr->endpoint_id_, ptr->cluster_id_, ptr->command_id_, ptr->command_args_.c_str(),
            chip::NullOptional,
            [ptr](void *, const ConcreteCommandPath &, const StatusIB &, TLVReader *) -> void {
                std::string str("exec success");
                ESP_LOGI(TAG, "command exec success");
                ptr->success_callback_(str);
                delete ptr;
            },
            [ptr](void *, CHIP_ERROR error) -> void {
                std::string str("command exec error");
                ESP_LOGI(TAG, "command exec error");
                ptr->error_callback_(str);
                delete ptr;
            });
        if (!cmd) {
            ESP_LOGE(TAG, "Failed to alloc memory for cluster_command");
            return;
        }

        cmd->send_command();
    }
}

namespace esp_matter {
extern esp_err_t string_to_uint16_array(const char *str, ScopedMemoryBufferWithSize<uint16_t> &uint16_array);
extern esp_err_t string_to_uint32_array(const char *str, ScopedMemoryBufferWithSize<uint32_t> &uint32_array);
} // namespace esp_matter

static void read_cluster_attribute_cb(intptr_t arg)
{
    cluster_comand_t *ptr = (cluster_comand_t *)arg;

    if (ptr) {
        ESP_LOGE(TAG,
                 "node id 0x%" PRIx32 "%" PRIx32 " endpoint_ids:%d cluster_ids:0x%" PRIx32 " attribute_ids:0x%" PRIx32,
                 (uint32_t)(ptr->node_id_ >> 32), (uint32_t)(ptr->node_id_ & 0xFFFFFFFF), ptr->endpoint_id_,
                 ptr->cluster_id_, ptr->attribute_id_);

        ScopedMemoryBufferWithSize<uint16_t> endpoint_ids;
        ScopedMemoryBufferWithSize<uint32_t> cluster_ids;
        ScopedMemoryBufferWithSize<uint32_t> attribute_ids;

        endpoint_ids.Alloc(1);
        cluster_ids.Alloc(1);
        attribute_ids.Alloc(1);
        if (!(endpoint_ids.Get() && cluster_ids.Get() && attribute_ids.Get())) {
            ESP_LOGE(TAG, "Failed to alloc memory for endpoint path");
            return;
        } else {
            *endpoint_ids.Get() = ptr->endpoint_id_;
            *cluster_ids.Get() = ptr->cluster_id_;
            *attribute_ids.Get() = ptr->attribute_id_;
        }

        ScopedMemoryBufferWithSize<AttributePathParams> attr_paths;
        ScopedMemoryBufferWithSize<EventPathParams> event_paths;
        attr_paths.Alloc(endpoint_ids.AllocatedSize());
        if (!attr_paths.Get()) {
            ESP_LOGE(TAG, "Failed to alloc memory for attribute paths");
            return;
        }
        for (size_t i = 0; i < attr_paths.AllocatedSize(); ++i) {
            attr_paths[i] = AttributePathParams(endpoint_ids[i], cluster_ids[i], attribute_ids[i]);
        }

        controller::read_command *cmd = chip::Platform::New<controller::read_command>(
            ptr->node_id_, std::move(attr_paths), std::move(event_paths),
            [](uint64_t remote_node_id, const chip::app::ConcreteDataAttributePath &path,
               chip::TLV::TLVReader *data) -> void {
                if (!data) {
                    return;
                }
                char attribute_buf[128];
                snprintf(attribute_buf, sizeof(attribute_buf),
                         "\"node_id\":%llx,\"cluster_id\":%lu,\"attribute_id\":%lu,", remote_node_id, path.mClusterId,
                         path.mAttributeId);
                TLVType type = data->GetType();

                switch (type) {
                case kTLVType_SignedInteger: {
                    int32_t value;
                    chip::app::DataModel::Decode(*data, value);
                    snprintf(attribute_buf + strlen(attribute_buf), sizeof(attribute_buf) - strlen(attribute_buf),
                             "\"type\":\"int\",\"value\":%ld", value);
                } break;
                case kTLVType_UnsignedInteger: {
                    uint32_t value;
                    chip::app::DataModel::Decode(*data, value);
                    snprintf(attribute_buf + strlen(attribute_buf), sizeof(attribute_buf) - strlen(attribute_buf),
                             "\"type\":\"uint\",\"value\":%lu", value);
                } break;
                case kTLVType_Boolean: {
                    bool value;
                    chip::app::DataModel::Decode(*data, value);
                    snprintf(attribute_buf + strlen(attribute_buf), sizeof(attribute_buf) - strlen(attribute_buf),
                             "\"type\":\"bool\",\"value\":%s", value ? "\"true\"" : "\"false\"");
                } break;
                default:
                    break;
                }
                // ESP_LOGI(TAG, "Attribute results:%s", attribute_buf);
                // TODO: report the attribute results to server
            },
            [](uint64_t remote_node_id, const ScopedMemoryBufferWithSize<AttributePathParams> &attr_paths,
               const ScopedMemoryBufferWithSize<EventPathParams> &EventPathParams) -> void {},
            nullptr);
        if (!cmd) {
            ESP_LOGE(TAG, "Failed to alloc memory for read_command");
            return;
        }
        cmd->send_command();
    }

    delete ptr;
}

CHIP_ERROR send_cluster_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t command_id,
                                std::string &command_args, std::function<void(std::string &str)> success_callback,
                                std::function<void(std::string &str)> error_callback)
{
    cluster_comand_t *command_arg = new cluster_comand_t(node_id, endpoint_id, cluster_id, command_id, command_args,
                                                         success_callback, error_callback);
    return chip::DeviceLayer::PlatformMgr().ScheduleWork(send_cluster_command_cb, (intptr_t)command_arg);
}

CHIP_ERROR read_cluster_attribute(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                  std::function<void(std::string &str)> callback)
{
    cluster_comand_t *command_arg = new cluster_comand_t(node_id, endpoint_id, cluster_id, attribute_id, callback);
    return chip::DeviceLayer::PlatformMgr().ScheduleWork(read_cluster_attribute_cb, (intptr_t)command_arg);
}

static bool check_device_in_list(uint64_t node_id)
{
    for (matter_device_t *device = s_device_list; device; device = device->next) {
        if (device->node_id == node_id) {
            return true;
        }
    }
    return false;
}

CHIP_ERROR send_light_onoff_command(uint64_t node_id, const std::string &action)
{
    if (!s_selected_device) {
        return CHIP_ERROR_INTERNAL;
    }

    if (!check_device_in_list(node_id)) {
        ESP_LOGE(TAG, "Device 0x%" PRIx64 " not found in device list", node_id);
        return CHIP_ERROR_NOT_FOUND;
    }

    chip::CommandId command_id = OnOff::Commands::Toggle::Id;
    if (action == "On") {
        command_id = OnOff::Commands::On::Id;
    } else if (action == "Off") {
        command_id = OnOff::Commands::Off::Id;
    }

    ESP_LOGE(TAG, "light 0x%" PRIx64 " action:%s", node_id, action.c_str());

    uint32_t node_id_upper = (uint32_t)(node_id >> 32);
    uint32_t node_id_lower = (uint32_t)(node_id & 0xFFFFFFFF);

    return chip::DeviceLayer::SystemLayer().ScheduleLambda([node_id_upper, node_id_lower, command_id]() {
        esp_matter::controller::send_invoke_cluster_command(uint64_t(node_id_upper) << 32 | node_id_lower, 1, OnOff::Id,
                                                            command_id, NULL);
    });
}

CHIP_ERROR send_light_level_command(uint64_t node_id, uint8_t level)
{
    if (!s_selected_device) {
        return CHIP_ERROR_INTERNAL;
    }

    if (!check_device_in_list(node_id)) {
        ESP_LOGE(TAG, "Device 0x%" PRIx64 " not found in device list", node_id);
        return CHIP_ERROR_NOT_FOUND;
    }

    ESP_LOGE(TAG, "light 0x%" PRIx64 " level:%d", node_id, level);

    uint32_t node_id_upper = (uint32_t)(node_id >> 32);
    uint32_t node_id_lower = (uint32_t)(node_id & 0xFFFFFFFF);

    return chip::DeviceLayer::SystemLayer().ScheduleLambda([node_id_upper, node_id_lower, level]() {
        char command_arg[128];
        snprintf(command_arg, sizeof(command_arg), "{\"0:U32\": %d,\"1:U32\":10,\"2:U32\":0,\"3:U32\":0}", level);
        esp_matter::controller::send_invoke_cluster_command(
            uint64_t(node_id_upper) << 32 | node_id_lower, 1, LevelControl::Id,
            LevelControl::Commands::MoveToLevelWithOnOff::Id, command_arg);
    });
}

CHIP_ERROR send_light_hue_command(uint64_t node_id, uint8_t hue)
{
    if (!s_selected_device) {
        return CHIP_ERROR_INTERNAL;
    }

    if (!check_device_in_list(node_id)) {
        ESP_LOGE(TAG, "Device 0x%" PRIx64 " not found in device list", node_id);
        return CHIP_ERROR_NOT_FOUND;
    }

    ESP_LOGE(TAG, "light 0x%" PRIx64 " hue:%d", node_id, hue);

    uint32_t node_id_upper = (uint32_t)(node_id >> 32);
    uint32_t node_id_lower = (uint32_t)(node_id & 0xFFFFFFFF);

    return chip::DeviceLayer::SystemLayer().ScheduleLambda([node_id_upper, node_id_lower, hue]() {
        char command_arg[128];
        snprintf(command_arg, sizeof(command_arg), "{\"0:U8\":%d,\"1:U8\":0,\"2:U16\":0,\"3:U8\":0,\"4:U8\":0}",
                 hue);
        esp_matter::controller::send_invoke_cluster_command(uint64_t(node_id_upper) << 32 | node_id_lower, 1,
                                                            ColorControl::Id, ColorControl::Commands::MoveToHue::Id,
                                                            command_arg);
    });
}

CHIP_ERROR send_light_saturation_command(uint64_t node_id, uint8_t saturation)
{
    if (!s_selected_device) {
        return CHIP_ERROR_INTERNAL;
    }

    if (!check_device_in_list(node_id)) {
        ESP_LOGE(TAG, "Device 0x%" PRIx64 " not found in device list", node_id);
        return CHIP_ERROR_NOT_FOUND;
    }

    ESP_LOGE(TAG, "light 0x%" PRIx64 " saturation:%d", node_id, saturation);

    uint32_t node_id_upper = (uint32_t)(node_id >> 32);
    uint32_t node_id_lower = (uint32_t)(node_id & 0xFFFFFFFF);

    return chip::DeviceLayer::SystemLayer().ScheduleLambda([node_id_upper, node_id_lower, saturation]() {
        char command_arg[128];
        snprintf(command_arg, sizeof(command_arg), "{\"0:U8\":%d,\"1:U16\":0,\"2:U8\":0,\"3:U8\":0}", saturation);
        esp_matter::controller::send_invoke_cluster_command(uint64_t(node_id_upper) << 32 | node_id_lower, 1,
                                                            ColorControl::Id,
                                                            ColorControl::Commands::MoveToSaturation::Id, command_arg);
    });
}

std::string get_light_state_command(uint64_t node_id)
{
    ESP_LOGE(TAG, "get light 0x%" PRIx64 " state", node_id);
    if (!s_selected_device) {
        return std::string("No device");
    }

    auto it = device_map.find(node_id);
    if (it == device_map.end()) {
        return std::string("Device state not found");
    }

    const light_state &state = it->second;
    std::string state_str = "Light State:\n";
    state_str += "On/Off: " + std::string(state.onoff ? "On" : "Off") + "\n";
    state_str += "Current Level: " +
        std::to_string(REMAP_TO_RANGE((uint16_t)state.current_level, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS)) + "\n";
    state_str += "Current Hue: " + std::to_string(REMAP_TO_RANGE(state.current_hue, MATTER_HUE, STANDARD_HUE)) + "\n";
    state_str += "Current Saturation: " +
        std::to_string(REMAP_TO_RANGE(state.current_saturation, MATTER_SATURATION, STANDARD_SATURATION)) + "\n";

    ESP_LOGI(TAG, "Light state: %s", state_str.c_str());
    return state_str;
}

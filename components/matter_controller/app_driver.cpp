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

#include <app_matter_controller.h>
#include <app_priv.h>
#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_cluster_command.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <matter_controller_device_mgr.h>

using namespace esp_matter;
using namespace chip::app::Clusters;
using esp_matter::controller::device_mgr::matter_device_t;
using namespace chip::TLV;

static const char *TAG = "app_driver";
extern uint16_t app_endpoint_id;
static matter_device_t *s_device_list = NULL;
static matter_device_t *s_selected_device = NULL;

void on_device_list_update()
{
    if (s_device_list) {
        esp_matter::controller::device_mgr::free_device_list(s_device_list);
    }
    s_device_list = esp_matter::controller::device_mgr::get_device_list_clone();
    s_selected_device = s_device_list;
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
    std::function<void(std::string &str)> _callback;
    cluster_comand(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t command_id,
                   std::string &command_args)
        : node_id_(node_id)
        , endpoint_id_(endpoint_id)
        , cluster_id_(cluster_id)
        , command_id_(command_id)
        , command_args_(command_args)
    {}

    cluster_comand(uint64_t node_id, uint32_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                   std::function<void(std::string &str)> callback)
        : node_id_(node_id)
        , endpoint_id_(endpoint_id)
        , cluster_id_(cluster_id)
        , attribute_id_(attribute_id)
        , _callback(callback)
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
    }

    delete ptr;
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
                                std::string &command_args)
{
    cluster_comand_t *command_arg = new cluster_comand_t(node_id, endpoint_id, cluster_id, command_id, command_args);
    return chip::DeviceLayer::PlatformMgr().ScheduleWork(send_cluster_command_cb, (intptr_t)command_arg);
}

CHIP_ERROR read_cluster_attribute(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                  std::function<void(std::string &str)> callback)
{
    cluster_comand_t *command_arg = new cluster_comand_t(node_id, endpoint_id, cluster_id, attribute_id, callback);
    return chip::DeviceLayer::PlatformMgr().ScheduleWork(read_cluster_attribute_cb, (intptr_t)command_arg);
}

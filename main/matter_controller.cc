/*
 *
 */

#include "matter_controller.h"
#include "app_esp_matter.h"
#include "application.h"
#include "board.h"
#include "display.h"
#include "esp_matter_controller_cluster_command.h"
#include "esp_matter_endpoint.h"
#include "esp_pthread.h"
#include "matter_controller_device_mgr.h"

#if CONFIG_CUSTOM_COMMISSIONABLE_DATA_PROVIDER
#include "dynamic_qrcode.h"
#endif

#include <esp_log.h>

#define TAG "MatterController"

using namespace chip;
using namespace chip::app::Clusters;

lv_img_dsc_t *MatterController::GetCommissioningQrcode()
{
    DisplayLockGuard lock(Board::GetInstance().GetDisplay());
    lv_obj_t *qrcode_obj = lv_qrcode_create(NULL);
    if (qrcode_obj == NULL) {
        return NULL;
    }
    lv_qrcode_set_size(qrcode_obj, 108);
    lv_obj_align(qrcode_obj, LV_ALIGN_CENTER, 0, 0);
    const char *qrcode_data = DynamicPasscodeCommissionableDataProvider::GetInstance().GetDynamicQRcodeStr();
    lv_qrcode_update(qrcode_obj, qrcode_data, strlen(qrcode_data));
    ESP_LOGI(TAG, "QR Data: %s", qrcode_data);

    return lv_canvas_get_image(qrcode_obj);
}

esp_err_t MatterController::Initialize()
{
    ESP_LOGI(TAG, "Initializing Matter Controller...");
    start_esp_matter();

    McpServer::GetInstance().AddTool(
        "self.matter.get_commissioning_state",
        "Get the commissioning state of the matter controller. If the current state is 'not commissioned', you can "
        "call `self.matter.start_commissioning` to start the commissioning process.\n"
        "Return:\n A string represents the controller state.",
        PropertyList(), [](const PropertyList &properties) -> ReturnValue {
            ESP_LOGI(TAG, "Get Controller commissioning state");
            if (!get_esp_matter_commissioning_state()) {
                return ReturnValue(std::string("Not commissioned"));
            }
            return ReturnValue(std::string("Has been commissioned"));
        });

    McpServer::GetInstance().AddTool(
        "self.matter.start_commissioning",
        "If matter controller is not commissioned, call this to start the commissioning process. ", PropertyList(),
        [this](const PropertyList &properties) -> ReturnValue {
            start_esp_matter();
            if (!get_esp_matter_commissioning_state()) {
                auto display = Board::GetInstance().GetDisplay();
                if (display != nullptr) {
                    display->SetPreviewImage(GetCommissioningQrcode());
                }
                return ReturnValue(std::string("Scan the QRcode to commissioning device"));
            }
            return ReturnValue(std::string("Has been commissioned"));
        });

    McpServer::GetInstance().AddTool("self.matter.factory_reset", "Set the matter controller to factory reset state.",
                                     PropertyList(), [](const PropertyList &properties) -> ReturnValue {
                                         esp_matter_factoryreset();
                                         return ReturnValue(std::string("success"));
                                     });

    McpServer::GetInstance().AddTool(
        "self.matter.get_device",
        "If has been commissioned, you can call `self.matter.get_device` to get the list of "
        "devices can be controlled, such light, plug, thermostat, etc.\n"
        "Return:\n A JSON object that provides the devices that can be control.",
        PropertyList(), [](const PropertyList &properties) -> ReturnValue {
            matter_device_t *device_list = esp_matter_get_device_list();
            if (!device_list) {
                esp_matter::controller::device_mgr::update_device_list(0);
                ESP_LOGE(TAG, "No device found");
                return ReturnValue(std::string("No device found"));
            }
            matter_device_t *current = device_list;

            MatterDeviceList::MatterDevice::MatterCluster onoff_cluster(app::Clusters::OnOff::Id);
            onoff_cluster.AddCommand(OnOff::Commands::Off::Id, std::string("TurnOff"))
                .AddCommand(OnOff::Commands::On::Id, std::string("TurnOn"))
                .AddCommand(OnOff::Commands::Toggle::Id, std::string("Toggle"));
            onoff_cluster.AddAttribute(OnOff::Attributes::OnOff::Id, std::string("OnOff"));

            MatterDeviceList matter_device_list;
            while (current) {
                if (current->endpoint_count != 1) {
                    current = current->next;
                    continue;
                }

                switch (current->endpoints[0].device_type_id) {
                case ESP_MATTER_ON_OFF_LIGHT_DEVICE_TYPE_ID: {
                    matter_device_list.AddDevice(current->endpoints[0].device_name, "On/OffLight", current->node_id, 1);
                    matter_device_list.AddClusterToDevice(current->node_id, 1, onoff_cluster);
                } break;
                case ESP_MATTER_EXTENDED_COLOR_LIGHT_DEVICE_TYPE_ID: {
                    matter_device_list.AddDevice(current->endpoints[0].device_name, "ColorLight", current->node_id, 1);
                    MatterDeviceList::MatterDevice::MatterCluster level_cluster(app::Clusters::LevelControl::Id);
                    MatterDeviceList::MatterDevice::MatterCommand move_to_level_command(
                        LevelControl::Commands::MoveToLevel::Id, std::string("MoveToLevel"));

                    Property level_arg(std::string("Level"), kPropertyTypeUnsignedInteger, 0, 254);
                    Property transition_time_arg(std::string("TransitionTime(ms)"), kPropertyTypeUnsignedInteger,
                                                 (uint32_t)0, 0, 65535);
                    Property options_mask_arg(std::string("OptionsMask"), kPropertyTypeUnsignedInteger, (uint32_t)0);
                    Property options_override_arg(std::string("OptionsOverride"), kPropertyTypeUnsignedInteger,
                                                  (uint32_t)0);
                    move_to_level_command.AddCommandArg(level_arg)
                        .AddCommandArg(transition_time_arg)
                        .AddCommandArg(options_mask_arg)
                        .AddCommandArg(options_override_arg);

                    level_cluster.AddCommand(move_to_level_command);
                    level_cluster.AddAttribute(LevelControl::Attributes::CurrentLevel::Id, std::string("CurrentLevel"));

                    matter_device_list.AddClusterToDevice(current->node_id, 1, level_cluster);
                    matter_device_list.AddClusterToDevice(current->node_id, 1, onoff_cluster);
                } break;
                case ESP_MATTER_ON_OFF_PLUGIN_UNIT_DEVICE_TYPE_ID:
                    matter_device_list.AddDevice(current->endpoints[0].device_name, "On/OffPlug", current->node_id, 1);
                    matter_device_list.AddClusterToDevice(current->node_id, 1, onoff_cluster);
                    break;
                case ESP_MATTER_THERMOSTAT_DEVICE_TYPE_ID: {
                    matter_device_list.AddDevice(current->endpoints[0].device_name, "Thermostat", current->node_id, 1);
                    MatterDeviceList::MatterDevice::MatterCluster thermostat_cluster(app::Clusters::Thermostat::Id);
                    thermostat_cluster
                        .AddAttribute(Thermostat::Attributes::UnoccupiedCoolingSetpoint::Id,
                                      std::string("CoolingSetpoint"))
                        .AddAttribute(Thermostat::Attributes::UnoccupiedHeatingSetpoint::Id,
                                      std::string("HeatingSetpoint"));
                    MatterDeviceList::MatterDevice::MatterCommand setpoint_raise_lower_command(
                        Thermostat::Commands::SetpointRaiseLower::Id, std::string("SetpointRaiseOrLower"));

                    setpoint_raise_lower_command
                        .AddCommandArg(
                            Property(std::string("Mode (0)"), kPropertyTypeUnsignedInteger, (uint32_t)0, 0, 0))
                        .AddCommandArg(Property(std::string("Amount (amount 1 for 0.1 celsius degree)"),
                                                kPropertyTypeInteger, -127, 127));
                    thermostat_cluster.AddCommand(setpoint_raise_lower_command);
                    thermostat_cluster.AddAttribute(Thermostat::Attributes::LocalTemperature::Id,
                                                    std::string("LocalTemperature"));
                    matter_device_list.AddClusterToDevice(current->node_id, 1, thermostat_cluster);
                } break;
                default:
                    ESP_LOGW(TAG, "Unknown device type: 0x%04" PRIX32, current->endpoints[0].device_type_id);
                    break;
                }
                // ESP_LOGI(TAG, "Device name:%s type :%" PRIX32 " node id: 0x%" PRIX32 "%" PRIX32,
                //          current->endpoints[0].device_name, current->endpoints[0].device_type_id,
                //          (uint32_t)(current->node_id >> 32), (uint32_t)(current->node_id & 0xFFFFFFFF));
                current = current->next;
            }
            ESP_LOGI(TAG, "devices %s", matter_device_list.to_json().c_str());
            return ReturnValue(matter_device_list.to_json());
        });

    McpServer::GetInstance().AddTool(
        "self.matter.control_device",
        "Control a matter device, the device MUST comes from device list (self.matter.get_device)\n"
        "Args:\n"
        "`node_id`, `endpoint_id`, `cluster_id`, `command_id`, `command_args`:\n"
        "  - If there are no arguments, `command_args` should be \"{}\".\n"
        "  - Otherwise, it should be a JSON object string.\n"
        "    Each key must be in the format \"<TagNumber>:<DataType>\" where:\n"
        "      - TagNumber indicates the argument order (e.g., 0 for the first argument).\n"
        "      - DataType specifies the value type:\n"
        "          - 'I32' for integer\n"
        "          - 'U32' for unsigned integer\n"
        "          - 'STR' for string\n"
        "Example:\n"
        "For the `move_to_level` command, `command_args` should be:\n"
        "\"{\"0:U32\": 10, \"1:U32\": 0, \"2:U32\": 0, \"3:U32\": 0}\","
        "Or `command_args` can refer to "
        "https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html#cluster-commands. \n"
        "Return:\n"
        "A string indicating command sending state. Note that the actual execution may take some time.\n",
        PropertyList({Property("node_id", kPropertyTypeString), Property("endpoint_id", kPropertyTypeUnsignedInteger),
                      Property("cluster_id", kPropertyTypeUnsignedInteger),
                      Property("command_id", kPropertyTypeUnsignedInteger),
                      Property("command_args", kPropertyTypeString)}),
        [](const PropertyList &properties) -> ReturnValue {
            uint64_t node_id = std::stoull(properties["node_id"].value<std::string>(), nullptr, 16);
            std::string command_args(properties["command_args"].value<std::string>());
            return send_cluster_command(
                       node_id, properties["endpoint_id"].value<uint32_t>(), properties["cluster_id"].value<uint32_t>(),
                       properties["command_id"].value<uint32_t>(), command_args,
                       [](std::string &str) {
                           // Application::GetInstance().WakeWordInvoke(str);
                       },
                       [](std::string &str) {
                           // Application::GetInstance().WakeWordInvoke(str);
                       }) == CHIP_NO_ERROR
                ? ReturnValue(std::string("Have sent command, waiting execution"))
                : ReturnValue(std::string("failed"));
        });

    // /* method: notifications is not implemented in xiaozhi-server */
    // McpServer::GetInstance().AddTool(
    //     "self.matter_controller.read_device_attribute",
    //     "Read an attribute from a device in the device list.\n"
    //     "Before calling this method, use `self.matter_controller.get_device_list` to retrieve the available
    //     devices.\n" "After executing `control_device`, you can use this method to check the corresponding attribute
    //     state.\n\n" "Args:\n"
    //     "- `node_id`, `endpoint_id`, `cluster_id`, `attribute_id`: Must match the values from `device_list`.\n\n"
    //     "Returns:\n"
    //     "A string indicating whether the command was sent successfully. The actual attribute value will be delivered
    //     " "later via a notification.\n", PropertyList({Property("node_id", kPropertyTypeString),
    //     Property("endpoint_id", kPropertyTypeUnsignedInteger),
    //                   Property("cluster_id", kPropertyTypeUnsignedInteger),
    //                   Property("attribute_id", kPropertyTypeUnsignedInteger)}),
    //     [](const PropertyList &properties) -> ReturnValue {
    //         uint64_t node_id = std::stoull(properties["node_id"].value<std::string>(), nullptr, 16);
    //         return read_cluster_attribute(node_id, properties["endpoint_id"].value<uint32_t>(),
    //                                       properties["cluster_id"].value<uint32_t>(),
    //                                       properties["attribute_id"].value<uint32_t>(),
    //                                       [](std::string &str) -> void {
    //                                           std::string payload = "{\"jsonrpc\":\"2.0\",";
    //                                           payload += "\"method\":\"notifications/state_changed\",";
    //                                           payload += "\"params\":{";
    //                                           payload += str;
    //                                           payload += "}}";
    //                                           ESP_LOGI(TAG, "MCP results: %s", payload.c_str());
    //                                           Application::GetInstance().SendMcpMessage(payload);
    //                                       }) == CHIP_NO_ERROR
    //             ? ReturnValue(std::string("Have sent read command"))
    //             : ReturnValue(std::string("failed"));
    //     });

    return ESP_OK;
}

/*
 *
 */

#include "lvgl.h"
#include "lvgl_image.h"
#include "mcp_server.h"
#include <esp_err.h>

class MatterController {
public:
    esp_err_t Initialize();
    std::unique_ptr<LvglImage> GetCommissioningQrcode();

    static MatterController *GetInstance()
    {
        static MatterController instance;
        return &instance;
    }

    void SetDeviceListState(bool state)
    {
        get_device_state_ = state;
    }

    bool GetDeviceListState()
    {
        return get_device_state_;
    }

private:
    MatterController() = default;
    bool get_device_state_ = false;
};

class MatterDeviceList {
public:
    class MatterDevice {
    public:
        class MatterCommand {
        public:
            MatterCommand(uint32_t command_id, const std::string &command_description)
                : command_id_(command_id)
                , command_description_(command_description)
            {}

            MatterCommand &AddCommandArg(const Property &arg)
            {
                command_args_.AddProperty(arg);
                return *this;
            }

            std::string to_json() const
            {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddNumberToObject(json, "id", command_id_);
                cJSON_AddStringToObject(json, "desc", command_description_.c_str());

                if (command_args_.begin() != command_args_.end()) {
                    cJSON *command_args_json = cJSON_Parse(command_args_.to_json().c_str());
                    cJSON_AddItemToObject(json, "args", command_args_json);
                } else {
                    cJSON_AddNullToObject(json, "args");
                }

                char *json_str = cJSON_PrintUnformatted(json);
                std::string result(json_str);
                cJSON_free(json_str);
                cJSON_Delete(json);

                return result;
            }

            uint32_t GetCommandId() const { return command_id_; }

        private:
            uint32_t command_id_;
            std::string command_description_;
            PropertyList command_args_;
        };

        class MatterCluster {
        public:
            MatterCluster(uint32_t cluster_id)
                : cluster_id_(cluster_id)
            {}

            std::string to_json() const
            {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddNumberToObject(json, "clu_id", cluster_id_);

                if (!command_lists_.empty()) {
                    cJSON *commands_json = cJSON_CreateArray();
                    for (const auto &command : command_lists_) {
                        cJSON *command_json = cJSON_Parse(command.to_json().c_str());
                        cJSON_AddItemToArray(commands_json, command_json);
                    }
                    cJSON_AddItemToObject(json, "commands", commands_json);
                }

                if (!command_lists_.empty()) {
                    cJSON *attributes_json = cJSON_CreateArray();
                    for (const auto &attribute : attribute_lists_) {
                        cJSON *attr_json = cJSON_CreateObject();
                        cJSON_AddNumberToObject(attr_json, "id", std::get<0>(attribute));
                        cJSON_AddStringToObject(attr_json, "name", std::get<1>(attribute).c_str());
                        cJSON_AddItemToArray(attributes_json, attr_json);
                    }
                    cJSON_AddItemToObject(json, "attributes", attributes_json);
                }

                char *json_str = cJSON_PrintUnformatted(json);
                std::string result(json_str);
                cJSON_free(json_str);
                cJSON_Delete(json);

                return result;
            }

            MatterCluster &AddCommand(const MatterCommand &command)
            {
                for (const auto &existing_command : command_lists_) {
                    if (existing_command.GetCommandId() == command.GetCommandId()) {
                        return *this;
                    }
                }
                command_lists_.push_back(command);
                return *this;
            }

            MatterCluster &AddCommand(uint32_t command_id, const std::string &command_description)
            {
                for (const auto &existing_command : command_lists_) {
                    if (existing_command.GetCommandId() == command_id) {
                        return *this;
                    }
                }
                command_lists_.emplace_back(command_id, command_description);
                return *this;
            }

            MatterCluster & AddAttribute(const uint32_t attribute_id, const std::string &attribute_name)
            {
                for (const auto &existing_attribute : attribute_lists_) {
                    if (std::get<0>(existing_attribute) == attribute_id) {
                        return *this;
                    }
                }
                attribute_lists_.emplace_back(attribute_id, attribute_name);
                return *this;
            }

            uint16_t GetClusterId() const { return cluster_id_; }

        private:
            uint16_t cluster_id_;
            std::vector<MatterCommand> command_lists_;
            std::vector<std::tuple<uint32_t, std::string>> attribute_lists_;
        };

        MatterDevice(const std::string &name, const std::string &device_type, uint64_t node_id, uint16_t endpoint_id)
            : device_name_(name)
            , device_type_(device_type)
            , node_id_(node_id)
            , endpoint_id_(endpoint_id)
        {}

        void AddCluster(const MatterCluster &cluster)
        {
            for (const auto &existing_cluster : cluster_lists_) {
                if (existing_cluster.GetClusterId() == cluster.GetClusterId()) {
                    return;
                }
            }
            cluster_lists_.push_back(cluster);
        }

        std::string to_json() const
        {
            cJSON *json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "name", device_name_.c_str());
            cJSON_AddStringToObject(json, "type", device_type_.c_str());
            char node_id_str[17] = {0};
            this->uint64_to_hex(node_id_, node_id_str);
            // snprintf(node_id_str, sizeof(node_id_str), "%016llX", node_id_);
            cJSON_AddStringToObject(json, "node_id", node_id_str);
            cJSON_AddNumberToObject(json, "endpoint_id", endpoint_id_);

            if (!cluster_lists_.empty()) {
                cJSON *clusters_json = cJSON_CreateArray();
                for (const auto &cluster : cluster_lists_) {
                    cJSON *clu_json = cJSON_Parse(cluster.to_json().c_str());
                    cJSON_AddItemToArray(clusters_json, clu_json);
                }
                cJSON_AddItemToObject(json, "clusters", clusters_json);
            }
            char *json_str = cJSON_PrintUnformatted(json);
            std::string result(json_str);
            cJSON_free(json_str);
            cJSON_Delete(json);
            return result;
        }

        void uint64_to_hex(uint64_t value, char *out_str) const
        {
            static const char hex[] = "0123456789ABCDEF";
            for (int i = 0; i < 16; ++i) {
                int shift = (15 - i) * 4;
                out_str[i] = hex[(value >> shift) & 0xF];
            }
            out_str[16] = '\0';
        }

        uint64_t GetNodeId() const { return node_id_; }

        uint16_t GetEndpointId() const { return endpoint_id_; }

    private:
        std::string device_name_;
        std::string device_type_;
        uint64_t node_id_;
        uint16_t endpoint_id_;

        std::vector<MatterCluster> cluster_lists_;
    };

    void AddDevice(const std::string &device_name, const std::string &device_type, uint64_t node_id,
                   uint16_t endpoint_id)
    {
        device_list_.emplace_back(device_name, device_type, node_id, endpoint_id);
    }

    void AddClusterToDevice(uint64_t node_id, uint16_t endpoint_id, const MatterDevice::MatterCluster &cluster)
    {
        for (auto &device : device_list_) {
            if (device.GetNodeId() == node_id && device.GetEndpointId() == endpoint_id) {
                device.AddCluster(cluster);
                return;
            }
        }
    }

    void AddDevice(MatterDevice &device)
    {
        for (const auto &existing_device : device_list_) {
            if (existing_device.GetNodeId() == device.GetNodeId() &&
                existing_device.GetEndpointId() == device.GetEndpointId()) {
                return;
            }
        }
        device_list_.push_back(device);
    }

    std::string to_json() const
    {
        cJSON *json = cJSON_CreateArray();
        for (const auto &device : device_list_) {
            cJSON *device_json = cJSON_Parse(device.to_json().c_str());
            cJSON_AddItemToArray(json, device_json);
        }
        char *json_str = cJSON_PrintUnformatted(json);
        std::string result(json_str);
        cJSON_free(json_str);
        cJSON_Delete(json);
        return result;
    }

private:
    std::vector<MatterDevice> device_list_;
};
/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "esp_matter.h"
#include "matter_device.h"

void start_esp_matter();

bool get_esp_matter_initialized();

bool get_esp_matter_commissioning_state();

bool esp_matter_factoryreset();

matter_device_t *esp_matter_get_device_list();

CHIP_ERROR send_cluster_command(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t command_id,
                                std::string &command_args, std::function<void(std::string &str)> success_callback,
                                std::function<void(std::string &str)> error_callback);

CHIP_ERROR read_cluster_attribute(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                                  std::function<void(std::string &str)> callback);

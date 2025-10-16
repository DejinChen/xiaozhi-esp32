/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "esp_matter.h"
#include "matter_device.h"

/** Standard max values (used for remapping attributes) */
#define STANDARD_BRIGHTNESS 100
#define STANDARD_HUE 360
#define STANDARD_SATURATION 100
#define STANDARD_TEMPERATURE_FACTOR 1000000

/** Matter max values (used for remapping attributes) */
#define MATTER_BRIGHTNESS 254
#define MATTER_HUE 254
#define MATTER_SATURATION 254
#define MATTER_TEMPERATURE_FACTOR 1000000

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

CHIP_ERROR send_light_onoff_command(uint64_t node_id, const std::string &action);
CHIP_ERROR send_light_level_command(uint64_t node_id, uint8_t level);
CHIP_ERROR send_light_hue_command(uint64_t node_id, uint8_t hue);
CHIP_ERROR send_light_saturation_command(uint64_t node_id, uint8_t saturation);
std::string get_light_state_command(uint64_t node_id);

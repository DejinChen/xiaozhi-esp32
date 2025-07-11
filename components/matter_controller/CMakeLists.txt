set(ldfragments linker.lf)
idf_component_register(SRC_DIRS         "." "dynamic_qrcode"
                       INCLUDE_DIRS      "." "dynamic_qrcode"
                       REQUIRES esp_matter esp_rainmaker controller_custom_cluster controller_rest_apis
                       LDFRAGMENTS "${ldfragments}")

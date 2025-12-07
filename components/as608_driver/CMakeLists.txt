idf_component_register(
    SRCS "as608_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_event nvs_flash
)
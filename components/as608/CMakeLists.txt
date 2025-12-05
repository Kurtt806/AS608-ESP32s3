idf_component_register(SRCS "as608.c" "as608_protocol.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer)
idf_component_register(
    SRCS 
        "as608.cpp" 
        "as608_protocol.cpp" 
        "as608_uart.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer
)
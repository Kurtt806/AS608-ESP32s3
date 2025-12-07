# Web assets - includes new ws.js and api.js

if(NOT CMAKE_CURRENT_BINARY_DIR MATCHES "bootloader")
idf_component_register(
    SRCS 
        "main.cc"
    INCLUDE_DIRS 
        "."
)
endif()
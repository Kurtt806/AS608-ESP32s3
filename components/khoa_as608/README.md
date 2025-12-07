# AS608 Fingerprint Sensor - C++ Driver

## Cấu trúc File

```
components/khoa_as608/
├── as608.hpp           # API công khai (include file chính)
├── as608.cpp           # Logic chính + C wrapper
├── as608_protocol.hpp  # Mã lệnh, parser định nghĩa
├── as608_protocol.cpp  # Protocol implementation
├── as608_uart.hpp      # UART driver class
├── as608_uart.cpp      # UART implementation
├── CMakeLists.txt      # Build configuration
└── README.md           # Documentation
```

## Hướng dẫn Include

### Sử dụng C++ (Khuyến nghị)

```cpp
#include "as608.hpp"

// Tạo instance
as608::AS608 sensor;

// Cấu hình
as608::Config config;
config.uart_num = 1;
config.tx_pin = 17;
config.rx_pin = 16;
config.baudrate = 57600;

// Khởi tạo
sensor.init(config);
```

### Sử dụng C (Tương thích ngược)

```c
#include "as608.hpp"

// Tạo handle
void* sensor = as608_create();

// Cấu hình
as608_config_t config = {
    .uart_num = 1,
    .tx_pin = 17,
    .rx_pin = 16,
    .baudrate = 57600
};

// Khởi tạo
as608_init(sensor, &config);
```

## Event-Driven Callbacks

### C++ API

```cpp
#include "as608.hpp"

as608::AS608 sensor;

// Đăng ký callback
sensor.setCallback([](as608::Event event, const as608::EventData& data) {
    switch (event) {
        case as608::Event::FingerDetected:
            printf("Finger detected!\n");
            break;
            
        case as608::Event::EnrollStep:
            printf("Enroll step %d/%d\n", data.step, data.totalSteps);
            break;
            
        case as608::Event::EnrollComplete:
            printf("Enrolled at ID %d\n", data.id);
            break;
            
        case as608::Event::MatchOk:
            printf("Match OK! ID=%d Score=%d\n", data.id, data.score);
            break;
            
        case as608::Event::MatchFailed:
            printf("No match found\n");
            break;
            
        default:
            break;
    }
});

// Khởi động enrollment không-blocking
sensor.startEnroll(5);  // Lưu vào ID 5

// Main loop
while (true) {
    sensor.process();  // Gọi thường xuyên để xử lý state machine
    vTaskDelay(pdMS_TO_TICKS(50));
}
```

### C API với Callback

```c
#include "as608.hpp"

void my_callback(as608_event_t event, const as608_event_data_t* data, void* user_data) {
    switch (event) {
        case AS608_EVENT_FINGER_DETECTED:
            printf("Finger detected!\n");
            break;
            
        case AS608_EVENT_ENROLL_STEP:
            printf("Enroll step %d/%d\n", data->step, data->total_steps);
            break;
            
        case AS608_EVENT_ENROLL_COMPLETE:
            printf("Enrolled at ID %d\n", data->id);
            break;
            
        case AS608_EVENT_MATCH_OK:
            printf("Match OK! ID=%d Score=%d\n", data->id, data->score);
            break;
            
        case AS608_EVENT_MATCH_FAILED:
            printf("No match found\n");
            break;
    }
}

void* sensor = as608_create();
as608_config_t config = { .uart_num = 1, .tx_pin = 17, .rx_pin = 16, .baudrate = 57600 };
as608_init(sensor, &config);
as608_set_callback(sensor, my_callback, NULL);

// Khởi động match không-blocking
as608_start_match(sensor);

// Main loop
while (1) {
    as608_process(sensor);  // Gọi thường xuyên
    vTaskDelay(pdMS_TO_TICKS(50));
}
```

## Synchronous (Blocking) Operations

```cpp
as608::AS608 sensor;
sensor.init(config);

// Handshake
if (sensor.handshake() == ESP_OK) {
    printf("Sensor connected\n");
}

// Đọc và match thủ công
if (sensor.readImage() == ESP_OK) {
    sensor.genChar(1);
    
    int matchId;
    uint16_t score;
    if (sensor.search(&matchId, &score) == ESP_OK) {
        printf("Match: ID=%d Score=%d\n", matchId, score);
    }
}

// Enrollment thủ công
// Bước 1: Đọc và tạo char buffer 1
sensor.readImage();
sensor.genChar(1);

// Bước 2: Đọc và tạo char buffer 2  
sensor.readImage();
sensor.genChar(2);

// Bước 3: Tạo model và lưu
sensor.regModel();
sensor.store(10);  // Lưu vào ID 10
```

## Asynchronous (Non-Blocking) State Machines

### Enrollment Flow

```
startEnroll(id)
    │
    v
WaitFinger1 ──► FingerDetected event
    │
    v
GenChar1 ──► EnrollStep(1) event
    │
    v
WaitRemoveFinger ──► FingerRemoved event ──► EnrollStep(2) event
    │
    v
WaitFinger2 ──► FingerDetected event  
    │
    v
GenChar2 ──► EnrollStep(3) event
    │
    v
CreateModel ──► EnrollStep(4) event
    │
    v
StoreModel ──► EnrollStep(5) event ──► EnrollComplete event
```

### Match Flow

```
startMatch()
    │
    v
WaitFinger ──► FingerDetected event
    │
    v
GenChar
    │
    v
Search ──► MatchOk(id, score) hoặc MatchFailed event
```

## Event Types

| Event | Description | Data Fields |
|-------|-------------|-------------|
| `FingerDetected` | Phát hiện ngón tay | - |
| `FingerRemoved` | Ngón tay đã bỏ ra | - |
| `EnrollStart` | Bắt đầu enrollment | `id`, `totalSteps` |
| `EnrollStep` | Hoàn thành một bước | `step`, `totalSteps` |
| `EnrollComplete` | Enrollment thành công | `id` |
| `EnrollFailed` | Enrollment thất bại | `error` |
| `MatchStart` | Bắt đầu match | - |
| `MatchOk` | Match thành công | `id`, `score` |
| `MatchFailed` | Không tìm thấy match | - |
| `Error` | Lỗi chung | `error` |

## Classes Overview

### `as608::AS608` (as608.hpp)
- Class chính để tương tác với sensor
- Chứa tất cả state trong private members
- Cung cấp cả blocking và non-blocking API

### `as608::AS608Uart` (as608_uart.hpp)  
- UART driver wrapper
- Quản lý TX/RX buffers
- Timeout handling

### `as608::AS608Protocol` (as608_protocol.hpp)
- Command codes (enum class Command)
- Confirmation codes (enum class ConfirmCode)
- Packet building và parsing

## Migration từ C Code

### Trước (C)

```c
#include "as608.h"

as608_config_t cfg = { .uart_num = 1, ... };
as608_init(&cfg);

as608_read_image();
as608_gen_char(1);
as608_search(&id, &score);
```

### Sau (C++)

```cpp
#include "as608.hpp"

as608::AS608 sensor;
as608::Config config(1, 17, 16, 57600);
sensor.init(config);

sensor.readImage();
sensor.genChar(1);
sensor.search(&id, &score);
```

### Sau (C với wrapper)

```c
#include "as608.hpp"

void* sensor = as608_create();
as608_config_t config = { .uart_num = 1, ... };
as608_init(sensor, &config);

as608_read_image(sensor);
as608_gen_char(sensor, 1);
as608_search(sensor, &id, &score);

as608_destroy(sensor);
```

## Build

Đảm bảo `CMakeLists.txt` chứa:

```cmake
idf_component_register(
    SRCS 
        "as608.cpp" 
        "as608_protocol.cpp" 
        "as608_uart.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer
)
```

Chạy:
```bash
idf.py build
```

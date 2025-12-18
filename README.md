# AS608-ESP32s3

Dự án hệ thống bảo mật vân tay sử dụng cảm biến AS608 trên bo mạch ESP32-S3 với ESP-IDF.

## Mô tả

Đây là một hệ thống quản lý vân tay tích hợp với ESP32-S3, cho phép đăng ký, so khớp và xóa vân tay. Dự án bao gồm các tính năng như WiFi và phản hồi âm thanh.

## Tính năng chính

- **Quản lý vân tay**: Đăng ký, so khớp, xóa vân tay
- **Giao diện web**: Cấu hình và quản lý qua trình duyệt
- **WiFi**: Kết nối mạng không dây
- **Phản hồi âm thanh**: Thông báo trạng thái qua loa
- **Lưu trữ NVS**: Lưu trữ cài đặt và metadata vân tay

## Yêu cầu phần cứng

- ESP32-S3
- Cảm biến vân tay AS608
- Loa hoặc module âm thanh (tùy chọn)

## Yêu cầu phần mềm

- ESP-IDF v5.x
- Python 3.x (cho công cụ build)

## Cài đặt và Build

1. **Xuất môi trường ESP-IDF**:
   ```
   C:\esp\v5.5.1\esp-idf\export.ps1
   ```

2. **Dọn dẹp build**:
   ```
   idf.py fullclean
   ```

3. **Thiết lập target**:
   ```
   idf.py set-target esp32s3
   ```

4. **Cấu hình (tùy chọn)**:
   ```
   idf.py menuconfig
   ```

5. **Build**:
   ```
   idf.py build
   ```

6. **Flash và monitor**:
   ```
   idf.py flash monitor
   ```

## Xuất firmware

Sử dụng script `tools/export_firmware.py` để xuất firmware:
```
python tools/export_firmware.py
```

## Cấu trúc dự án

- `main/`: Code chính của ứng dụng
  - `app/`: Logic điều khiển chính
  - `finger/`: Quản lý cảm biến vân tay
  - `wifi/`: Quản lý WiFi
  - `settings/`: Quản lý cài đặt
  - `common/`: Các tiện ích chung
- `components/`: Các component tùy chỉnh
  - `as608/`: Driver cho cảm biến AS608
  - `khoa_audio/`: Quản lý âm thanh
  - `khoa_esp_wifi/`: Tiện ích WiFi
- `tools/`: Công cụ hỗ trợ

## Sử dụng

Sau khi flash, ESP32-S3 sẽ khởi động và tạo điểm truy cập WiFi. Kết nối vào mạng WiFi của thiết bị và truy cập giao diện web để cấu hình và quản lý vân tay.

## Đóng góp

Mọi đóng góp đều được chào đón. Vui lòng tạo issue hoặc pull request trên GitHub.

## Giấy phép

Dự án này sử dụng giấy phép MIT.
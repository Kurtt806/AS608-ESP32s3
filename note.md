

# 1. Export ESP-IDF environment
C:\esp\v5.5.1\esp-idf\export.ps1

# 2. Clean build
idf.py fullclean

# 3. Set target
idf.py set-target esp32s3

# 4. mở menuconfig
idf.py menuconfig

# 5. build
idf.py -v build;    idf.py -v flash monitor

# 6. hoặc chỉ build app
idf.py app; idf.py app-flash monitor

# xuất rel
python D:\ESP-IDF\AS608-ESP32s3\tools\export_firmware.py

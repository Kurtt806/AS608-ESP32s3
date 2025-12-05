

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
python scripts/release.py

///////////--------->>>>>>
chcp 65001
$env:PYTHONUTF8="1"
$env:PYTHONIOENCODING="utf-8"
idf.py -v reconfigure
///////////--------->>>>>>

# ESP-IDF Partition Table (ESP32-S3, 4MB)
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x4000,
otadata,  data, ota,     0xD000,   0x2000,
phy_init, data, phy,     0xF000,   0x1000,
factory,  app,  factory, 0x10000,  0x280000,  
assets,   data, spiffs,  0x290000, 0x170000,
# Trước tiên hãy sao lưu phân vùng thông tin xuất xưởng chứa thông tin xác thực để kết nốiSenseCraft server
esptool.py --chip esp32s3 --baud 2000000 --before default_reset --after hard_reset --no-stub read_flash 0x9000 204800 nvsfactory.bin


<!-- giải nén ZIP -->
Expand-Archive -Path "D:\XIAOZHI\xiaozhi-music\releases\v2.0.3_bread-compact-wifi.zip" -DestinationPath "D:\XIAOZHI\xiaozhi-music\releases\v2.0.3_bread-compact-wifi"

<!-- **************************************************************************** -->
# build cho sensecap-watcher 2.0.4
-> Build 
python scripts/release.py sensecap-watcher -c config_vi.json

-> Flash
idf.py -DBOARD_NAME=sensecap-watcher-en build flash

<!-- **************************************************************************** -->
# build cho esp32s3-n16r8 lcd 1.54 inch
-> Build
python scripts/release.py xingzhi-cube-1.54tft-wifi -c config_vi.json

-> Flash
idf.py -DBOARD_NAME=xingzhi-cube-1.54tft-wifi build flash

<!-- **************************************************************************** -->
# build cho esp32s3-n4r2 lcd (VI3D-S3N4R2-240-240)
-> Build
python scripts/release.py VI3D-S3N4R2-240-240 -c config_vi.json

-> Flash
idf.py -DBOARD_NAME=VI3D-S3N4R2-240-240 build flash

<!-- **************************************************************************** -->
# build cho esp32s3-n16R8 lcd 1.54 inch (VI3D-S3N16R8-240-240)
-> Build
python scripts/release.py VI3D-S3N16R8-240-240 -c config_vi.json

-> Flash
idf.py -DBOARD_NAME=VI3D-S3N16R8-240-240 build flash

<!-- **************************************************************************** -->
# build cho bread-compact-wifi
-> Build,Flash,Monitor
del releases\v2.0.3_bread-compact-wifi.zip -Recurse -Force ; 
python scripts/release.py bread-compact-wifi -c config_vi.json;
idf.py -DBOARD_NAME=bread-compact-wifi build flash monitor;

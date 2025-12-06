#!/usr/bin/env python3
"""
export_firmware.py - Export compiled firmware to release folder

Usage:
    python tools/export_firmware.py [version]

Example:
    python tools/export_firmware.py 1.0.0
    python tools/export_firmware.py  # Auto-generates version from date
"""

import os
import sys
import shutil
from datetime import datetime
from pathlib import Path

# Paths
PROJECT_DIR = Path(__file__).parent.parent
BUILD_DIR = PROJECT_DIR / "build"
RELEASE_DIR = PROJECT_DIR / "release"

# Files to export
FILES_TO_EXPORT = [
    ("AS608-ESP32s3.bin", "firmware.bin"),           # Main firmware
    ("bootloader/bootloader.bin", "bootloader.bin"), # Bootloader
    ("partition_table/partition-table.bin", "partition-table.bin"),  # Partition table
    ("ota_data_initial.bin", "ota_data_initial.bin"),  # OTA data (if exists)
]

def get_version():
    """Get version string from command line or generate from date"""
    if len(sys.argv) > 1:
        return sys.argv[1]
    return datetime.now().strftime("%Y%m%d_%H%M%S")

def get_firmware_info():
    """Extract firmware info from build"""
    info = {
        "project": "AS608-ESP32s3",
        "target": "esp32s3",
    }
    
    # Try to read project description
    desc_file = BUILD_DIR / "project_description.json"
    if desc_file.exists():
        import json
        with open(desc_file, 'r') as f:
            desc = json.load(f)
            info["project"] = desc.get("project_name", info["project"])
            info["idf_version"] = desc.get("idf_ver", "unknown")
    
    return info

def export_firmware():
    """Export firmware files to release folder"""
    version = get_version()
    info = get_firmware_info()
    
    # Create release folder with version
    release_folder = RELEASE_DIR / f"v{version}"
    release_folder.mkdir(parents=True, exist_ok=True)
    
    print(f"\n{'='*60}")
    print(f"  Exporting Firmware - {info['project']}")
    print(f"  Version: {version}")
    print(f"{'='*60}\n")
    
    exported_files = []
    
    for src_name, dst_name in FILES_TO_EXPORT:
        src_path = BUILD_DIR / src_name
        
        if src_path.exists():
            # Add version to filename
            name, ext = os.path.splitext(dst_name)
            versioned_name = f"{name}_v{version}{ext}"
            dst_path = release_folder / versioned_name
            
            shutil.copy2(src_path, dst_path)
            size_kb = dst_path.stat().st_size / 1024
            print(f"  ✓ {dst_name:30} -> {versioned_name} ({size_kb:.1f} KB)")
            exported_files.append(dst_path)
        else:
            print(f"  ✗ {src_name:30} (not found)")
    
    # Also copy combined binary for easy flashing
    combined_bin = BUILD_DIR / "AS608-ESP32s3.bin"
    if combined_bin.exists():
        # Copy as single OTA update file
        ota_file = release_folder / f"ota_update_v{version}.bin"
        shutil.copy2(combined_bin, ota_file)
        size_kb = ota_file.stat().st_size / 1024
        print(f"\n  ✓ OTA Update file: ota_update_v{version}.bin ({size_kb:.1f} KB)")
    
    # Create flash info file
    flash_info = release_folder / "flash_info.txt"
    with open(flash_info, 'w') as f:
        f.write(f"Firmware Export Info\n")
        f.write(f"{'='*40}\n\n")
        f.write(f"Project: {info['project']}\n")
        f.write(f"Version: {version}\n")
        f.write(f"Target: {info.get('target', 'esp32s3')}\n")
        f.write(f"IDF Version: {info.get('idf_version', 'unknown')}\n")
        f.write(f"Export Date: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")
        f.write(f"Flash Commands:\n")
        f.write(f"--------------\n")
        f.write(f"Full Flash:\n")
        f.write(f"  esptool.py -p COMx -b 460800 --before default_reset --after hard_reset write_flash\n")
        f.write(f"    0x0 bootloader_v{version}.bin\n")
        f.write(f"    0x8000 partition-table_v{version}.bin\n")
        f.write(f"    0x20000 firmware_v{version}.bin\n\n")
        f.write(f"OTA Update (via web interface):\n")
        f.write(f"  Upload: ota_update_v{version}.bin\n")
    
    print(f"\n  ✓ Flash info: flash_info.txt")
    
    print(f"\n{'='*60}")
    print(f"  Export complete! Files saved to:")
    print(f"  {release_folder}")
    print(f"{'='*60}\n")
    
    return release_folder

def main():
    # Check if build exists
    if not BUILD_DIR.exists():
        print("Error: Build directory not found!")
        print("Please run 'idf.py build' first.")
        sys.exit(1)
    
    main_bin = BUILD_DIR / "AS608-ESP32s3.bin"
    if not main_bin.exists():
        print("Error: Firmware binary not found!")
        print("Please run 'idf.py build' first.")
        sys.exit(1)
    
    export_firmware()

if __name__ == "__main__":
    main()

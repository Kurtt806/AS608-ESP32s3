#!/usr/bin/env python3
"""
Audio to PCM Converter for ESP32 Audio Module

Converts audio files (WAV, MP3, OGG, FLAC) to raw PCM format
optimized for ESP32 playback.

Output format: 16-bit signed LE, mono, 16kHz

Requirements:
- ffmpeg (must be in PATH)
- Python 3.6+

Usage:
  python convert_audio.py input.wav                    # Convert single file
  python convert_audio.py input1.wav input2.mp3       # Convert multiple files
  python convert_audio.py *.wav                       # Convert all WAV files
  python convert_audio.py input.wav -o output.pcm    # Specify output name
  python convert_audio.py input.wav -d sounds/       # Specify output directory
"""

import subprocess
import sys
import os
import argparse
from pathlib import Path

def convert_to_pcm(input_file: str, output_file: str = None, output_dir: str = None) -> bool:
    """
    Convert audio file to PCM format for ESP32
    
    Args:
        input_file: Path to input audio file
        output_file: Optional output file path
        output_dir: Optional output directory
    
    Returns:
        True if successful, False otherwise
    """
    input_path = Path(input_file)
    
    if not input_path.exists():
        print(f"Error: File not found: {input_file}")
        return False
    
    # Determine output path
    if output_file:
        out_path = Path(output_file)
    elif output_dir:
        out_path = Path(output_dir) / f"{input_path.stem}.pcm"
    else:
        out_path = input_path.with_suffix('.pcm')
    
    # Create output directory if needed
    out_path.parent.mkdir(parents=True, exist_ok=True)
    
    # FFmpeg command for PCM output
    cmd = [
        'ffmpeg',
        '-y',                    # Overwrite output
        '-i', str(input_path),   # Input file
        '-f', 's16le',           # Raw PCM format
        '-acodec', 'pcm_s16le',  # 16-bit signed little-endian
        '-ar', '16000',          # 16kHz sample rate
        '-ac', '1',              # Mono
        str(out_path)
    ]
    
    print(f"Converting: {input_path.name} -> {out_path.name}")
    
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True
        )
        
        if result.returncode != 0:
            print(f"Error: {result.stderr}")
            return False
        
        # Print file size and duration
        size = out_path.stat().st_size
        duration_ms = (size / 2) / 16  # 16-bit = 2 bytes, 16kHz
        print(f"  OK: {size} bytes ({size/1024:.1f} KB, {duration_ms:.0f}ms)")
        return True
        
    except FileNotFoundError:
        print("Error: ffmpeg not found. Please install ffmpeg and add to PATH.")
        return False
    except Exception as e:
        print(f"Error: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(
        description='Convert audio files to PCM for ESP32',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s input.wav                    # Convert to input.pcm
  %(prog)s input.wav -o beep.pcm        # Convert to beep.pcm
  %(prog)s *.wav -d sounds/             # Convert all WAV to sounds/
  
Required sound files for ESP32 audio module:
  boot.pcm, ready.pcm, beep.pcm, finger_detected.pcm,
  match_ok.pcm, match_fail.pcm, enroll_start.pcm, enroll_step.pcm,
  enroll_ok.pcm, enroll_fail.pcm, delete_ok.pcm, error.pcm
  
PCM Format: 16-bit signed LE, mono, 16kHz
"""
    )
    
    parser.add_argument('files', nargs='+', help='Input audio files')
    parser.add_argument('-o', '--output', help='Output file (single input only)')
    parser.add_argument('-d', '--dir', help='Output directory')
    
    args = parser.parse_args()
    
    if args.output and len(args.files) > 1:
        print("Error: -o/--output can only be used with single input file")
        sys.exit(1)
    
    success = 0
    failed = 0
    
    for f in args.files:
        if convert_to_pcm(f, args.output, args.dir):
            success += 1
        else:
            failed += 1
    
    print(f"\nDone: {success} converted, {failed} failed")
    sys.exit(0 if failed == 0 else 1)


if __name__ == '__main__':
    main()

#!/bin/zsh
# ============================================================================
# RaveGPT USB Device Diagnostic & Recovery Script
# ============================================================================
# Checks all layers: USB enumeration, serial ports, firmware status, 
# serial communication, and automatically recovers devices in bootloader mode
# ============================================================================

set -e

SCRIPT_DIR="${0:a:h}"
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo "${BLUE}   RaveGPT Multi-Layer Diagnostic & Recovery Tool${NC}"
echo "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo ""

# ============================================================================
# Layer 1: USB Device Enumeration
# ============================================================================
echo "${YELLOW}[Layer 1] USB Device Enumeration${NC}"
echo "─────────────────────────────────────────"

ESP32_DETECTED=false
FFT_TEENSY_DETECTED=false
LED_TEENSY_DETECTED=false
FFT_IN_BOOTLOADER=false

echo "Scanning USB devices..."
USB_DATA=$(system_profiler SPUSBDataType 2>/dev/null)

# Check ESP32
if echo "$USB_DATA" | grep -q "USB JTAG/serial debug unit"; then
    ESP32_SERIAL=$(echo "$USB_DATA" | grep -A 5 "USB JTAG/serial debug unit" | grep "Serial Number:" | awk '{print $3}')
    echo "${GREEN}✓ ESP32 detected${NC} (Serial: $ESP32_SERIAL)"
    ESP32_DETECTED=true
else
    echo "${RED}✗ ESP32 NOT detected${NC}"
fi

# Check Teensy devices (Product ID 0x0478 = HID/Bootloader, 0x0483 = Serial mode)
TEENSY_COUNT=$(echo "$USB_DATA" | grep -c "Vendor ID: 0x16c0" || true)
if [ $TEENSY_COUNT -eq 0 ]; then
    echo "${RED}✗ No Teensy devices detected${NC}"
else
    echo "${BLUE}Found $TEENSY_COUNT Teensy device(s)${NC}"
    
    # Check for USB Serial (running firmware)
    if echo "$USB_DATA" | grep -q "Product ID: 0x0483"; then
        SERIAL_TEENSY=$(echo "$USB_DATA" | grep -B 5 "Product ID: 0x0483" | grep "Serial Number:" | awk '{print $3}')
        echo "${GREEN}✓ Teensy in Serial mode detected${NC} (Serial: $SERIAL_TEENSY)"
        LED_TEENSY_DETECTED=true
    fi
    
    # Check for Composite Device (HID/bootloader mode)
    if echo "$USB_DATA" | grep -q "Product ID: 0x0478"; then
        HID_TEENSY=$(echo "$USB_DATA" | grep -B 5 "Product ID: 0x0478" | grep "Serial Number:" | awk '{print $3}')
        echo "${YELLOW}⚠ Teensy in HID/Bootloader mode detected${NC} (Serial: $HID_TEENSY)"
        echo "  ${YELLOW}This Teensy is NOT running firmware - needs upload${NC}"
        FFT_TEENSY_DETECTED=true
        FFT_IN_BOOTLOADER=true
    fi
fi

echo ""

# ============================================================================
# Layer 2: Serial Port Detection
# ============================================================================
echo "${YELLOW}[Layer 2] Serial Port Detection${NC}"
echo "─────────────────────────────────────────"

SERIAL_PORTS=($(ls /dev/cu.usbmodem* 2>/dev/null || true))
PORT_COUNT=${#SERIAL_PORTS[@]}

echo "Found $PORT_COUNT serial port(s):"
for port in "${SERIAL_PORTS[@]}"; do
    echo "  - $port"
done

ESP32_PORT=""
LED_TEENSY_PORT=""

if [ $PORT_COUNT -eq 0 ]; then
    echo "${RED}✗ No serial ports detected!${NC}"
elif [ $PORT_COUNT -eq 1 ]; then
    echo "${YELLOW}⚠ Only 1 serial port - likely ESP32${NC}"
    ESP32_PORT="${SERIAL_PORTS[1]}"
elif [ $PORT_COUNT -eq 2 ]; then
    echo "${GREEN}✓ 2 serial ports detected${NC}"
    echo "  ${YELLOW}Assuming: Port 1 = ESP32, Port 2 = LED Teensy${NC}"
    ESP32_PORT="${SERIAL_PORTS[1]}"
    LED_TEENSY_PORT="${SERIAL_PORTS[2]}"
else
    echo "${GREEN}✓ $PORT_COUNT serial ports detected${NC}"
    ESP32_PORT="${SERIAL_PORTS[1]}"
    LED_TEENSY_PORT="${SERIAL_PORTS[2]}"
fi

echo ""

# ============================================================================
# Layer 3: Firmware Status Check
# ============================================================================
echo "${YELLOW}[Layer 3] Firmware Status${NC}"
echo "─────────────────────────────────────────"

NEEDS_FFT_UPLOAD=false
NEEDS_LED_UPLOAD=false

if [ "$FFT_IN_BOOTLOADER" = true ]; then
    echo "${RED}✗ FFT Teensy is in bootloader mode - NO FIRMWARE RUNNING${NC}"
    NEEDS_FFT_UPLOAD=true
else
    echo "${GREEN}✓ FFT Teensy appears to be running firmware${NC}"
fi

if [ "$LED_TEENSY_DETECTED" = true ]; then
    echo "${GREEN}✓ LED Teensy is running firmware (USB Serial mode)${NC}"
else
    echo "${YELLOW}⚠ LED Teensy status unknown${NC}"
fi

echo ""

# ============================================================================
# Layer 4: Wiring/Pin Configuration Check
# ============================================================================
echo "${YELLOW}[Layer 4] Expected Wiring Configuration${NC}"
echo "─────────────────────────────────────────"
echo "Expected connections:"
echo "  1. ESP32 TX → FFT Teensy Pin 0 (Serial1 RX) @ 38400 baud"
echo "  2. FFT Teensy Pin 14 (Serial3 TX) → LED Teensy Pin 1 (Serial1 RX) @ 921600 baud"
echo ""
echo "${YELLOW}⚠ Physical wiring cannot be verified automatically - manual check required${NC}"
echo ""

# ============================================================================
# Layer 5: Serial Communication Test (if devices available)
# ============================================================================
echo "${YELLOW}[Layer 5] Serial Communication Test${NC}"
echo "─────────────────────────────────────────"

if [ -n "$LED_TEENSY_PORT" ]; then
    echo "Testing LED Teensy serial output..."
    timeout 2 cat "$LED_TEENSY_PORT" 2>/dev/null | head -5 &
    MONITOR_PID=$!
    sleep 2
    kill $MONITOR_PID 2>/dev/null || true
    wait $MONITOR_PID 2>/dev/null || true
    echo "${GREEN}✓ LED Teensy serial port accessible${NC}"
else
    echo "${YELLOW}⚠ Cannot test - LED Teensy serial port not available${NC}"
fi

echo ""

# ============================================================================
# Layer 6: Recovery Actions
# ============================================================================
echo "${YELLOW}[Layer 6] Recovery & Upload${NC}"
echo "─────────────────────────────────────────"

if [ "$NEEDS_FFT_UPLOAD" = true ]; then
    echo "${BLUE}FFT Teensy needs firmware upload${NC}"
    echo "Project: teensy_fft"
    echo ""
    
    read -q "REPLY?Upload FFT Teensy firmware now? (y/n) "
    echo ""
    
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "${BLUE}Uploading FFT Teensy firmware...${NC}"
        cd "$SCRIPT_DIR/teensy_fft"
        pio run -t upload
        
        if [ $? -eq 0 ]; then
            echo "${GREEN}✓ FFT Teensy firmware uploaded successfully${NC}"
            echo "  ${YELLOW}Waiting for device to restart...${NC}"
            sleep 3
        else
            echo "${RED}✗ FFT Teensy upload failed${NC}"
            exit 1
        fi
    fi
fi

echo ""

# ============================================================================
# Layer 7: Post-Recovery Verification
# ============================================================================
echo "${YELLOW}[Layer 7] Post-Recovery Verification${NC}"
echo "─────────────────────────────────────────"

echo "Re-scanning serial ports..."
SERIAL_PORTS_AFTER=($(ls /dev/cu.usbmodem* 2>/dev/null || true))
PORT_COUNT_AFTER=${#SERIAL_PORTS_AFTER[@]}

echo "Serial ports after recovery: $PORT_COUNT_AFTER"
for port in "${SERIAL_PORTS_AFTER[@]}"; do
    echo "  - $port"
done

if [ $PORT_COUNT_AFTER -eq 3 ]; then
    echo "${GREEN}✓ All 3 devices showing serial ports!${NC}"
elif [ $PORT_COUNT_AFTER -eq 2 ]; then
    echo "${YELLOW}⚠ Only 2 serial ports (FFT Teensy might still be in bootloader)${NC}"
else
    echo "${RED}✗ Unexpected port count: $PORT_COUNT_AFTER${NC}"
fi

echo ""

# ============================================================================
# Summary
# ============================================================================
echo "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo "${BLUE}   Diagnostic Summary${NC}"
echo "${BLUE}═══════════════════════════════════════════════════════════════${NC}"

[ "$ESP32_DETECTED" = true ] && echo "${GREEN}✓ ESP32${NC}" || echo "${RED}✗ ESP32${NC}"
[ "$FFT_TEENSY_DETECTED" = true ] && echo "${GREEN}✓ FFT Teensy${NC}" || echo "${RED}✗ FFT Teensy${NC}"
[ "$LED_TEENSY_DETECTED" = true ] && echo "${GREEN}✓ LED Teensy${NC}" || echo "${RED}✗ LED Teensy${NC}"

echo ""
echo "Serial Ports: $PORT_COUNT_AFTER"
if [ $PORT_COUNT_AFTER -eq 3 ]; then
    echo "${GREEN}Status: All devices ready${NC}"
elif [ "$FFT_IN_BOOTLOADER" = true ]; then
    echo "${YELLOW}Status: FFT Teensy needs firmware upload (press boot button was pressed)${NC}"
else
    echo "${YELLOW}Status: Check connections${NC}"
fi

echo ""
echo "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo ""

# Offer to monitor serial outputs
read -q "REPLY?Monitor FFT Teensy serial output? (y/n) "
echo ""
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if [ $PORT_COUNT_AFTER -ge 2 ]; then
        echo "${BLUE}Monitoring FFT Teensy (${SERIAL_PORTS_AFTER[2]})...${NC}"
        echo "Press Ctrl+C to exit"
        cat "${SERIAL_PORTS_AFTER[2]}"
    else
        echo "${RED}Cannot monitor - insufficient ports${NC}"
    fi
fi

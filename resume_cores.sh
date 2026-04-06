#!/bin/bash
# Resume All Cores Script for AM263Px
# Use this if cores are halted after debug session

echo "============================================"
echo "Resume AM263Px Cores"
echo "============================================"
echo ""
echo "This script will resume all halted cores"
echo ""

echo "Connecting to XDS110 debugger..."
echo ""

# Resume Core0 (FreeRTOS)
echo "Resuming Core0 (Cortex_R5_0 - FreeRTOS)..."
arm-none-eabi-gdb -batch -ex "target extended-remote :3333" -ex "monitor core 0" -ex "monitor resume" -ex "detach" -ex "quit" 2>/dev/null
if [ $? -eq 0 ]; then
    echo "[OK] Core0 resumed"
else
    echo "[WARN] Failed to resume Core0 - may not be halted"
fi

echo ""

# Resume Core3 (NoRTOS)
echo "Resuming Core3 (Cortex_R5_3 - NoRTOS)..."
arm-none-eabi-gdb -batch -ex "target extended-remote :3333" -ex "monitor core 3" -ex "monitor resume" -ex "detach" -ex "quit" 2>/dev/null
if [ $? -eq 0 ]; then
    echo "[OK] Core3 resumed"
else
    echo "[WARN] Failed to resume Core3 - may not be halted"
fi

echo ""
echo "============================================"
echo "Done! All cores should be running now."
echo "============================================"
echo ""

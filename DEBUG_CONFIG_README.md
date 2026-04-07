# CCU Multicore Debug Configuration

## ⚠️ IMPORTANT: Cross-Cluster Debug Fix

**Nếu Core3 không halt khi debug lần thứ 2, xem:** [CCS_MULTICORE_DEBUG_TUTORIAL.md](CCS_MULTICORE_DEBUG_TUTORIAL.md) - Hướng dẫn fix chi tiết.

**Tóm tắt:** File `.theia/launch.json` phải có `ResetOnRestart=1` và `AutoResetOnConnect=1` cho TẤT CẢ cores.

## Overview

Đã cấu hình debug riêng cho từng core của AM263Px multicore system:
- **Core0 (FreeRTOS)**: Cortex_R5_0 (r5fss0-0) - Ethernet Gateway
- **Core1 (NoRTOS)**: Cortex_R5_3 (r5fss1-1) - Real-time Control

## Files Created/Modified

### Target Configuration Files (.ccxml)
- `ccu_ti_multi_core_freertos/targetConfigs/AM263Px_R5F0.ccxml` - Core0 specific
- `ccu_ti_multi_core_realtime/targetConfigs/AM263Px_R5F3.ccxml` - Core1 specific

### VS Code Debug Configuration
- `.vscode/launch.json` - Debug configurations for VS Code
- `.vscode/tasks.json` - Build tasks

### GDB Init Scripts
- `ccu_ti_multi_core_freertos/gdb_init_r5f0.txt` - GDB init for Core0
- `ccu_ti_multi_core_realtime/gdb_init_r5f3.txt` - GDB init for Core1

## How to Debug

### Using CCS Theia (Code Composer Studio)

#### Debug Single Core
1. Open project in CCS Theia
2. Right-click on project → **Debug As** → **CCS Debug**
3. CCS sẽ tự động sử dụng file .ccxml tương ứng:
   - `ccu_ti_multi_core_freertos` → AM263Px_R5F0.ccxml → Core0
   - `ccu_ti_multi_core_realtime` → AM263Px_R5F3.ccxml → Core1

#### Debug Both Cores (Synchronous)
1. Open `ccu_ti_multi_core_system` project
2. Right-click → **Debug As** → **CCS Debug**
3. Cả 2 cores sẽ được debug đồng thời

### Using VS Code

#### Prerequisites
Cần cài đặt extension:
- Cortex-Debug (marus25.cortex-debug)
- C/C++ (ms-vscode.cpptools)

#### Debug Core0 (FreeRTOS)
1. Mở file `ccu_ti_multi_core_freertos/main.c`
2. Nhấn F5 hoặc chọn **Run and Debug**
3. Chọn **Debug Core0 (FreeRTOS - R5F0)**

#### Debug Core1 (NoRTOS)
1. Mở file `ccu_ti_multi_core_realtime/main.c`
2. Nhấn F5 hoặc chọn **Run and Debug**
3. Chọn **Debug Core1 (NoRTOS - R5F3)**

### Using GDB CLI

#### Debug Core0
```bash
cd D:\VinDynamics\Project\CCU_TI_RS485_CAN\2.Firmware\ccu_ti_mutilcore
arm-none-eabi-gdb -x ccu_ti_multi_core_freertos/gdb_init_r5f0.txt
```

#### Debug Core1
```bash
cd D:\VinDynamics\Project\CCU_TI_RS485_CAN\2.Firmware\ccu_ti_mutilcore
arm-none-eabi-gdb -x ccu_ti_multi_core_realtime/gdb_init_r5f3.txt
```

## Debug Behavior (Configured)

### Start Debug Session
- **Core được RESET** tự động khi debug bắt đầu
- **Core HALT** sau reset để debugger có thể load symbols
- GDB/VS Code: `preLaunchCommands` với `monitor reset` + `monitor halt`
- CCS: `.ccxml` với `<startup>` commands

### Stop Debug Session
- **Core TIẾP TỤC CHẠY** - KHÔNG halt, KHÔNG reset
- GDB: `quit` function với `continue &` trước khi detach
- VS Code: `postRestartCommands: ["continue"]`
- CCS: Cần cấu hình "Do not halt on disconnect" (xem bên dưới)

### CCS Configuration Required
Để core chạy tiếp khi disconnect trong CCS:

**CCS Theia (Code Composer Studio):**
1. Menu → **Window** → **Preferences**
2. Expand **Code Composer Studio** → **Debug**
3. Tìm option: **"On debugger disconnect"**
4. Chọn: **"Do not halt or reset"** hoặc **"Continue execution"**
5. Apply và OK

**Hoặc qua CCS Console:**
```
CCS Debug Console > help disconnect
Usage: disconnect [options]
Options:
  --no-halt     Do not halt target on disconnect
  --no-reset    Do not reset target on disconnect
```

### Manual Reset/Halt Commands
```xml
<!-- CCS Debug View -->
Right-click on core → Reset → Reset CPU     (Reset core)
Right-click on core → Suspend               (Halt core)
Right-click on core → Resume                (Continue running)
```

## Troubleshooting

### Issue: Cannot connect to Core3 (R5F3)
**Solution**: Đảm bảo sử dụng file `AM263Px_R5F3.ccxml` trong project `ccu_ti_multi_core_realtime`

### Issue: Core halts after stopping debug session
**Solution**:
1. **CCS**: Configure Preferences → Debug → "Do not halt on disconnect"
2. **VS Code**: Kiểm tra `postRestartCommands: ["continue"]` trong launch.json
3. **GDB**: Script đã có `continue &` trước detach, kiểm tra có được execute không

### Issue: Core not resetting on debug start
**Solution**:
1. Kiểm tra file .ccxml có `<startup>` section với `monitor reset` command
2. VS Code: Kiểm tra `preLaunchCommands` có `monitor reset`
3. CCS: Check option "Reset on connect" trong Debug Preferences

### Issue: VS Code debug not working
**Solution**:
1. Kiểm tra debugger path trong `launch.json` → `armToolchainPath`
2. Đảm bảo J-Link hoặc XDS110 debugger đang kết nối
3. Xem Output tab → Cortex Debug để xem lỗi chi tiết

### Issue: After debug, core is stuck/halted
**Quick Fix**:
```bash
# GDB CLI
arm-none-eabi-gdb
(gdb) target extended-remote :3333
(gdb) monitor core 0  # hoặc 3
(gdb) monitor resume
(gdb) quit
```

**CCS:**
1. Open Debug View
2. Right-click core → **Resume** (hoặc F8)

## Core Mapping Reference

```
AM263Px Core Architecture:
├── R5FSS0 (Cluster 0)
│   ├── r5fss0-0 → Cortex_R5_0 (Core0 - FreeRTOS)
│   └── r5fss0-1 → Cortex_R5_1 (unused)
└── R5FSS1 (Cluster 1)
    ├── r5fss1-0 → Cortex_R5_2 (unused)
    └── r5fss1-1 → Cortex_R5_3 (Core1 - NoRTOS)
```

## Notes

- Cross-cluster debug (r5fss0-0 + r5fss1-1) yêu cầu file ccxml riêng cho mỗi core
- System project sử dụng `system.xml` để map projects đến cores
- SysConfig context phải khớp với core: `r5fss0-0` cho Core0, `r5fss1-1` cho Core1

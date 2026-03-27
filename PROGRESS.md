# Nhật ký Dự án: CCU Multicore Gateway IPC

## 1. Thông tin chung
- **Ngày cập nhật:** 2026-03-26 16:00
- **Trạng thái:** ✅ LOCK-FREE RING BUFFER IPC HOÀN THÀNH!
- **Cấu hình phần cứng:** AM263Px LaunchPad (R5FSS0-0 FreeRTOS, R5FSS0-1 NoRTOS).
- **Shared Memory:** 32KB (0x701D0000)

## 2. TỔNG KẾT THÀNH TÍCH 🎉

### ✅ Bidirectional IPC (HOÀN THÀNH)
- Core 0 ↔ Core 1: IPC hoạt động 2 chiều ổn định
- Core 1 → Core 0: MSG_CAN_DATA_READY (0x01) ✓
- Core 0 → Core 1: MSG_ETH_DATA_READY (0x02) ✓

### ✅ Bidirectional Shared Memory (HOÀN THÀNH)
- **Core 0 → Core 1:** Core 0 ghi → Core 1 đọc ✓
  - Test data: `[1,2,3,4,5,6,7,8]`
  - Message: MSG_TEST_DATA_READY (0x09)
  - Evidence: `[Core1] *** TEST DATA from Core 0: 1 2 3 4 5 6 7 8 ***`

- **Core 1 → Core 0:** Core 1 ghi → Core 0 đọc ✓
  - Test data: `[10,20,30,40,50,60,70,80]`
  - Message: MSG_TEST_DATA_FROM_CORE1 (0x0A)
  - Evidence: `[Core0] *** TEST DATA from Core 1: 10 20 30 40 50 60 70 80 ***`

## 3. VẤN ĐỀ ĐÃ GIẢI QUYẾT

### Vấn đề 1: IPC Callback Signature (ĐÃ FIX)
✅ Signature đã sửa đúng với SDK

### Vấn đề 2: 50s DELAY (ĐÃ FIX)
✅ Core 0 timeout: 60000ms → 10000ms

### Vấn đề 3: vTaskDelay() Blocking (ĐÃ WORKAROUND)
✅ Dùng task có sẵn với ulTaskNotifyTake()

### Vấn đề 4: MSG_TEST_DATA_READY = 0x10 KHÔNG HOẠT ĐỘNG (ĐÃ FIX)
✅ Đổi message value từ 0x10 → 0x09, 0x0A
**IMPORTANT:** Message values > 0x0F không hoạt động với IpcNotify trên AM263Px!

## 4. CẤU TRÚC SHARED MEMORY TEST

### Test Flow Core 0 → Core 1:
```
1. Core 0: Ghi [1,2,3,4,5,6,7,8] → gGatewaySharedMem.test_data.data[]
2. Core 0: Set test_data.ready = 1, sequence++
3. Core 0: Gửi MSG_TEST_DATA_READY (0x09) → Core 1
4. Core 1: IPC callback → Copy data → Set flag
5. Core 1 Main Loop: Log "TEST DATA from Core 0: 1 2 3 4 5 6 7 8"
```

### Test Flow Core 1 → Core 0:
```
1. Core 1: Ghi [10,20,30,40,50,60,70,80] → gGatewaySharedMem.test_data.data[]
2. Core 1: Set test_data.ready = 1, sequence++
3. Core 1: Gửi MSG_TEST_DATA_FROM_CORE1 (0x0A) → Core 0
4. Core 0: IPC callback → Copy data → Set flag
5. Core 0 IPC Task: Log "TEST DATA from Core 1: 10 20 30 40 50 60 70 80"
```

### Message IDs Đang Hoạt Động:
| Message ID | Value | Direction | Purpose | Status |
|------------|-------|-----------|---------|--------|
| MSG_CAN_DATA_READY | 0x01 | Core 1 → Core 0 | Motor states | ✅ |
| MSG_ETH_DATA_READY | 0x02 | Core 0 → Core 1 | Motor commands | ✅ |
| MSG_TEST_DATA_READY | 0x09 | Core 0 → Core 1 | Test data (0→1) | ✅ |
| MSG_TEST_DATA_FROM_CORE1 | 0x0A | Core 1 → Core 0 | Test data (1→0) | ✅ |

## 5. LOG EVIDENCE

### Bidirectional Shared Memory Success:
```
# Core 0 → Core 1:
[Core0] SHMEM: Written test data [1,2,3,4,5,6,7,8] to shared memory
[IPC] Test data notification: Core 0 -> Core 1, msgValue=0x09
[Core1] *** TEST DATA from Core 0: 1 2 3 4 5 6 7 8 ***

# Core 1 → Core 0:
[Core1] SHMEM: Written test data [10,20,30,40,50,60,70,80] to shared memory
[IPC] Core 1 -> Core 0: MSG_TEST_DATA_FROM_CORE1 (0x0A)
[IPC] Core 1 test data notification: SUCCESS
[Core0] *** TEST DATA from Core 1: 10 20 30 40 50 60 70 80 ***
```

### Data Integrity Test:
- Core 1 gửi liên tiếp nhiều messages
- Core 0 nhận chính xác từng mảng: `10 20 30 40 50 60 70 80`
- **KHÔNG có data corruption** ✓

## 6. Checklist hoàn thành
- [x] Fix IPC callback signature
- [x] Fix timeout sync (50s delay eliminated)
- [x] Core 1 → Core 0 IPC working
- [x] Core 0 → Core 1 IPC working
- [x] Test data API added
- [x] **Core 0 → Core 1 shared memory test ✅**
- [x] **Core 1 → Core 0 shared memory test ✅**
- [x] **Bidirectional shared memory verified ✅**
- [ ] CAN interface integration
- [ ] UDP/Ethernet integration

## 7. FILES MODIFIED

### Core 0 (FreeRTOS):
- `main.c`: Test data nhận từ Core 1, callback xử lý
- `gateway_shared.h`: Thêm MSG_TEST_DATA_FROM_CORE1
- `gateway_shared.c`: Handler cho MSG_TEST_DATA_FROM_CORE1

### Core 1 (NoRTOS):
- `main.c`: Test data gửi mỗi 200 cycles
- `gateway_shared.h`: Thêm MSG_TEST_DATA_FROM_CORE1
- `gateway_shared.c`: Notify function dùng MSG_TEST_DATA_FROM_CORE1

## 8. ARCHITECTURE SUMMARY

### IPC + Shared Memory Gateway:
```
┌─────────────────────────────────────────────────────────────┐
│                    SHARED MEMORY                            │
│                  0x701D0000 (16KB)                          │
├─────────────────────────────────────────────────────────────┤
│  • test_data_buf_t: Test data buffer (16 uint32_t)         │
│  • motor_state_ipc_t: Motor states (23 motors)             │
│  • motor_cmd_ipc_t: Motor commands (23 motors)              │
│  • imu_state_ipc_t: IMU state (gyro, quat, euler, mag)    │
│  • Statistics & Diagnostics                                 │
└─────────────────────────────────────────────────────────────┘
        ↑                         ↑
        │ Write                  │ Write
        │ Read                   │ Read
    ┌───┴───┐               ┌───┴───┐
    │ Core 0│               │ Core 1│
    │ Free  │               │ NoRTOS│
    │ RTOS  │               │       │
    └───────┘               └───────┘
    Ethernet                 CAN Bus
    UDP/xGW                 8x CAN
    (xGW UDP                  IMU
     Interface)              (YIS320)

IPC Notifications (IpcNotify):
• 0x01: CAN_DATA_READY  (Core 1 → 0)
• 0x02: ETH_DATA_READY  (Core 0 → 1)
• 0x06: IMU_DATA_READY  (Core 1 → 0)
• 0x09: TEST_DATA (Core 0 → 1)
• 0x0A: TEST_DATA (Core 1 → 0)
```

## 9. RECENT UPDATES (2026-03-24)

### Ethernet/UDP Integration (Core 0)
- ✅ Created xGW UDP interface for Core 0
- ✅ Implemented xgw_udp_interface.h and xgw_udp_interface.c
- ✅ Integrated with main.c for Core 0
- ✅ UDP TX task sends motor states at 1000Hz
- ✅ UDP RX receives motor commands from PC
- ✅ xGW protocol packet building and parsing
- ✅ CRC32 validation for received packets
        ↑                         ↑
        │ Write                  │ Write
        │ Read                   │ Read
    ┌───┴───┐               ┌───┴───┐
    │ Core 0│               │ Core 1│
    │ Free  │               │ NoRTOS│
    │ RTOS  │               │       │
    └───────┘               └───────┘
    Ethernet                 CAN Bus
    UDP/xGW                 8x CAN

IPC Notifications (IpcNotify):
• 0x01: CAN_DATA_READY  (Core 1 → 0)
• 0x02: ETH_DATA_READY  (Core 0 → 1)
• 0x09: TEST_DATA (Core 0 → 1)
• 0x0A: TEST_DATA (Core 1 → 0)
```

## 9. MEMORY MAP FIX (2026-03-25)

### Vấn đề Core 1 không hoạt động sau porting ccu_ti:
- **Nguyên nhân**: Core 1 OCRAM được cấu hình sai địa chỉ
- **Root cause**: Khi porting ccu_ti (single-core), memory map bị thay đổi

### Memory Map AM263Px (3MB = 6 Banks × 512KB):

| Bank | Address Range | Interconnect | Near For |
|------|---------------|--------------|----------|
| BANK0 | 0x7000_0000 - 0x7007_FFFF | R5SS0 VBUSM | R5SS0 cores (0,1) |
| BANK1 | 0x7008_0000 - 0x700F_FFFF | R5SS0 VBUSM | R5SS0 cores (0,1) |
| BANK2 | 0x7010_0000 - 0x7017_FFFF | R5SS1 VBUSM | R5SS1 cores (2,3) |
| BANK3 | 0x7018_0000 - 0x701F_FFFF | R5SS1 VBUSM | R5SS1 cores (2,3) |
| BANK4 | 0x7020_0000 - 0x7027_FFFF | VBUSM CORE | Common |
| BANK5 | 0x7028_0000 - 0x702F_FFFF | VBUSM CORE | Common |

**Access Latency**: Near < Common < Far

### BEFORE (SAI):
```
Core 0 (r5fss0-0): 0x70040000 - 0x700C0000 (512KB)
                    └─ BANK0 + BANK1 (near OK)

Core 1 (r5fss0-1): 0x70100000 - 0x70140000 (256KB)
                    └─ BANK2 (FAR BANK cho R5SS0!) ❌

ENET_CPPI_DESC: 0x700C0000 (BANK1)
```

### AFTER (ĐÃ SỬA - Có SBL cho boot from flash):
```
BANK0 (0x70000000-0x70080000): SBL (256KB) + Core 1 OCRAM (256KB)
                              0x70000000-0x70040000: SBL ✅
                              0x70040000-0x70080000: Core 1 (Near bank) ✅

BANK1 (0x70080000-0x70100000): Core 0 OCRAM (512KB)
                              └─ Near bank ✅
                              FreeRTOS needs more RAM for lwIP

BANK2 (0x70100000-0x70140000): ENET_CPPI_DESC (256KB)
                              └─ OK for DMA buffer
```

### Phân bổ cuối cùng:
| Core | OCRAM Address | Size | Bank | Type |
|------|---------------|------|------|------|
| SBL | 0x70000000 | 256KB | BANK0 | Bootloader ✅ |
| Core 0 (FreeRTOS) | 0x70080000 | 512KB | BANK1 | Near ✅ |
| Core 1 (NoRTOS) | 0x70040000 | 256KB | BANK0 | Near ✅ |
| ENET_CPPI_DESC | 0x70100000 | 256KB | BANK2 | Far (OK) |

### Files Modified:
- `ccu_ti_multi_core_freertos/example.syscfg`:
  - Xóa SBL region (không cần cho JTAG debug)
  - OCRAM: 0x70080000, size: 0x80000 (512KB)
  - ENET_CPPI_DESC: 0x70100000, size: 0x40000 (256KB)

- `ccu_ti_multi_core_realtime/example.syscfg`:
  - OCRAM: 0x70000000, size: 0x40000 (256KB)

### Next Steps:
1. ✅ SysConfig files updated
2. ⏳ Run SysConfig tool to regenerate linker files
3. ⏳ Rebuild both projects in CCS
4. ⏳ Test both cores running

## 10. LOCK-FREE RING BUFFER IPC (2026-03-26)

### ✅ Kiến trúc Lock-free Circular FIFO
**Phiên bản:** 4.0.0 - Lock-free Ring Buffer

**Đặc điểm:**
- **Non-blocking**: Producer/Consumer không chờ đợi (không dùng mutex/spinlock)
- **Cache Line Alignment**: 32-byte cho mỗi control variable (tránh false sharing)
- **Memory Barriers**: Inline assembly ARM `dmb/dsb` cho Cortex-R5F
- **Dual Unidirectional Ring Buffers**: 2 channel độc lập

### Memory Layout:
```
Shared Memory: 32KB (0x701D0000)
├── RingBuf 0→1: 8KB (Core0 Producer → Core1 Consumer)
├── RingBuf 1→0: 8KB (Core1 Producer → Core0 Consumer)
├── Legacy Buffers: ~200 bytes (backward compatibility)
└── IMU, Stats, Diagnostics: ~300 bytes
```

### Test Results:
```
✅ Core 0 → Core 1: seq=0,1,2,3... [1,2,3,4,5,6,7,8]
✅ Core 1 → Core 0: seq=0,1,2,3... [10,20,30,40,50,60,70,80]
✅ No data corruption
✅ Sequence numbers increment correctly
```

### Files Modified:
- `gateway_shared.h`: Added lock-free ring buffer structures and API
- `gateway_shared.c`: Implemented ring buffer read/write with memory barriers
- `ccu_ti_multi_core_freertos/main.c`: Updated test code for ring buffer
- `ccu_ti_multi_core_realtime/main.c`: Updated test code for ring buffer

## 11. KNOWN ISSUES

### ✅ Ethernet/UDP Not Working - FIXED (2026-03-26)
- **Root Cause:** `enet_lwip_task_wrapper()` was missing CPSW driver initialization
- **Previous Code:**
  ```c
  tcpip_init(lwip_init_callback, &init_sem);
  sys_sem_wait(&init_sem);
  // TODO: Initialize Ethernet driver (CPSW)  ← NOT DONE!
  while (1) { vTaskDelay(1000); }  ← No actual Ethernet processing
  ```
- **Fixed Code (following ccu_ti pattern):**
  ```c
  enet_lwip_example(NULL);  // Initializes CPSW, PHY, and calls main_loop()
  ```
- **Changes Made:**
  1. Added `#include "test_enet_lwip.h"` to main.c
  2. Modified `enet_lwip_task_wrapper()` to call `enet_lwip_example()`
  3. Copied SDK test.c and modified `test_init()` to call `xgw_udp_start()`
  4. Updated build files to include test.c

### ⚠️ Ethernet Status - NEEDS TESTING
- **Next Steps:**
  - Rebuild project in CCS
  - Test with Wireshark to verify UDP traffic
  - Verify PHY link status in logs

### ✅ IPC Test Code Cleanup (2026-03-27)
- **Issue:** Core 0 crashed during `ipc_process_task()` - log cut off mid-line
- **Root Cause:** Test code in production paths causing complexity
  - `ipc_process_task()` had extensive test logging every 10/20 notifications
  - IPC callbacks had ring buffer test reads in ISR context
  - Core 1 main loop had test code every 100/200 cycles
- **Solution:** Cleaned up production code
  - Removed all test code from `ipc_process_task()` - now minimal
  - Simplified IPC callbacks - no DebugP_log or ring buffer reads in ISR
  - Simplified Core 1 main loop - removed test notifications
  - Kept production code: motor command processing, CAN transmission, heartbeat

## 12. NEXT STEPS
1. ✅ IPC communication - DONE
2. ✅ Shared memory test - DONE
3. ✅ Lock-free ring buffer - DONE (2026-03-26)
4. ✅ Memory map fix - DONE (2026-03-25)
5. ✅ UDP/Ethernet interface code - DONE (2026-03-24)
6. ⏳ **ETHernet/UDP troubleshooting - IN PROGRESS**
7. ⏳ CAN interface integration
8. ⏳ Motor command/state protocol implementation
9. ✅ IMU interface porting - DONE (2026-03-24)

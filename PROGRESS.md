# Nhật ký Dự án: CCU Multicore Gateway IPC

## 1. Thông tin chung
- **Ngày cập nhật:** 2026-03-23 10:00
- **Trạng thái:** ✅ BIDIRECTIONAL SHARED MEMORY HOÀN THÀNH!
- **Cấu hình phần cứng:** AM263Px LaunchPad (R5FSS0-0 FreeRTOS, R5FSS0-1 NoRTOS).

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

IPC Notifications (IpcNotify):
• 0x01: CAN_DATA_READY  (Core 1 → 0)
• 0x02: ETH_DATA_READY  (Core 0 → 1)
• 0x09: TEST_DATA (Core 0 → 1)
• 0x0A: TEST_DATA (Core 1 → 0)
```

## 9. NEXT STEPS
1. ✅ IPC communication - DONE
2. ✅ Shared memory test - DONE
3. ⏳ CAN interface integration
4. ⏳ UDP/Ethernet integration
5. ⏳ Motor command/state protocol implementation

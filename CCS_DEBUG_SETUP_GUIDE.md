# CCS Debug Configuration Guide - Cross-Cluster Multicore

## Vấn đề đã xác định

Khi debug cross-cluster cores (r5fss0-0 + r5fss1-1):
- **Core0 (R5F0)**: Hoạt động đúng - halt khi start, continue khi stop
- **Core3 (R5F3)**: KHÔNG halt khi start debug lần 2 - vẫn running từ lần trước

## Nguyên nhân

CCS Theia cache debug state cho mỗi core. Với cross-cluster:
- Core0 được cache đúng vì là core đầu tiên trong cluster
- Core3 (khác cluster) không được reset/halt đúng do CCS multicore debug limitation

## Giải pháp: Cấu hình Debug Properties thủ công

### Bước 1: Mở Debug Configuration

**Cách 1:**
1. Click chuột phải vào project
2. **Debug As** → **Debug Configurations...**

**Cách 2:**
1. Menu **Run** → **Debug Configurations...**

### Bước 2: Cấu hình cho Core0 (FreeRTOS)

1. Chọn **CCS Debug** → **ccu_ti_multi_core_freertos Debug**
2. Tab **Main**:
   - Project: `ccu_ti_multi_core_freertos`
   - Target Config: `targetConfigs/AM263Px_R5F0.ccxml`

3. Tab **Debugger** → **Main**:
   - **Auto detect and run to main**: ✓ Check
   - **Enable Breakpoints**: ✓ Check

4. Tab **Debugger** → **Advanced**:
   ```
   Connection Settings:
   - Reset on connect: ✓ Check
   - Delay after reset (ms): 100
   - Halt on connect: ✓ Check

   Disconnect Settings:
   - Halt on disconnect: ✗ Uncheck (QUAN TRỌNG!)
   - Reset on disconnect: ✗ Uncheck (QUAN TRỌNG!)
   ```

5. Click **Apply**

### Bước 3: Cấu hình cho Core3 (NoRTOS)

1. Chọn **CCS Debug** → **ccu_ti_multi_core_realtime Debug**
2. Tab **Main**:
   - Project: `ccu_ti_multi_core_realtime`
   - Target Config: `targetConfigs/AM263Px_R5F3.ccxml`

3. Tab **Debugger** → **Main**:
   - **Auto detect and run to main**: ✓ Check
   - **Enable Breakpoints**: ✓ Check

4. Tab **Debugger** → **Advanced**:
   ```
   Connection Settings:
   - Reset on connect: ✓ Check
   - Delay after reset (ms): 200 (tăng lên cho Core3)
   - Halt on connect: ✓ Check
   - Force halt before connect: ✓ Check (THÊM VÀO!)

   Disconnect Settings:
   - Halt on disconnect: ✗ Uncheck (QUAN TRỌNG!)
   - Reset on disconnect: ✗ Uncheck (QUAN TRỌNG!)
   ```

5. Click **Apply** → **Close**

### Bước 4: Xóa CCS Cache (QUAN TRỌNG!)

Để Core3 halt đúng khi start lần 2:

1. **Close CCS hoàn toàn**
2. Xóa cache folders:
   ```bash
   # Windows
   rm -rf %USERPROFILE%\.ccs\metadata\.plugins\org.eclipse.cdt.dsf\*
   rm -rf %USERPROFILE%\.ccs\metadata\.plugins\org.eclipse.cdt.core\*

   # Hoặc tìm và xóa thư mục này:
   # C:\Users\<username>\.ccs\workspace\.metadata\.plugins\
   ```

3. **Reopen CCS Theia**

### Bước 5: Debug Workflow

**Lần đầu (initial debug):**
1. Start debug Core0 → Core0 reset + halt ✓
2. Start debug Core3 → Core3 reset + halt ✓
3. Set breakpoints, debug...

**Stop debug:**
1. Click **Stop (nút vuông)** → Cores continue running
2. Verify cores running via LED/UART/...

**Lần thứ 2+ (subsequent debug):**
1. Start debug Core0 → Core0 reset + halt ✓
2. Start debug Core3 → **Core3 PHẢI reset + halt** ✓

## Troubleshooting

### Core3 vẫn running khi start lần 2

**Giải pháp 1: Manual halt trước khi debug**
```
CCS Debug Console:
> halt core 3
> reset core 3
```

**Giải pháp 2: Tăng reset delay**
Trong Debugger → Advanced:
- Delay after reset: 200 → 500ms

**Giải pháp 3: Xóa cache lại**
- Close CCS
- Delete: `%USERPROFILE%\.ccs\workspace\.metadata\.plugins\org.eclipse.cdt.dsf\`
- Reopen CCS

### Core bị halt sau stop debug

**Check:**
- Disconnect settings có "Halt on disconnect" uncheck chưa?

**Manual resume:**
```
CCS Debug Console:
> run core 0
> run core 3
```

Hoặc dùng script:
```batch
resume_cores.bat
```

## GDB Script Alternative

Nếu CCS debug config không work, dùng GDB CLI:

```batch
# Debug Core0
cd D:\VinDynamics\Project\CCU_TI_RS485_CAN\2.Firmware\ccu_ti_mutilcore
arm-none-eabi-gdb -x ccu_ti_multi_core_freertos/gdb_init_r5f0.txt

# Debug Core3
arm-none-eabi-gdb -x ccu_ti_multi_core_realtime/gdb_init_r5f3.txt
```

## Verification

Test workflow:
1. ✓ Start debug Core0 → halt tại main
2. ✓ Start debug Core3 → halt tại main
3. ✓ Stop cả 2 → cores continue running
4. ✓ Start debug Core0 lại → halt
5. ✓ Start debug Core3 lại → **halt (không running!)**

# Stub Registry - CCU Multicore Project

Track incomplete implementations, TODOs, and hardcoded values.

---

## Priority Levels

- **CRITICAL**: Blocks functionality, must fix immediately
- **HIGH**: Degrades functionality, should fix soon
- **MEDIUM**: Works but needs improvement
- **LOW**: Nice to have, can defer

---

## Active Stubs

| ID | Location | Description | Priority | Assigned | Status |
|----|----------|-------------|----------|----------|--------|
| S001 | freertos/main.c:219 | TODO: Enable GPIO instrumentation pins in SysConfig | MEDIUM | - | PENDING |
| S002 | realtime/main.c:220 | TODO: Enable GPIO instrumentation pins in SysConfig | MEDIUM | - | PENDING |
| S003 | realtime/main.c:469 | TODO: Enable GPIO instrumentation pins in SysConfig | MEDIUM | - | PENDING |
| S004 | realtime/main.c:470 | TODO: Enable GPIO instrumentation pins in SysConfig | MEDIUM | - | PENDING |
| S005 | realtime/main.c:499 | TODO: Enable GPIO instrumentation pins in SysConfig | MEDIUM | - | PENDING |
| S006 | realtime/main.c:500 | TODO: Enable GPIO instrumentation pins in SysConfig | MEDIUM | - | PENDING |
| S007 | realtime/main.c:591 | dispatcher_timer not properly integrated (callback never registered) | LOW | - | DEPRECATED |
| S008 | realtime/main.c:736 | TODO: Enable GPIO instrumentation pins in SysConfig | MEDIUM | - | PENDING |
| S009 | realtime/main.c:742 | TODO: Enable GPIO instrumentation pins in SysConfig | MEDIUM | - | PENDING |
| S010 | realtime/main.c:750 | TODO: Enable GPIO instrumentation pins in SysConfig | MEDIUM | - | PENDING |
| S011 | realtime/main.c:757 | TODO: Enable GPIO instrumentation pins in SysConfig | MEDIUM | - | PENDING |

---

## Completed Stubs

| ID | Description | Resolution | Date Completed |
|----|-------------|------------|----------------|
| S101 | Emergency stop handler | Implemented basic handler in gateway_shared.c | 2026-03-31 |

---

## Stub Template for New Entries

```markdown
| ID | Location | Description | Priority | Assigned | Status |
|----|----------|-------------|----------|----------|--------|
| SXXX | file.c:line | Brief description | PRI | assignee | PENDING/IN_PROGRESS/DONE |
```

**Details:**
- Current implementation: What it does now
- Required implementation: What it should do
- Dependencies: What blocks this stub
- Estimated effort: Time/complexity

---

## GPIO Instrumentation Notes

The following GPIO pins are reserved for debug instrumentation but need SysConfig enable:

| Pin | Function | Core | Purpose |
|-----|----------|------|---------|
| PA0 | Heartbeat | Core1 | 1Hz toggle for liveness check |
| PA1 | Timer ISR | Core1 | 1000Hz toggle for timing measurement |
| PA2 | CAN RX ISR | Core1 | Toggle on CAN RX for traffic monitoring |
| PA3 | IMU UART ISR | Core1 | Toggle on IMU data receive |

**Action Required:** Enable these pins in TI SysConfig before uncommenting GPIO debug code.

---

## Related Files

- `SESSION_LOG.md` - Daily log of stub completion
- `BUG_REGISTRY.md` - Known bugs vs incomplete implementations
- `CLAUDE.md` - Project guidelines and architecture

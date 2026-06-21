## Lab4 已完成內容（可直接沿用）

### Basic Part
- **Exception/Trap 機制**：完成 S-mode 例外與中斷處理路徑，並調整 trap handler 結構與流程。
- **Core Timer（SBI）**：完成以 SBI timer extension 驅動的計時中斷流程，支援週期性重設與開/關控制。
- **UART0 + PLIC 非同步 I/O**：完成 UART 中斷化收發（RX/TX）與對應穩定性修正，包含輸出問題與 nested interrupt 下的 race condition 保護（critical section）。
- **平台整合**：完成 OrangePi RV2 相關支援與平台參數修正（包含 timer/frequency 相關調整）。

### Advanced Part
- **Advanced 1（Timer Multiplexing）**：完成 timer API 與 timeout 機制，將計時邏輯從 trap 路徑抽離成獨立 timer 模組。
- **Advanced 2（Concurrent I/O + Nested Interrupt）**：完成 task queue 與優先序任務處理，將中斷 handler 與實際處理解耦，支援在中斷情境中的 preemption/nested execution。

### 程式架構整理（Lab4 新增/重構）
- 已將 Lab4 核心邏輯模組化為 **trap / timer / task / uart** 分工。
- `timer.c` / `timer.h`：timer API、timeout 與硬體 timer 重設流程。
- `task.c` / `task.h`：任務佇列、優先序與中斷情境執行邏輯。
- `uart.c` / `uart.h`：UART 中斷收發、buffer/同步保護與輸出修正。
- `trap.c`：例外/中斷分派與 nested interrupt 流程銜接。

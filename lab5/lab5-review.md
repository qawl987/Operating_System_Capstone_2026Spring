## Lab5 已完成內容

### Basic Part
- **Thread 機制**：完成 thread 建立、獨立 kernel stack、round-robin run queue、`switch_to` context switch，以及 idle thread 回收 zombie thread 的流程。
- **User Process 與 Trap Frame**：完成 user program 載入與 U-mode 執行，透過 trap frame 保存/還原 user context，支援 `exec`、`fork`、`waitpid`、`exit`、`stop` 等 process 操作。
- **System Call 介面**：完成 Lab5 所需 syscall number 與 handler，包含 `getpid`、`uart_read/write`、`exec`、`fork`、`waitpid`、`exit`、`stop`、`display`、`usleep` 等。
- **Timer Preemption 與 Sleep**：將 timer interrupt 接上 scheduler，支援 1/32 秒週期性 preemption，並以 `usleep` / wakeup queue 讓 user process 可以 sleep 後再被喚醒。
- **Video Player**：完成 framebuffer display syscall，支援 QEMU ramfb 與 Orange Pi RV2 HDMI framebuffer，並在 RV2 上使用 `cbo.flush` 將 cache 內容同步給 display controller。

### Advanced Part
- **POSIX Signal**：完成 `signal`、`kill`、`sigreturn`，支援 user-space signal handler、signal trampoline、handler 專用 user stack，以及 handler 結束後恢復原本 trap frame。
- **Signal + Process 整合**：fork 後的 child 可繼承已註冊 handler；未註冊 handler 時依預設行為終止 process；`sigreturn` 會輸出 spec 要求的驗證訊息。

### Orange Pi RV2 修正與穩定性
- **Framebuffer Cache Flush**：針對 RV2 實體 HDMI，逐 cache line 執行 `cbo.flush`，避免畫面因 cache stale 而定格或顯示舊 frame。
- **Framebuffer 單一 Owner**：避免多個 video child 同時推進同一支影片造成 2x 播放速度；同一時間只有第一個 display process 會真正寫 framebuffer，owner 在 exit/stop/free 時釋放。
- **Thread List Critical Section**：保護 `all_threads`、`run_queue`、`zombie_queue` 的插入、刪除與 timer wakeup 掃描，避免 RV2 timer interrupt 在 list mutation 中途打進來造成 list 損壞，進而導致 shell 與 video 卡死。
- **UART/Task 穩定性**：調整 UART 輸出與 task callback 執行時的中斷狀態，避免在關中斷區間內長時間輸出或遇到 nested interrupt race。

### 檔案統整（Lab5 新增/修改）

#### 新增/主要模組
- `include/thread.h`, `src/thread.c`：thread/process 資料結構、run queue、zombie 回收、fork/exec/wait/stop/usleep、signal 狀態與 pending signal 檢查。
- `include/syscall.h`, `src/syscall.c`：syscall number 分派與 user/kernel 參數傳遞，串接 process、UART、framebuffer、signal 相關服務。
- `include/framebuffer.h`, `src/framebuffer.c`：QEMU ramfb 初始化、RV2 framebuffer 寫入、cache flush、display ownership 管理。
- `src/switch.S`：保存/還原 callee-saved registers 與 `tp` thread pointer 的 context switch。

#### 主要修改檔案
- `src/start.S`：加入 exception entry / `ret_from_exception`，保存 trap frame 並支援 S-mode 與 U-mode trap return。
- `src/trap.c`：完成 syscall、timer interrupt、external interrupt 分派，整合 timer preemption、UART IRQ、task queue 與 pending signal 檢查。
- `src/timer.c`, `include/timer.h`：延續 Lab4 timer multiplexing，加入 scheduler wakeup 所需的週期性 timer programming。
- `src/uart.c`, `include/uart.h`：支援 interrupt-driven UART 收發，供 shell 與 user syscall 共用。
- `src/main.c`：初始化 Lab5 thread/trap/framebuffer/syscall 子系統，提供 `exec osctest.bin` 等 shell 流程。
- `include/config.h`：加入 QEMU / Orange Pi RV2 的 user image、framebuffer、timer、PLIC、UART 等平台參數。
- `Makefile`, `kernel.its`：整合 Lab5 相關模組、initramfs 與 Orange Pi RV2 `kernel.fit` 建置流程。

## Lab3 已完成內容（可直接沿用）

### Basic Part
- **Buddy System Page Allocator**：完成以 page frame 為單位的配置/釋放與 buddy merge/split，支援 `alloc_pages` / `free_pages`、order-based free list 管理與狀態追蹤。
- **Dynamic Memory Allocator（kmalloc）**：完成 chunk pool（16~2048 bytes）與大於 pool 大小時回退到 buddy page allocation 的流程，提供 `allocate/free` 測試介面。
- **記憶體初始化整合**：完成開機後記憶體子系統初始化，讓 shell 可直接執行 `alloc_test` 驗證 allocator 行為。

### Advanced Part
- **Advanced 1（Efficient Page Allocation）**：以 frame array + linked-list 管理 free area，維持快速 split/merge 與配置路徑。
- **Advanced 2（Reserved Memory）**：支援從 DTB 解析 `/memory` 與 `/reserved-memory`，並保留 DTB/kernel/initramfs/relocation 區間，避免配置踩到保留區。
- **Advanced 3（Startup Allocation）**：完成 early-boot bump-pointer startup allocator，先配置 frame array，再交棒給 buddy/kmalloc，解決記憶體初始化 chicken-and-egg 問題。

### 檔案統整（Lab3 新增/修改）

#### 新增檔案
- `include/list.h`：circular doubly linked list 基礎容器（free list/chunk list 共用）。
- `include/buddy.h`, `src/buddy.c`：buddy allocator 核心資料結構、page 配置/釋放、merge/split、reserved region hole-punch。
- `include/kmalloc.h`, `src/kmalloc.c`：dynamic allocator（chunk pools + page fallback）與 `alloc_test` 測試接口。
- `include/startup_alloc.h`, `src/startup_alloc.c`：startup allocator、reserved region 表、整體 memory init 流程。
- `include/logger.h`：分級 logging（spec/info/debug）統一輸出行為。

#### 主要修改檔案
- `src/main.c`：改為呼叫 `startup_memory_init(...)` 建立 Lab3 記憶體子系統，並加入 `alloc_test` 指令流程。
- `include/dtbParser.h`, `src/dtbParser.c`：擴充 DTB 解析能力（memory region、reserved-memory、subnode 走訪）。
- `include/config.h`：加入平台相依記憶體參數與 log level 等設定，支援 QEMU/OrangePi RV2。
- `Makefile`：納入 Lab3 新增模組（buddy/kmalloc/startup allocator/logger）編譯。
- `src/link.ld`, `src/link_pi.ld`：配合記憶體保留與平台配置調整 linker 相關配置。

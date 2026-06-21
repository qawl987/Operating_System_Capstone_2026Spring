## Lab6 已完成內容

### Basic Part
- **Kernel Virtual Memory**：完成 RISC-V Sv39 page table 初始化，開機時建立 identity mapping 與 higher-half kernel mapping，寫入 `satp` 啟用 MMU，並在跳轉到 higher-half 後移除低位址 identity mapping。
- **Higher-half Kernel Layout**：將 kernel linker script 調整到 `0xffffffc000000000`，並透過 `PAGE_OFFSET` 提供 kernel 對實體記憶體的 linear mapping 存取。
- **Kernel / MMIO Mapping**：使用 PMD 2 MiB page 建立 boot-time mapping，RAM 使用 `V-R-W-X-G-A-D`，低位址 MMIO 區域使用 non-executable 的 `V-R-W-G-A-D`，讓 UART、PLIC、timer、framebuffer 等既有 driver 在 VM 開啟後仍可正常存取。
- **User Address Space Isolation**：每個 user process 建立獨立 PGD，只複製 kernel half mapping，user half 由 process 自己的 image、stack、mmap VMA 管理，context switch 時切換到該 process 的 page table。
- **User Program Mapping**：`exec` / spawn user program 時，將 user image 映射到 `USER_TEXT_VA` 並設成 user executable/readable page，user stack 映射到高位址 stack top，維持 U-mode 與 S-mode address space 隔離。
- **Page Fault Handling**：trap handler 會辨識 instruction/load/store page fault；合法 fault 交給 process VM handler 處理，非法存取則印出 `[Segmentation fault]: Kill Process` 並終止 process。

### Advanced Part
- **Advanced 1（mmap）**：完成 anonymous `mmap` syscall，支援 `PROT_READ`、`PROT_WRITE`、`PROT_EXEC` 與 `MAP_POPULATE`；以 VMA table 記錄每段 mapping 的起訖、權限與 flags，並處理 hint address、page alignment、範圍限制與 overlap 檢查。
- **mmap 權限保護**：依照 VMA prot 產生 user PTE 權限；對 read-only mapping 寫入、對 mapping 外存取、或對不符合權限的 page fault 都會走 segmentation fault 路徑。`mmap_r` 會先通過 in-bounds zero-fill read，再因 out-of-bounds read segfault；`mmap_w` 會因寫入 `PROT_READ` mapping segfault。
- **Advanced 2（Demand Paging）**：未使用 `MAP_POPULATE` 的 anonymous mapping 只先建立 VMA，不立即配置實體頁；第一次合法 access 時由 page fault handler 配置 zero-filled page 並印出 `[Translation fault]: <addr>`。
- **Demand Stack Growth**：user stack 改以 stack VMA 表示，允許 stack top 往下的合法頁面在 page fault 時 demand allocation，支援 `demand` 測資碰觸尚未映射的 stack page。
- **Advanced 3（Copy-on-Write fork）**：`fork` 不再完整複製 user pages，而是複製 page table 結構並共享 user leaf pages；可寫頁會清掉 `PTE_W`、加上 `PTE_COW`，並增加實體 page refcount。
- **CoW Permission Fault**：child 或 parent 寫入 CoW page 時，store page fault handler 會印出 `[Permission fault]: <addr>`；若 refcount 大於 1 會配置新頁並複製內容，否則直接恢復 write permission，最後清除 `PTE_COW` 並 flush TLB。
- **Signal / VM 整合**：signal stack 移到 user stack VMA 下方的獨立頁面區間，避免與 growable user stack 或 mmap range 重疊；fork 後仍保留 Lab5 signal handler 行為，並讓 CoW stack fault 不破壞 signal return 流程。

### QEMU 測試結果
- `exec osctest.bin` 後輸入 `mmap_r`：會印出 mmap address、`[PASS] in-bounds read zero-fill`，接著因 out-of-bounds read 觸發 `[Segmentation fault]: Kill Process`。
- `exec osctest.bin` 後輸入 `mmap_w`：會印出 mmap address，接著因寫入 read-only mapping 觸發 `[Segmentation fault]: Kill Process`。
- `exec osctest.bin` 後輸入 `demand`：合法 demand page fault 會印出 `[Translation fault]: 0x0000003fffffe000`，並回到 shell。
- Advanced 3 後，`mmap_r` / `mmap_w` 前後可能額外出現 `[Permission fault]: 0x0000003ffffff000`，這是 shell/command fork 後 user stack CoW 寫入造成的預期 log。

### 檔案統整（Lab6 新增/修改）

#### 新增/主要模組
- `include/vm.h`, `src/vm.c`：Sv39 PTE flags、kernel/user page table 建立、higher-half mapping、pagewalk、user PGD、virtual-to-physical translation、PTE lookup、CoW page table clone 與 `satp` switch。
- `include/thread.h`, `src/thread.c`：process VMA table、`mmap` syscall backend、anonymous page allocation、page fault handler、demand stack VMA、CoW fork 與 signal stack 位址整合。

#### 主要修改檔案
- `src/start.S`：開機早期呼叫 `setup_vm` 啟用 MMU，切換到 higher-half 後呼叫 `drop_identity_map` 移除 temporary identity mapping。
- `src/link.ld`：將 kernel 連結到 higher-half virtual address，保留 boot code 所需的早期初始化配置。
- `src/trap.c`：新增 user-mode page fault 分派，將合法 VM fault 交給 `process_handle_page_fault`，失敗時輸出 segmentation fault 並結束 process。
- `src/syscall.c`：新增 `SYS_MMAP` 分派，將 user 傳入的 address、length、prot、flags 轉交 process VM 層處理。
- `include/buddy.h`, `src/buddy.c`：提供 page refcount 查詢與遞增介面，供 CoW fork / CoW fault 判斷 shared page 使用。
- `src/startup_alloc.c`：配合 higher-half kernel mapping 與 VM 啟用後的實體/虛擬位址轉換，維持 reserved memory 與 allocator 初始化流程可用。
- `src/framebuffer.c` 與既有 driver：透過 `phys_to_virt` 後的 MMIO / framebuffer 位址繼續運作，保留 Lab5 QEMU ramfb 與 Orange Pi RV2 framebuffer 支援。

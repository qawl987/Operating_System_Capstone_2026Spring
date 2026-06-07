## Lab7 已完成內容

### Basic Part
- **Basic 1（Root File System）**：完成 VFS 介面與 root filesystem 初始化，將 `tmpfs` 掛載為 rootfs；支援 filesystem registration、mount setup、vnode/file handle 抽象，以及 `open` / `close` / `read` / `write` 基本操作。
- **tmpfs**：完成可寫入的 memory-based filesystem，支援 regular file 與 directory vnode；檔案內容存在 kernel memory 中，符合 spec 對 component name、目錄 entry 數量與單檔大小的基本限制。
- **Basic 2（Multi-level VFS）**：支援 `mkdir`、在任意 directory vnode 上 mount filesystem，以及 absolute pathname lookup；lookup 會在 mount point 自動跨到 mounted filesystem root。
- **Basic 3（Multitask VFS）**：每個 thread/process 維護自己的 `fs_root`、`cwd` 與最多 16 個 file descriptor；支援 relative path、`""`、`.`、`..`、連續 `/` normalization 與 `chdir`，並完成 syscall 14~20：
  - `open`（14）
  - `close`（15）
  - `read`（16）
  - `write`（17）
  - `mkdir`（18）
  - `mount`（19）
  - `chdir`（20）
- **fork / fd table 整合**：`fork` 後 child 繼承 parent 的 file descriptor table，同一個 file handle 以 refcount 管理，讓 shared `f_pos` 行為符合同一 fd handle 的語意；thread 回收時會關閉尚未 close 的 fd。
- **Basic 4（/ramfs）**：完成 read-only `ramfs`，開機時建立 `/ramfs` 並掛載 `ramfs`；ramfs 內容由 initramfs cpio 匯入，`create` / `write` / `mkdir` 在 ramfs 上會失敗。
- **exec 與 VFS 整合**：`exec` syscall 改為透過 VFS 開檔並讀入 user image；若使用相對檔名找不到，會 fallback 到 `/ramfs/<name>`，因此 shell 可直接執行 `exec vfs.bin`。

### Advanced Part
- **Advanced 1（/dev/uart）**：完成 `devfs` 並掛載於 `/dev`，提供 device file `/dev/uart`；對該檔案 read/write 會轉接既有 UART driver，行為等同原本 `uart_read` / `uart_write`。
- **stdin / stdout / stderr**：每個 thread/process 初始化時會預開 `/dev/uart` 到 fd `0`、`1`、`2`，讓 user program 可用標準 VFS syscall 進行 console I/O。
- **Advanced 2（/dev/fb）**：`devfs` 提供 `/dev/fb` write-only device file；write 會根據 file handle 的 `f_pos` 寫入 framebuffer linear buffer，並在 Orange Pi RV2 上沿用 cache flush 流程。
- **lseek64 syscall（21）**：完成 VFS `lseek64` 路徑，支援 spec 要求的 `SEEK_SET`，供 `/dev/fb` 重設 framebuffer 寫入 offset。
- **ioctl syscall（22）**：完成 VFS `ioctl` 路徑；`/dev/fb` 支援 request `0`（`FB_IOCTL_GET_INFO`），回傳 framebuffer width、height、bpp。
- **QEMU ramfb 初始化**：在 `/dev/fb` open/write 前會確保 `framebuffer_init()` 已設定 QEMU `fw_cfg` ramfb；Orange Pi RV2 則沿用既有 framebuffer base address。

### QEMU 測試結果
- `make build`：通過。
- `make run_initrd` 後執行 `exec vfs.bin`，在 user program 輸入 `vfs`：
  - Basic Exercise 1：`open/read/write` PASS。
  - Basic Exercise 2：`mkdir`、`mount` PASS。
  - Basic Exercise 3：relative lookup / cwd 相關測試 PASS。
  - Basic Exercise 4：`ramfs` read-only、create/write/mkdir failure 測試 PASS。
  - Advanced Exercise 1：輸入 `x` 後，stdin / stdout / stderr PASS。
- `make run_initrd_vnc` 後執行 `exec vfs.bin`，在 user program 輸入 `vfs_fork`：
  - 可成功 fork child 並進入 framebuffer 測試流程。
  - child 透過 `/dev/fb` 進行 `ioctl` / `lseek64` / `write` framebuffer path；QEMU 未出現 immediate fault/crash。

### 檔案統整（Lab7 新增/修改）

#### 新增/主要模組
- `include/vfs.h`, `src/vfs.c`：VFS core，包含 filesystem registration、mount、pathname lookup、open/close/read/write/lseek64/ioctl dispatch、cwd/root/fd table 輔助流程。
- `include/tmpfs.h`, `src/tmpfs.c`：可寫 memory filesystem，實作 tmpfs vnode/file operations。
- `include/ramfs.h`, `src/ramfs.c`：read-only ramfs filesystem，mount 時從 initramfs cpio populate `/ramfs` 內容。
- `include/devfs.h`, `src/devfs.c`：device filesystem，提供 `/dev/uart` 與 `/dev/fb`，讓 device driver 透過 VFS file operations 暴露給 user process。

#### 主要修改檔案
- `src/main.c`：在 thread system 初始化前呼叫 `vfs_init(...)`，建立 root tmpfs、`/ramfs` 與 `/dev` mount。
- `include/thread.h`, `src/thread.c`：thread 結構新增 `fs_root`、`cwd`、`files[VFS_MAX_FD]`；thread 初始化時預開 stdio，釋放時清理 fd。
- `src/process.c`：`fork` 繼承 file descriptor table，並對 shared file handle 增加 refcount。
- `src/syscall.c`：新增 Lab7 syscall 14~22，並將 `exec` 改為透過 VFS 載入 user image。
- `include/initrd.h`, `src/initrd.c`：新增 cpio iterator，供 ramfs mount 時逐項匯入 initramfs 檔案。
- `include/framebuffer.h`, `src/framebuffer.c`：新增 framebuffer info query 與 offset-based write API，供 `/dev/fb` 的 `ioctl` / `write` 使用。
- `Makefile`：納入 `vfs.c`、`tmpfs.c`、`ramfs.c`、`devfs.c` 編譯。

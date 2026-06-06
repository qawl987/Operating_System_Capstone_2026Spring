import struct
import sys
import os
import time

def main():
    if len(sys.argv) != 3:
        print("用法: python3 send_kernel.py <設備節點> <Kernel檔案路徑>")
        print("範例: python3 send_kernel.py /dev/pts/3 kernel.bin")
        sys.exit(1)

    tty_dev = sys.argv[1]
    kernel_file = sys.argv[2]

    if not os.path.exists(kernel_file):
        print(f"錯誤: 找不到檔案 {kernel_file}")
        sys.exit(1)

    # 讀取 Kernel Binary 內容
    with open(kernel_file, "rb") as f:
        kernel_data = f.read()

    kernel_size = len(kernel_data)
    
    # 建立 Header (Magic Number + Size)
    # '<II' 代表 Little-Endian 的兩個 32-bit Unsigned Integer
    header = struct.pack('<II', 0x544F4F42, kernel_size)

    print(f"準備傳送: {kernel_file}")
    print(f"檔案大小: {kernel_size} bytes")
    print(f"目標設備: {tty_dev}")

    # 打開設備節點並寫入
    try:
        # 使用 buffering=0 確保資料立即送出
        with open(tty_dev, "wb", buffering=0) as tty:
            # 寫入 Header
            tty.write(header)
            time.sleep(0.1)  # Give bootloader time to process header
            
            # 寫入 Kernel Data (in chunks to avoid buffer overflow)
            chunk_size = 256
            for i in range(0, len(kernel_data), chunk_size):
                chunk = kernel_data[i:i+chunk_size]
                tty.write(chunk)
                time.sleep(0.05)  # Increased delay to prevent buffer overflow
                
        print("傳送完成！")
    except PermissionError:
        print(f"權限不足：無法寫入 {tty_dev}。請嘗試使用 sudo，或更改 /dev/pts/X 的權限。")
    except Exception as e:
        print(f"發生錯誤: {e}")

if __name__ == "__main__":
    main()
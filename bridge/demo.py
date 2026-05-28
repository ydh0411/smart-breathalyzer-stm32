#!/usr/bin/env python3
"""演示启动器：创建虚拟串口，模拟设备数据，运行真实 ai_bridge.py"""

import os
import pty        # 创建虚拟串口对（不需要真实设备）
import subprocess
import sys
import threading
import time

# 模拟设备的完整输出序列：启动 → 状态变化 → BLE LOG dump
# 每个元组: (延迟秒数, "串口输出行")
EVENTS = [
    # ---- 设备启动 ----
    (0.5, "# time_ms,raw_adc,filtered_adc,state,baseline,warning_th,danger_th"),
    (0.3, "BLE CMD: W/WAKE, S/SLEEP, C/CAL, T/STAT, L/LOG, H/HELP"),
    # ---- 模拟一个饮酒场景 ----
    (1.0, "LOG,2000,WARMING,820,0%"),       # 预热中
    (1.0, "LOG,30000,SAFE,920,0%"),         # 预热完成，基线校准
    (2.0, "LOG,35000,WARNING,1850,68%"),    # 检测到酒精 → 警告
    (2.0, "LOG,38000,DANGER,2500,145%"),    # 浓度升高 → 危险
    (2.0, "LOG,41000,COOLDOWN,1600,50%"),   # 浓度下降 → 冷却
    (2.0, "LOG,44000,SAFE,950,2%"),         # 恢复正常
    # ---- BLE LOG 命令的返回数据 ----
    (1.5, "--- LOG BOOK (6/32 entries) ---"),
    (0.2, "[  2.0s] WARMING  ADC= 820  LVL=  0%"),
    (0.2, "[ 30.0s] SAFE     ADC= 920  LVL=  0%"),
    (0.2, "[ 35.0s] WARNING  ADC=1850  LVL= 68%"),
    (0.2, "[ 38.0s] DANGER   ADC=2500  LVL=145%"),
    (0.2, "[ 41.0s] COOLDOWN ADC=1600  LVL= 50%"),
    (0.2, "[ 44.0s] SAFE     ADC= 950  LVL=  2%"),
    (0.3, "--- END LOG ---"),              # 触发 AI 分析
]


def run_simulator(fd: int):
    """在后台线程中按时间序列写入模拟数据到 PTY"""
    try:
        with os.fdopen(fd, "w", buffering=1) as f:
            for delay, line in EVENTS:
                time.sleep(delay)
                f.write(line + "\r\n")
                f.flush()
            while True:          # 事件播完后保持运行
                time.sleep(10)
    except (OSError, BrokenPipeError):
        pass                     # 桥接退出后静默结束


def main():
    api_key = os.environ.get("DEEPSEEK_API_KEY") or "sk-cfbf6f9bce2e4f95a13e299a411832bc"

    # 创建虚拟串口对：master 端写入模拟数据，slave 端给桥接读取
    master_fd, slave_fd = pty.openpty()
    slave_name = os.ttyname(slave_fd)   # 获取 slave 端的设备路径 /dev/ttysXXX
    os.close(slave_fd)                  # 关闭 slave fd，桥接会通过路径打开

    print(f"虚拟串口: {slave_name}")
    print("=" * 55)
    print("演示：酒精检测仪 + AI 分析")
    print("- 模拟设备启动 -> 检测到饮酒事件 -> BLE Log Dump")
    print("- AI 自动分析完整 log book")
    print("=" * 55)

    # 启动模拟器线程
    sim_thread = threading.Thread(target=run_simulator, args=(master_fd,), daemon=True)
    sim_thread.start()

    # 运行真实的 ai_bridge.py，连接到虚拟串口
    bridge_path = os.path.join(os.path.dirname(__file__), "ai_bridge.py")
    try:
        subprocess.run(
            [sys.executable, "-u", bridge_path, slave_name, "--baud", "115200"],
            env={**os.environ, "DEEPSEEK_API_KEY": api_key},
            check=False,
        )
    except KeyboardInterrupt:
        print("\n演示结束。")
    finally:
        os.close(master_fd)


if __name__ == "__main__":
    main()

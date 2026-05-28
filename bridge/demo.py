#!/usr/bin/env python3
"""Demo launcher: simulates breathalyzer, triggers AI log book analysis."""

import os
import pty
import subprocess
import sys
import threading
import time

# Simulated events: startup -> state changes -> BLE LOG command dump
EVENTS = [
    # ---- startup ----
    (0.5, "# time_ms,raw_adc,filtered_adc,state,baseline,warning_th,danger_th"),
    (0.3, "BLE CMD: W/WAKE, S/SLEEP, C/CAL, T/STAT, L/LOG, H/HELP"),
    # ---- scenario: preheating -> safe -> warning -> danger -> cooldown -> safe ----
    (1.0, "LOG,2000,WARMING,820,0%"),
    (1.0, "LOG,30000,SAFE,920,0%"),
    (2.0, "LOG,35000,WARNING,1850,68%"),
    (2.0, "LOG,38000,DANGER,2500,145%"),
    (2.0, "LOG,41000,COOLDOWN,1600,50%"),
    (2.0, "LOG,44000,SAFE,950,2%"),
    # ---- user sends L command over BLE, device dumps log book ----
    (1.5, "--- LOG BOOK (6/32 entries) ---"),
    (0.2, "[  2.0s] WARMING  ADC= 820  LVL=  0%"),
    (0.2, "[ 30.0s] SAFE     ADC= 920  LVL=  0%"),
    (0.2, "[ 35.0s] WARNING  ADC=1850  LVL= 68%"),
    (0.2, "[ 38.0s] DANGER   ADC=2500  LVL=145%"),
    (0.2, "[ 41.0s] COOLDOWN ADC=1600  LVL= 50%"),
    (0.2, "[ 44.0s] SAFE     ADC= 950  LVL=  2%"),
    (0.3, "--- END LOG ---"),
]


def run_simulator(fd: int):
    try:
        with os.fdopen(fd, "w", buffering=1) as f:
            for delay, line in EVENTS:
                time.sleep(delay)
                f.write(line + "\r\n")
                f.flush()
            while True:
                time.sleep(10)
    except (OSError, BrokenPipeError):
        pass


def main():
    api_key = os.environ.get("DEEPSEEK_API_KEY")
    if not api_key:
        print("[错误] 请先设置: export DEEPSEEK_API_KEY=sk-xxx")
        sys.exit(1)

    master_fd, slave_fd = pty.openpty()
    slave_name = os.ttyname(slave_fd)
    os.close(slave_fd)

    print(f"虚拟串口: {slave_name}")
    print("=" * 55)
    print("演示：酒精检测仪 + AI 分析")
    print("- 模拟设备启动 -> 检测到饮酒事件 -> BLE Log Dump")
    print("- AI 自动分析完整 log book")
    print("=" * 55)

    sim_thread = threading.Thread(target=run_simulator, args=(master_fd,), daemon=True)
    sim_thread.start()

    bridge_path = os.path.join(os.path.dirname(__file__), "ai_bridge.py")
    try:
        subprocess.run(
            [sys.executable, "-u", bridge_path, slave_name, "--baud", "9600"],
            env={**os.environ, "DEEPSEEK_API_KEY": api_key},
            check=False,
        )
    except KeyboardInterrupt:
        print("\n演示结束。")
    finally:
        os.close(master_fd)


if __name__ == "__main__":
    main()

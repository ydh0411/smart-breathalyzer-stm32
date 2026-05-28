#!/usr/bin/env python3
"""AI Bridge: breathalyzer serial -> DeepSeek API for alerts and chat."""

import argparse
import os
import select
import sys
import time
from datetime import datetime

import serial
from openai import OpenAI

MAX_LOG_ENTRIES = 32

# ---- terminal colors ----
RED = "\033[91m"
YELLOW = "\033[93m"
GREEN = "\033[92m"
GREY = "\033[90m"
RESET = "\033[0m"

SYSTEM_PROMPT = """你是智能酒精检测仪的AI助手，能访问设备的实时数据和事件日志。
用中文回答，简洁直接。可以解释酒精浓度和风险，给出安全建议。
如果被问到能否开车，始终强调"不确定就不要开"。
"""


def timestamp() -> str:
    return datetime.now().strftime("%H:%M:%S")


def parse_args():
    p = argparse.ArgumentParser(description="AI Bridge for Smart Breathalyzer")
    p.add_argument("port", nargs="?", default=None,
                   help="Serial port (e.g. /dev/tty.usbmodem*, COM3). Auto-detect if omitted.")
    p.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default: 115200)")
    p.add_argument("--model", default="deepseek-chat", help="DeepSeek model name")
    return p.parse_args()


def find_port() -> str | None:
    import glob
    candidates = (glob.glob("/dev/tty.usbmodem*") + glob.glob("/dev/tty.usbserial*") +
                  glob.glob("/dev/tty.wchusbserial*") + glob.glob("/dev/tty.SLAB_USBtoUART*"))
    return candidates[0] if candidates else None


def connect_serial(port: str | None, baud: int) -> serial.Serial:
    if port is None:
        port = find_port()
        if port is None:
            print("[错误] 未找到串口设备，请手动指定端口")
            sys.exit(1)
        print(f"[自动检测] 找到设备: {port}")
    ser = serial.Serial(port, baud, timeout=0.1)
    # Reset NUCLEO board via DTR
    ser.dtr = False
    time.sleep(0.1)
    ser.dtr = True
    time.sleep(0.1)
    ser.dtr = False
    time.sleep(1.5)
    # Drain boot messages
    ser.read(8192)
    print(f"[{timestamp()}] 设备已连接: {port} @ {baud} baud")
    return ser


def format_log_entry(e: dict) -> str:
    return f"[{e['ts_s']:6.1f}s] {e['state']:<8} ADC={e['adc']:>4}  LVL={e['level']:>3}%"


def parse_serial_line(line: str, log_buffer: list[dict], current: dict,
                      log_dump_state: dict) -> str | None:
    """Parse one line from serial.

    Returns:
      'danger' / 'warning' — auto-alert from STATE= transition
      'log_dump'           — device finished dumping log book (L command response)
      None                 — no action needed
    """
    line = line.strip()
    if not line:
        return None

    alert = None

    # BLE log dump: "--- LOG BOOK (5/32 entries) ---"
    if line.startswith("--- LOG BOOK"):
        log_dump_state["active"] = True
        log_dump_state["buffer"] = []  # temporary buffer for this dump
        print(f"  {YELLOW}{line}{RESET}")
        return None

    # BLE log dump: "--- END LOG ---"
    if line.startswith("--- END LOG"):
        log_dump_state["active"] = False
        if log_dump_state["buffer"]:
            log_buffer.clear()
            log_buffer.extend(log_dump_state["buffer"])
            log_dump_state["buffer"] = []
        print(f"  {YELLOW}{line}{RESET}")
        return "log_dump"

    # BLE log dump entry: "[  2.0s] WARMING  ADC= 820  LVL=  0%"
    if log_dump_state["active"] and line.startswith("[") and "ADC=" in line:
        try:
            # Parse "[  xx.xs] STATE    ADC=xxxx  LVL=xxx%"
            ts_str = line[1:].split("s]")[0].strip()
            rest = line.split("]")[1]
            state_str = rest.split("ADC=")[0].strip()
            adc_str = rest.split("ADC=")[1].split("LVL=")[0].strip()
            lvl_str = rest.split("LVL=")[1].rstrip("%").strip()
            entry = {
                "ts_s": float(ts_str),
                "state": state_str,
                "adc": int(adc_str),
                "level": int(lvl_str),
            }
            log_dump_state["buffer"].append(entry)
        except (ValueError, IndexError):
            pass
        return None

    if line.startswith("LOG,"):
        parts = line.split(",")
        if len(parts) >= 5:
            entry = {
                "ts_s": int(parts[1]) / 1000.0,
                "state": parts[2].strip(),
                "adc": int(parts[3]),
                "level": int(parts[4].rstrip("%")),
            }
            if not log_dump_state["active"]:
                log_buffer.append(entry)
                while len(log_buffer) > MAX_LOG_ENTRIES:
                    log_buffer.pop(0)
        print(f"  {GREEN}{format_log_entry(log_buffer[-1] if log_buffer else entry)}{RESET}")

    elif line.startswith("DATA,"):
        parts = line.split(",")
        if len(parts) >= 5:
            current["raw_adc"] = int(parts[2])
            current["filtered_adc"] = int(parts[3])
            current["state"] = parts[4].strip()
            current["ts_ms"] = int(parts[1])
        print(f"  {GREY}ADC={current.get('filtered_adc','?')} {current.get('state','?')}{RESET}")

    elif line.startswith("STATE="):
        parts = line.split(",")
        for p in parts:
            k, _, v = p.partition("=")
            if k == "STATE":
                old_state = current.get("state")
                current["state"] = v.strip()
                if old_state != current["state"] and current["state"] == "DANGER":
                    alert = "danger"
                elif old_state != current["state"] and current["state"] == "WARNING":
                    alert = "warning"
            elif k == "AVG":
                current["filtered_adc"] = int(v)
            elif k == "LVL":
                current["level"] = int(v)
        print(f"  {YELLOW}STATE={current.get('state','?')} ADC={current.get('filtered_adc','?')} LVL={current.get('level','?')}%{RESET}")

    else:
        print(f"  {line}")

    return alert


def build_context(log_buffer: list[dict], current: dict) -> str:
    lines = ["--- 事件日志 ---"]
    if log_buffer:
        for e in log_buffer:
            lines.append(format_log_entry(e))
    else:
        lines.append("（暂无事件记录）")
    lines.append(f"\n当前状态: {current.get('state', '?')}")
    lines.append(f"当前ADC: {current.get('filtered_adc', '?')}")
    lines.append(f"当前浓度: {current.get('level', '?')}%")
    return "\n".join(lines)


def call_ai(client: OpenAI, model: str, log_buffer: list[dict], current: dict,
            user_message: str | None = None, stream: bool = True) -> str:
    context = build_context(log_buffer, current)

    if user_message is None:
        user_content = f"检测到状态变化。请根据以下数据给出安全建议：\n\n{context}"
    else:
        user_content = f"设备数据：\n\n{context}\n\n用户问题：{user_message}"

    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": user_content},
    ]

    try:
        response = client.chat.completions.create(
            model=model,
            messages=messages,
            stream=stream,
            max_tokens=400,
        )
        if stream:
            full = ""
            for chunk in response:
                delta = chunk.choices[0].delta.content
                if delta:
                    print(delta, end="", flush=True)
                    full += delta
            print()
            return full
        else:
            content = response.choices[0].message.content
            print(content)
            return content
    except Exception as e:
        msg = f"[错误] API调用失败: {e}"
        print(f"{RED}{msg}{RESET}")
        return msg


def handle_local_command(cmd: str, log_buffer: list[dict], current: dict) -> bool:
    """Handle /commands. Returns True if should quit."""
    cmd = cmd.strip()

    if cmd in ("/quit", "/q"):
        print("再见！")
        return True

    if cmd == "/log":
        print(f"\n{GREEN}--- Log Book ({len(log_buffer)}/{MAX_LOG_ENTRIES}) ---{RESET}")
        if log_buffer:
            for e in log_buffer:
                print(format_log_entry(e))
        else:
            print("（暂无记录）")
        print()

    elif cmd == "/status":
        print(f"\n当前状态:   {current.get('state', '?')}")
        print(f"实时ADC:    {current.get('filtered_adc', '?')}")
        print(f"浓度百分比:  {current.get('level', '?')}%")
        print(f"Log条目数:   {len(log_buffer)}\n")

    elif cmd == "/clear":
        log_buffer.clear()
        print("Log buffer 已清空。")

    elif cmd == "/help":
        print("本地命令: /log /status /clear /quit /help")

    else:
        print(f"未知命令: {cmd}，输入 /help 查看可用命令")

    return False


def handle_alert(client: OpenAI, model: str, alert_type: str,
                 log_buffer: list[dict], current: dict):
    if alert_type == "danger":
        prefix = f"{RED}[! 危险预警]{RESET}"
    else:
        prefix = f"{YELLOW}[! 警告]{RESET}"

    print(f"\n{prefix} 正在分析...")
    print(f"{GREY}┌─ AI 分析 ─────────────────────{RESET}")
    call_ai(client, model, log_buffer, current)
    print(f"{GREY}└────────────────────────────────{RESET}\n")


def handle_log_analysis(client: OpenAI, model: str, log_buffer: list[dict], current: dict):
    """Call AI to analyze the complete log book after a BLE LOG command."""
    context = build_context(log_buffer, current)
    user_content = f"请分析以下酒精检测仪的完整事件日志，给出总体评估和建议：\n\n{context}"

    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": user_content},
    ]

    try:
        response = client.chat.completions.create(
            model=model,
            messages=messages,
            stream=True,
            max_tokens=400,
        )
        for chunk in response:
            delta = chunk.choices[0].delta.content
            if delta:
                print(delta, end="", flush=True)
        print()
    except Exception as e:
        print(f"{RED}[错误] API调用失败: {e}{RESET}")


def main():
    args = parse_args()

    api_key = os.environ.get("DEEPSEEK_API_KEY") or "sk-cfbf6f9bce2e4f95a13e299a411832bc"

    ser = connect_serial(args.port, args.baud)
    client = OpenAI(api_key=api_key, base_url="https://api.deepseek.com")

    log_buffer: list[dict] = []
    current: dict = {}
    last_alert_state: str | None = None
    log_dump_state: dict = {"active": False, "buffer": []}
    line_buf = ""

    print("输入问题与AI对话，发送 L 命令查看 log book 分析\n")

    try:
        while True:
            # Read serial (direct read avoids macOS in_waiting issues)
            try:
                data = ser.read(4096)
                if data:
                        line_buf += data.decode("utf-8", errors="replace")
                        while "\n" in line_buf:
                            idx = line_buf.index("\n")
                            line = line_buf[:idx]
                            line_buf = line_buf[idx + 1:]
                            signal = parse_serial_line(line, log_buffer, current, log_dump_state)
                            if signal == "log_dump":
                                print(f"\n{YELLOW}[Log Book 分析]{RESET}")
                                print(f"{GREY}┌─ AI 分析 ─────────────────────{RESET}")
                                handle_log_analysis(client, args.model, log_buffer, current)
                                print(f"{GREY}└────────────────────────────────{RESET}\n")
                            elif signal and current.get("state") != last_alert_state:
                                last_alert_state = current.get("state")
                                handle_alert(client, args.model, signal, log_buffer, current)
                else:
                    time.sleep(0.05)
            except Exception as e:
                print(f"{RED}[错误] 串口读取失败: {e}{RESET}")
                time.sleep(2)
                continue

            # Check stdin (non-blocking)
            rlist, _, _ = select.select([sys.stdin], [], [], 0.05)
            if rlist:
                user_input = sys.stdin.readline()
                if not user_input:
                    continue
                user_input = user_input.strip()
                if not user_input:
                    continue
                if user_input.startswith("/"):
                    if handle_local_command(user_input, log_buffer, current):
                        return
                else:
                    print(f"{GREY}┌─ AI 回复 ─────────────────────{RESET}")
                    call_ai(client, args.model, log_buffer, current, user_input)
                    print(f"{GREY}└────────────────────────────────{RESET}\n")

    except KeyboardInterrupt:
        print("\n中断退出。")
    finally:
        ser.close()


if __name__ == "__main__":
    main()

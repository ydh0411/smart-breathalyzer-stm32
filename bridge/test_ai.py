#!/usr/bin/env python3
"""End-to-end test of AI bridge without a real device."""

import os
import sys
sys.path.insert(0, os.path.dirname(__file__))

from openai import OpenAI

# Replicate the bridge's core data structures and functions inline for standalone testing
MAX_LOG_ENTRIES = 32

SYSTEM_PROMPT = """你是智能酒精检测仪的AI助手，能访问设备的实时数据和事件日志。
用中文回答，简洁直接。可以解释酒精浓度和风险，给出安全建议。
如果被问到能否开车，始终强调"不确定就不要开"。
"""


def format_log_entry(e: dict) -> str:
    return f"[{e['ts_s']:6.1f}s] {e['state']:<8} ADC={e['adc']:>4}  LVL={e['level']:>3}%"


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
            user_message: str | None = None):
    context = build_context(log_buffer, current)

    if user_message is None:
        user_content = f"检测到状态变化。请根据以下数据给出安全建议：\n\n{context}"
    else:
        user_content = f"设备数据：\n\n{context}\n\n用户问题：{user_message}"

    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": user_content},
    ]

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


def main():
    api_key = os.environ.get("DEEPSEEK_API_KEY")
    if not api_key:
        print("[错误] 请设置 DEEPSEEK_API_KEY")
        sys.exit(1)

    client = OpenAI(api_key=api_key, base_url="https://api.deepseek.com")
    model = "deepseek-chat"

    # Simulated log buffer after a drinking event
    log_buffer = [
        {"ts_s": 5.0, "state": "WARMING", "adc": 820, "level": 0},
        {"ts_s": 30.0, "state": "SAFE", "adc": 800, "level": 0},
        {"ts_s": 45.0, "state": "WARNING", "adc": 1850, "level": 68},
        {"ts_s": 50.0, "state": "DANGER", "adc": 2500, "level": 145},
    ]
    current = {"state": "DANGER", "filtered_adc": 2500, "level": 145}

    print("=" * 50)
    print("测试 1：自动危险预警")
    print("=" * 50)
    call_ai(client, model, log_buffer, current)
    print()

    print("=" * 50)
    print("测试 2：用户对话（能不能开车）")
    print("=" * 50)
    call_ai(client, model, log_buffer, current, "我现在能开车吗？")
    print()


if __name__ == "__main__":
    main()

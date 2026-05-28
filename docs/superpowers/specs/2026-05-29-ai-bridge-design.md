# AI Bridge: 智能酒精检测仪 PC 端 AI 桥接

## Overview

Python 脚本，通过 USB 串口连接酒精检测仪，解析 log book 数据，接入 DeepSeek API 实现：
- **自动预警解读**：检测到 Danger/Warning 状态时自动调用 AI 给出中文安全建议
- **手动对话**：用户在终端直接打字和 AI 对话，AI 能结合 log book 历史回答问题

## Architecture

单线程 select/poll 模型：

```
select([serial_fd, stdin])
     ├─ serial 有数据 → parse_serial() → 更新 log_buffer / current_reading
     │                                   → 检测状态变化，自动触发 AI
     └─ stdin 有输入  → 以 / 开头 → 本地命令
                        否则       → 发给 AI（带 log_buffer 上下文）
```

## Components

### 1. Serial Parser
- 逐行读取串口
- `LOG,` 前缀 → 追加 log_buffer（最多 32 条 FIFO）
- `DATA,` 前缀 → 更新 current_reading
- `STATE=` 前缀 → 更新 current_state
- 所有行原样打印到终端（带时间戳前缀）

### 2. Log Buffer
- 与固件 event_log 一一对应的列表
- 每条：`{ts_s, state, adc, level}`
- 满 32 条后自动滚动

### 3. AI Client (DeepSeek)
- 使用 `openai` 库，base_url = `https://api.deepseek.com`
- 模型：`deepseek-chat`
- API key 来源：`DEEPSEEK_API_KEY` 环境变量
- 流式输出回复

### 4. Auto Alert
- 仅在 Danger/Warning 状态**首次进入**时触发一次
- 用 `last_alert_state` 防重复
- 红色/黄色前缀标签区分危险/警告

### 5. Manual Chat
- 用户输入不以 `/` 开头 → 发给 AI
- 附带当前 log_buffer + 当前读数作为上下文
- 流式打印回复，前缀 `🤖 `

### 6. Local Commands（`/` 前缀）
- `/log` — 打印当前 log buffer
- `/status` — 打印当前传感器状态
- `/clear` — 清空 log buffer
- `/quit` — 退出

## System Prompt

```
你是智能酒精检测仪的AI助手，能访问设备的实时数据和事件日志。
用中文回答，简洁直接。可以解释酒精浓度和风险，给出安全建议。
如果被问到能否开车，始终强调"不确定就不要开"。
```

## File Structure

```
bridge/
├── ai_bridge.py      # 主脚本
└── requirements.txt  # pyserial, openai
```

不修改任何固件代码。

## Error Handling

- 串口断连：打印错误并等待重连，不断开
- API 调用失败：打印错误信息，不崩溃
- API key 未设置：启动时报错退出

# AISafeOS64 Agent 配置指南

## 创建完成

已成功在项目目录 `/home/kerfs/AISafeOS64/AISafeOS64` 创建 agent 工作空间。

### 创建的文件

```
AISafeOS64/
├── .openclaw/
│   └── workspace-state.json       # 工作空间状态
├── memory/
│   └── 2026-04-03.md              # 今日日志
├── AGENTS.md                      # Agent 职责和工作流程
├── SOUL.md                        # Agent 人格
├── IDENTITY.md                    # Agent 身份
├── USER.md                        # 开发者信息
├── MEMORY.md                      # 长期记忆
├── TOOLS.md                       # 工具配置
└── HEARTBEAT.md                   # 定期任务配置
```

## 下一步：配置 OpenClaw

### 选项 1：作为独立的 Agent（推荐）

编辑 `~/.openclaw/openclaw.json`，在 `agents` 部分添加：

```json
{
  "agents": {
    "defaults": {
      "model": {
        "primary": "zai/glm-5"
      },
      ...
    },
    "aisafeos": {
      "workspace": "/home/kerfs/AISafeOS64/AISafeOS64",
      "label": "aisafeos-kernel",
      "model": {
        "primary": "zai/glm-5"
      }
    }
  }
}
```

### 选项 2：作为 Channel 绑定（如需飞书控制）

如果需要通过飞书控制此 agent：

```json
{
  "agents": {
    "aisafeos": {
      "workspace": "/home/kerfs/AISafeOS64/AISafeOS64",
      "label": "aisafeos-kernel"
    }
  },
  "channels": {
    "feishu-aisafeos": {
      "enabled": true,
      "appId": "YOUR_APP_ID",
      "appSecret": "YOUR_APP_SECRET",
      "domain": "feishu",
      "groupPolicy": "open",
      "connectionMode": "websocket",
      "agent": "aisafeos"
    }
  }
}
```

### 选项 3：使用命令行启动

直接使用 `sessions_spawn` 启动：

```bash
# 通过 OpenClaw 命令行
openclaw session start --workspace /home/kerfs/AISafeOS64/AISafeOS64
```

## Agent 特点

### 工作方式
- **位置**: 在代码目录中工作，直接访问项目文件
- **工具**: 使用 ACP Harness 调用 Claude Code / Codex / Pi
- **专注**: 微内核操作系统开发

### 技术专长
- 微内核架构设计
- 实时调度算法
- IPC 消息传递
- 能力模型和访问控制
- MISRA C:2012 合规
- 安全认证（ISO 26262, IEC 61508）

## 使用示例

### 通过 OpenClaw 调用

```bash
# 发送消息给 agent
sessions_send --agent aisafeos "帮我审查 scheduler.c 的代码"

# 或在配置后直接对话
# Agent 会自动加载 AGENTS.md, SOUL.md, USER.md 等文件
```

### 通过飞书调用（如已配置）

在飞书中直接发送消息即可触发 agent。

## 重启 OpenClaw

配置完成后，重启 OpenClaw 以使配置生效：

```bash
openclaw gateway restart
```

---

**创建时间**: 2026-04-03 22:36
**工作空间**: /home/kerfs/AISafeOS64/AISafeOS64

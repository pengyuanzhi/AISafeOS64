# 🧠 项目核心大脑：CLAUDE.md

**版本**: 4.4 (单文档规范)
**最后更新**: 2026-03-16
**状态**: 🔴 激活 - 强制执行

---

## 1. 📂 文档规范

### 1.1 核心理念

- **根目录 (`/`)**：生产就绪状态，必须保持绝对纯净
- **文档区 (`docs/`)**：开发过程，所有非代码产物必须严格分类
- **脚本区 (`scripts/`)**：工具集，所有开发和维护脚本
- **归档区 (`docs/archive/`)**：时间胶囊，存储中间态、草稿及历史记录

### 1.2 绝对禁令

- ❌ 严禁在根目录创建任何临时文件
- ❌ 严禁在 `docs/` 根目录下直接存放文件
- ❌ 严禁创造未定义的子目录
- ❌ 严禁用开发日志污染 `README.md`
- ❌ 严禁在根目录放置开发和维护脚本

### 1.3 根目录规范

**仅允许存在：**
- 必需文件：`README.md`, `CLAUDE.md`
- 源代码目录：`src/`, `frontend/`, `tests/`
- 脚本目录：`scripts/`
- 核心配置：`.gitignore`, `.env.example`, `package.json`, `requirements.txt`, `docker-compose.yml`, `Dockerfile`等配置文件

### 1.4 脚本目录规范 (`scripts/`)

**命名规范**：
- 使用 kebab-case (小写字母 + 连字符)
- 脚本文件：`name.sh` 或 `name.py`
- 描述性名称，避免缩写

### 1.5 文档五维结构 (`docs/`)

**所有文档必须严格归类到以下五个子目录之一：**

| 子目录 | 用途 | 文档数量限制 |
| :--- | :--- | :--- |
| **`docs/requirements/`** | 需求规格（功能列表、用户故事、PRD、验收标准） | **仅1个**：`REQUIREMENTS.md` |
| **`docs/design/`** | 系统设计（架构图、API定义、技术选型、部署指南） | **仅1个**：`ARCHITECTURE.md` |
| **`docs/plans/`** | 项目计划（路线图、Sprint计划、任务拆解） | **仅1个**：`IMPLEMENTATION_PLAN.md` |
| **`docs/logs/`** | 过程日志（会议纪要、决策日志、阶段总结） | **按日期命名，保留多个** |
| **`docs/archive/`** | 历史归档（中间草稿、被废弃的方案） | **按日期命名，保留多个** |

**单文档原则**：
- `requirements/`, `design/`, `plans/` 每个目录**仅保留一个核心文档**
- 所有相关内容必须合并到该目录的唯一文档中
- 如需新增内容，更新现有文档，不创建新文件

### 1.6 归档命名规范 (`docs/archive/`)

**格式**: `YYYY-MM-DD-[序号]-[简短描述].md`

- `YYYY-MM-DD`: ISO 标准日期
- `[序号]`: 当日流水号，3位数字 (001, 002, ...)
- `[简短描述]`: 小写字母 + 连字符 (kebab-case)

### 1.7 文件分类规则

```
代码文件 → src/, frontend/, tests/
脚本文件 → scripts/
需求文档 → docs/requirements/REQUIREMENTS.md
设计文档 → docs/design/ARCHITECTURE.md
计划文档 → docs/plans/IMPLEMENTATION_PLAN.md
过程日志 → docs/logs/YYYY-MM-DD-[描述].md
历史归档 → docs/archive/YYYY-MM-DD-[序号]-[描述].md
```

---

## 2. 🔄 Git 协同规范

### 2.1 Commit Message 格式

```
<type>(<scope>): <subject>

<body>
```

**type 类型**：`feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

**scope 范围**：`structure`, `docs`, `scripts`, 或具体模块名

### 2.2 引用规范

代码注释或文档引用时，使用完整相对路径。

---

## 3. ✅ 合规检查清单

提交代码前确认：
- [ ] 根目录只有必需文件（README.md, CLAUDE.md, 配置文件）
- [ ] 所有脚本在 `scripts/` 目录
- [ ] requirements/, design/, plans/ 每个目录只有一个文档
- [ ] 归档文件按 `YYYY-MM-DD-[序号]-[描述].md` 命名
- [ ] 没有未定义的子目录
- [ ] 没有临时文件在根目录

---

**指令确认**：

1. **根目录**：保持绝对纯净
2. **scripts/**：所有脚本统一管理
3. **docs/requirements/REQUIREMENTS.md**：唯一需求文档
4. **docs/design/ARCHITECTURE.md**：唯一设计文档
5. **docs/plans/IMPLEMENTATION_PLAN.md**：唯一计划文档
6. **docs/logs/**：过程日志（按日期，保留多个）
7. **docs/archive/**：历史归档（按日期，保留多个）

根目录将始终保持整洁，文档结构清晰可追溯！

---

**维护者**: Claude AI
**版本**: 4.4

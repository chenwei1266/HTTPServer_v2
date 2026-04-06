# ChatUI v2 详细设计说明（README v1.0）

> 目录：`chat_ui_v2/`  
> 目标：基于现有代码实现，给出可落地、可维护、可演进的系统设计文档。

---

## 1. 项目定位与设计目标

ChatUI v2 是一个 **“单页前端 + C++ HTTP/SSE 后端 + MySQL 持久层 + 多模型策略层”** 的聊天系统，核心目标：

- 提供可直接运行的 AI 对话产品原型（支持注册登录、会话管理、消息流式输出）。
- 通过策略工厂解耦不同厂商模型（Claude / Qwen / 豆包 / 文心 / 本地 OpenAI 兼容）。
- 在不引入重框架的前提下，使用轻量组件快速组装业务能力。
- 对 v1 数据结构做平滑升级，尽量保持兼容。

### 1.1 非目标（当前版本）

- 不提供完整 RBAC、多租户、审计日志。
- 不实现复杂的前端工程化构建（当前是单 HTML 内联 CSS/JS）。
- WebSocket 聊天链路保留占位，但当前版本以 SSE 为主。

---

## 2. 功能设计

## 2.1 用户与认证

### 已实现

- 邮箱注册：`POST /api/auth/register`
  - 参数：`username/email/password`
  - 校验：用户名长度、邮箱格式、密码长度（>=8）
  - 成功后创建会话并写入 session。
- 邮箱登录：`POST /api/auth/login`
  - 校验邮箱与密码，成功后绑定 session。
- 退出登录：`POST /api/auth/logout`
  - 销毁会话。
- 当前用户：`GET /api/users/me`（兼容 `/api/auth/me`）
  - 从 session 中读取用户 ID，再查库返回用户信息。

### 设计说明

- 当前认证为 **Session Cookie 模式**（非 JWT 主导），前端虽保留 token 字段，但主要依赖 `credentials: same-origin` + 服务端 session。
- 密码采用 `SHA256(salt + password)`，每用户独立 salt。

## 2.2 会话（Conversation）管理

- `GET /api/conversations`：列出当前用户会话（按 `updated_at DESC`）。
- `POST /api/conversations`：创建新会话（可带 `model/system_prompt`）。
- `PATCH|PUT /api/conversations/:id`：更新标题。
- `DELETE /api/conversations/:id`：删除会话。

### 设计说明

- 会话与用户强绑定（查询均带 `user_id`）。
- title 支持自动命名：在首条用户消息出现时，截断前 28 字作为标题。

## 2.3 消息管理

- `GET /api/conversations/:id/messages`：拉取该会话消息。
- 入库策略：
  - 用户消息：SSE 请求启动后落库（`role=user`）。
  - AI 消息：流式完成后按拼接完整文本落库（`role=assistant`）。

### 设计说明

- 消息接口用于“历史回放”；实时增量由 SSE 通道提供。
- 前端展示时将消息倒序结果 reverse，恢复时间正序渲染。

## 2.4 AI 对话（SSE）

- `POST /api/chat/sse`
- 入参核心字段：
  - `conversation_id`
  - `model`
  - `system_prompt`
  - `messages: [{role, content}]`
- 服务端通过策略层选择厂商并流式返回：
  - `token` 事件：增量片段
  - `meta` 事件：模型/会话元信息
  - `error` 事件：错误
  - `done` 事件：结束

### 设计说明

- 当前主路径是 SSE，WebSocket 端点已注册但仅返回“未启用”提示。
- 模型路由采用 “推断 + 配置” 双轨：
  1. 先从请求模型名推断 provider（如 `qwen/claude/doubao`）。
  2. 再从 `config.json` 读取 provider 配置并创建具体策略。

## 2.5 前端交互能力

- 登录/注册双页签 + 表单校验 + 弱/中/强密码提示。
- 侧边栏会话列表、模型选择弹窗、系统提示词输入。
- Markdown 渲染 + 代码高亮 + 代码一键复制。
- 流式打字效果、停止生成、重新生成。
- 深/浅色主题切换、API Base 地址本地存储。

---

## 3. 架构设计

## 3.1 总体架构（逻辑分层）

1. **表现层（chatui.html）**
   - 单文件应用，负责 UI 状态、HTTP/SSE 调用、消息渲染。
2. **接入层（chat_main.cpp 路由）**
   - 注册 REST + SSE + WS 路由，组装 session、DAO、AI 策略。
3. **业务层（api/auth handlers）**
   - 认证、会话 CRUD、消息读取。
4. **模型层（ai/）**
   - `AIStrategy` 抽象 + 多 provider 实现 + 工厂注册。
5. **数据层（dao/ + MySQL）**
   - `UserDao/ConversationDao/MessageDao` 封装 SQL。
6. **基础设施层（HttpServer_v2）**
   - HTTP、SSE、WS、Session、DB 连接池能力。

## 3.2 关键设计原则

- **接口隔离**：模型厂商差异通过 Strategy 隔离，业务代码不直接依赖 curl 细节。
- **最小依赖**：无重量级 ORM / JSON 库，使用轻量 `JsonLite`。
- **增量兼容**：启动时自动建表并补字段，降低迁移成本。
- **单向数据流（前端）**：状态集中在 `state`，渲染函数从状态派生 UI。

## 3.3 请求生命周期（SSE 聊天）

1. 前端点击发送，若无会话则先创建会话。
2. 前端请求 `/api/chat/sse`，提交历史消息与模型参数。
3. 服务端校验 session → 校验会话归属 → 补齐/创建会话。
4. 写入用户消息到 `messages`。
5. AI 工厂创建策略实例并发起流式请求。
6. token 增量经 SSE 推送给前端。
7. 结束后拼接 AI 完整回复落库并 `touch` 会话更新时间。

---

## 4. 模块设计

## 4.1 入口与路由编排（`chat_main.cpp`）

职责：

- 初始化：加载页面、读取环境变量、初始化 DB 连接池、自动修复 schema。
- 模型：加载 `config.json`；如缺失则回退到 Claude 默认配置。
- 路由：
  - 页面与健康检查
  - 认证路由
  - 会话/消息路由
  - SSE/WS 路由

亮点：

- `ensureSchemaV2` 支持幂等创建与列补齐。
- `inferProvider` 基于模型名做 provider 推断，提升前端模型名自由度。

## 4.2 认证模块（`auth/`）

- `AuthHandlers.h`：注册/登录/登出处理器。
- `AuthMiddleware.h`：鉴权中间件，统一 session 解析。
- `PasswordUtil.h`：salt 生成、SHA256 哈希与验证。

设计取舍：

- 优先简洁与可读性，满足演示与中小规模生产原型。
- 后续可替换为 Argon2/bcrypt 并加入登录风控。

## 4.3 会话与消息 API 模块（`api/`）

- `ConversationListHandler`：会话列表与新建。
- `ConversationDetailHandler`：标题更新、删除。
- `MessageHandler`：按会话读取消息。

设计特点：

- 处理器粒度清晰，遵循“单 handler 单资源/子资源”。
- 所有业务入口先做 `AuthMiddleware::check`。

## 4.4 数据访问模块（`dao/`）

- `UserDao`：按邮箱/ID 查询、注册、登录校验。
- `ConversationDao`：创建、查询、列表、更新标题、删除、touch。
- `MessageDao`：插入、按会话列表。

设计特点：

- 以静态方法 + 连接池调用实现低接入成本。
- SQL 使用参数绑定，减少注入风险。

## 4.5 AI 策略模块（`ai/`）

- `AIStrategy`：统一接口（同步 + 流式）。
- `OpenAICompatibleStrategy`：抽象通用 curl + SSE 解析模板。
- `AIStrategies`：厂商实现（Claude/Qwen/Doubao/Wenxin/Local）。
- `AIFactory` + `ModelRegister`：注册与创建解耦。
- `AIConfig`：读取 `config.json` 的模型配置。

设计特点：

- “可扩展点”明确：新增厂商通常只需新增 Strategy + 注册宏。
- 文心策略单独处理 token 放 URL、响应字段差异等非兼容细节。

## 4.6 前端模块（`chatui.html`）

按功能分区：

- Auth：登录/注册、错误反馈。
- Chat：消息渲染、发送、流式处理、重试。
- Model：模型列表弹窗与筛选。
- UI：主题、侧边栏、设置、Toast。
- Network：`apiRequest` + `streamFromAPI`。

设计特点：

- 无构建工具即可运行，适合快速部署与调试。
- `normalizeApiBase` 对 API 地址做防御性标准化，避免相对路径误用。

---

## 5. 数据与配置设计

## 5.1 数据库模型

- `users`
  - 核心字段：`username/email/password_hash/salt`
  - 唯一约束：`email`、`username`
- `conversations`
  - `user_id` 外键
  - `title/model/system_prompt`
  - `updated_at` 用于排序与活跃度
- `messages`
  - `conversation_id` 外键
  - `role/content`

## 5.2 迁移策略

- 冷启动：`ensureSchemaV2` 自动建表/补字段。
- 脚本：
  - `sql/init_v2.sql`：全量初始化
  - `sql/migrate_from_v1.sql`：从 v1 结构补齐到 v2

## 5.3 配置设计（`config.json`）

```json
{
  "default_model": "local",
  "models": {
    "local": {
      "api_key": "",
      "base_url": "http://localhost:8002",
      "model": "Qwen3-Coder-Next",
      "max_tokens": 2048,
      "timeout": 120
    }
  }
}
```

说明：

- `default_model` 是 provider key，而非展示名。
- 每个 provider 可独立配置 API 地址、key、模型名与超时。

---

## 6. 运行与部署说明

## 6.1 构建依赖

- C++17
- muduo（`muduo_net/muduo_base`）
- OpenSSL
- mysqlcppconn
- libcurl

## 6.2 构建步骤（示例）

```bash
cd chat_ui_v2
mkdir -p build && cd build
cmake ..
make -j
```

生成可执行文件：`chatui_v2_server`

## 6.3 启动参数与环境变量

- 启动：`./chatui_v2_server [port]`（默认 8088）
- 环境变量：
  - `DB_HOST`（默认 `127.0.0.1`）
  - `DB_USER`（默认 `root`）
  - `DB_PASS`（默认 `123456`）
  - `DB_NAME`（默认 `chat_app`）
  - `DB_POOL_SIZE`（默认 `10`）

## 6.4 首次启动建议

1. 准备 MySQL 并创建具备建表权限的账号。
2. 按需编辑 `config.json` 填写模型 provider 配置。
3. 启动服务后访问 `http://<host>:<port>/`。
4. 打开设置确认 API Base 指向正确后端地址。

---

## 7. 接口清单（v1.0）

- 页面
  - `GET /`
- 健康
  - `GET /api/health`
- 认证
  - `POST /api/auth/register`
  - `POST /api/auth/login`
  - `POST /api/auth/logout`
  - `GET /api/users/me`
  - `GET /api/auth/me`（兼容）
- 会话
  - `GET /api/conversations`
  - `POST /api/conversations`
  - `PATCH|PUT /api/conversations/:id`
  - `DELETE /api/conversations/:id`
- 消息
  - `GET /api/conversations/:id/messages`
- 对话
  - `POST /api/chat/sse`
  - `WS /api/chat/ws`（占位）

---

## 8. 已知限制与风险

- JSON 解析为手写轻量实现，复杂嵌套/边缘格式容错有限。
- 密码哈希算法强度一般（建议升级）。
- SSE 结束后才持久化整段 AI 回复，若中断可能出现“前端已见、后端未存”。
- 目前缺少统一的可观测性（trace-id、指标、审计日志）。
- 前端单文件体量较大，长期维护成本会上升。

---

## 9. 后续演进路线图

## 9.1 短期（v1.1）

- 安全增强
  - 密码哈希升级 Argon2/bcrypt。
  - 登录限流、失败次数惩罚、基础风控。
- 稳定性
  - SSE 增量落库（可选每 N token checkpoint）。
  - 接口错误码标准化（code/message/details）。
- 可运维性
  - 增加结构化日志，统一 request-id。

## 9.2 中期（v1.2 ~ v1.5）

- 前端工程化
  - 拆分成模块化 JS/TS（Vite + 组件化）。
  - 样式体系与 design token 固化。
- 对话能力
  - 工具调用（function/tool calling）。
  - 多模态附件（文件上传 + OCR/解析）。
- 数据能力
  - 消息检索、会话归档、软删除与回收站。

## 9.3 长期（v2.0）

- 多租户与权限体系（组织/空间/成员角色）。
- 模型网关化：统一配额、成本统计、路由策略。
- 推理链路升级：SSE + WS 双栈稳定支持。
- 可观测平台：指标、日志、追踪闭环。

---

## 10. 扩展指南

## 10.1 新增一个模型厂商（推荐流程）

1. 在 `ai/` 中新增 Strategy（优先继承 `OpenAICompatibleStrategy`）。
2. 在 `ModelRegister.h` 增加 `REGISTER_AI_MODEL`。
3. 在 `config.json` 添加 provider 配置。
4. 前端 `MODELS` 列表增加展示项（可选）。

## 10.2 新增一个业务接口

1. 在 `api/` 新建 Handler。
2. 在 `chat_main.cpp` 注册路由。
3. 如需持久化，在 `dao/` 增加对应查询/写入方法。
4. 前端通过 `apiRequest` 调用并接入状态管理。

---

## 11. 版本信息

- 文档版本：`README v1.0`
- 适配代码目录：`chat_ui_v2/`
- 生成方式：基于现有实现逐文件阅读整理。



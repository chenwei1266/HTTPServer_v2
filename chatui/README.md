# ChatUI 重构版

## 目录结构

```
chatui/
├── application/          # HTTP 服务层
│   ├── ChatUI.h
│   └── ChatUI.cpp
│
├── llm/                  # LLM 推理模块
│   ├── core/
│   │   ├── LLMManager.h
│   │   └── LLMManager.cpp
│   ├── strategy/         # 各厂商策略实现
│   ├── config/           # 配置加载
│   ├── transport/        # 网络传输层
│   └── models.json       # 模型配置文件
│
├── user/                 # 用户与对话管理
│   ├── model/
│   │   ├── User.h
│   │   ├── Conversation.h
│   │   └── Context.h
│   ├── service/
│   │   ├── UserManager.h
│   │   └── UserManager.cpp
│
├── storage/              # 数据持久化层
│   ├── entity/           # 数据实体
│   ├── repository/       # 数据访问对象
│   │   ├── UserRepository.h
│   │   ├── ConversationRepository.h
│   │   └── MessageRepository.h
│   ├── StorageManager.h
│   └── StorageManager.cpp
│
└── frontend/             # 前端资源（待补充）
```

## 设计原则

1. **分层清晰**：应用层、业务层、数据层职责明确
2. **异步非阻�塞**：LLM 推理不阻塞 HTTP 线程
3. **配置驱动**：通过 models.json 动态加载模型
4. **可扩展性**：策略模式支持新增模型厂商

## 核心流程

```
HTTP Request → ChatUI::onHttpRequest()
              ↓ (鉴权 + 路由)
              → UserManager::buildRequestMessages()
              ↓ (构建上下文)
              → LLMManager::get() [异步]
              ↓ (流式推理)
              → SSE 推送 → 前端
              ↓ (完成)
              → UserManager::appendMessage() [落库]
```

## 开发计划

- [x] 创建目录结构
- [ ] 实现 LLMManager 核心框架
- [ ] 实现 models.json 配置加载
- [ ] 实现 UserManager 门面
- [ ] 实现 StorageManager 数据层
- [ ] 对接 ChatUI 应用层
- [ ] 前端集成

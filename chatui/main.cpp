#include "application/ChatUI.h"
#include <iostream>
#include <cstdlib>
#include <signal.h>

using namespace chatui::application;

static ChatUI* g_app = nullptr;

void signalHandler(int signal) {
    std::cout << "\n[Main] Caught signal " << signal << ", shutting down...\n";
    if (g_app) {
        g_app->shutdown();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "===========================================\n";
    std::cout << "  ChatUI v2 - Refactored Edition\n";
    std::cout << "===========================================\n\n";

    // 配置
    ChatUI::Config config;

    // 从环境变量读取数据库配置
    const char* dbHost = std::getenv("DB_HOST");
    const char* dbUser = std::getenv("DB_USER");
    const char* dbPass = std::getenv("DB_PASS");
    const char* dbName = std::getenv("DB_NAME");
    const char* dbPoolSize = std::getenv("DB_POOL_SIZE");

    if (dbHost) config.dbConfig.host = dbHost;
    if (dbUser) config.dbConfig.user = dbUser;
    if (dbPass) config.dbConfig.password = dbPass;
    if (dbName) config.dbConfig.database = dbName;
    if (dbPoolSize) config.dbConfig.poolSize = std::atoi(dbPoolSize);

    // 从命令行参数读取端口
    if (argc > 1) {
        config.port = std::atoi(argv[1]);
    }

    // 模型配置路径
    config.modelsConfigPath = "llm/models.json";

    std::cout << "Configuration:\n";
    std::cout << "  Port: " << config.port << "\n";
    std::cout << "  Database: " << config.dbConfig.database
              << "@" << config.dbConfig.host << "\n";
    std::cout << "  Models Config: " << config.modelsConfigPath << "\n\n";

    // 创建并初始化应用
    ChatUI app;
    g_app = &app;

    if (!app.init(config)) {
        std::cerr << "[Main] Failed to initialize application\n";
        return 1;
    }

    std::cout << "\n[Main] Application initialized successfully\n";
    std::cout << "[Main] Server listening on http://localhost:" << config.port << "\n";
    std::cout << "[Main] Press Ctrl+C to stop\n\n";

    // 运行服务
    app.run();

    return 0;
}

#!/bin/bash

# 配置数据库环境变量（根据你的实际情况修改）
export DB_HOST=127.0.0.1
export DB_USER=root
export DB_PASS=123456
export DB_NAME=chat_app
export DB_POOL_SIZE=10

# 启动服务器
cd /home/user/HTTPServer_v2/chatui
./build/chatui_v2_server 8088

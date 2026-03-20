# include "crow.h"
# include "crow/middlewares/cors.h"
# include "../include/db_manager.h"
# include <stdio.h>
# include <string>
# include <iostream>
#include <unordered_set>
#include <mutex>

// request body json数据
struct AuthPayload {
    bool is_valid;
    std::string username;
    std::string password;
};

// 解析请求体
AuthPayload parse_auth_request(const crow::request& req) {
    auto x = crow::json::load(req.body);
    // 检查字段
    if (!x || !x.has("username") || !x.has("password")) {
        return {false, "", ""};
    }
    return {true, x["username"].s(), x["password"].s()};
}

// 设置认证相关的路由
void setup_auth_routes(crow::App<crow::CORSHandler>& app, DBManager& db_manager) {
    CROW_ROUTE(app, "/api/login").methods(crow::HTTPMethod::POST)([&db_manager](
        const crow::request& req){

        // 防止用户端可能的恶意攻击或者断网等原因导致服务器崩溃
        auto payload = parse_auth_request(req);
        if (!payload.is_valid) {
            return crow::response(400, "{\"success\": false, \"message\": \"Invalid request body\"}");
        }

        std::string username = payload.username;
        std::string password = payload.password;

        // 验证用户
        if (!db_manager.verify_user(username, password)) {
            return crow::response(401, "{\"success\": false, \"message\": \"Invalid username or password\"}");
        }

        db_manager.print_tables(); // 打印数据库表内容，方便调试
        return crow::response(200, "{\"success\": true, \"message\": \"Login successful\"}");
    });
    
    CROW_ROUTE(app, "/api/register").methods(crow::HTTPMethod::POST)([&db_manager](
        const crow::request& req){

        auto payload = parse_auth_request(req);
        if (!payload.is_valid) {
            return crow::response(400, "{\"success\": false, \"message\": \"Invalid request body\"}");
        }

        std::string username = payload.username;
        std::string password = payload.password;

        // 添加用户
        if (!db_manager.add_user(username, password)) {
            return crow::response(400, "{\"success\": false, \"message\": \"Failed to register user\"}");
        }

        db_manager.print_tables();
        return crow::response(200, "{\"success\": true, \"message\": \"Registration successful\"}");
    });
}

// 设置WebSocket相关的路由
void setup_websocket_routes(crow::App<crow::CORSHandler>& app) {
    static std::unordered_set<crow::websocket::connection*> active_users;
    static std::mutex users_mutex;

    CROW_WEBSOCKET_ROUTE(app, "/ws/edit")
        .onopen([&](crow::websocket::connection& conn) {
            users_mutex.lock();
            active_users.insert(&conn);
            users_mutex.unlock();
            std::cout << "WebSocket connection opened. "<< std::endl;
        })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason) {
            users_mutex.lock();
            active_users.erase(&conn);
            users_mutex.unlock();
            std::cout << "WebSocket connection closed. " << std::endl;
        })
        .onmessage([&](crow::websocket::connection& conn, const std::string& message, bool is_binary) {
            std::cout << "Received message: " << message << std::endl;
            // Echo the message back to the client
            for (auto* user : active_users) {
                user->send_text(message);
            }
        });
}

int main(){
    // 初始化数据库管理器
    DBManager db_manager("../../database/users.db");
    if (!db_manager.init_tables()) {  
        std::cerr << "Failed to initialize database tables." << std::endl;
        return 1; 
    }

    // 初始化Crow应用和CORS中间件
    crow::App<crow::CORSHandler> app;
    auto& cors = app.get_middleware<crow::CORSHandler>();

    // 全局规则设定
    cors.global()
        .headers("X-Custom-Header", "Content-Type", "Accept") // 允许的自定义请求头
        .methods("POST"_method, "GET"_method, "OPTIONS"_method)
        .origin("*")
        .max_age(3600);
    
    setup_auth_routes(app, db_manager);
    setup_websocket_routes(app);
    app.port(1145).multithreaded().run();

    // 服务器关闭时会自动调用DBManager的析构函数，关闭数据库连接
    return 0;
}
# include "crow.h"
# include "crow/middlewares/cors.h"
# include "../include/db_manager.h"
# include <stdio.h>
# include <string>
# include <iostream>

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
    app.port(1145).multithreaded().run();

    // 服务器关闭时会自动调用DBManager的析构函数，关闭数据库连接
    return 0;
}
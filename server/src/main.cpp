# include "crow.h"
# include "crow/middlewares/cors.h"
# include "../include/db_manager.h"
# include <stdio.h>
# include <string>
# include <iostream>
# include <unordered_set>
# include <mutex>
# include <ctime>
# include <fstream>
# include <csignal>

# define PORT 1145 // 服务器监听端口
# define CANVAS_SIZE 16 // 总像素数
# define DB_PATH "../../database/users.db" // 数据库文件路径
# define CANVAS_STATE_PATH "../../database/canvas_state" // 画布状态文件路径

static std::unordered_map<crow::websocket::connection*, std::string> active_users;
static std::mutex users_mutex;
static std::mutex canvas_mtx;
static std::vector<std::string> memory_canvas(CANVAS_SIZE, "#FFFFFF"); // CANVAS_SIZE个像素的画布，初始颜色为白色

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

// 加载画布
void load_canvas_from_file() {
    std::cout << "Loading canvas state from file..." << std::endl;
    std::ifstream file(CANVAS_STATE_PATH, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open canvas state file for reading." << std::endl;
        return;
    }
    std::string line;
    int index = 0;
    while (std::getline(file, line) && index < CANVAS_SIZE) {
        memory_canvas[index++] = line;
    }
    std::cout << "Canvas state loaded successfully." << std::endl;
    file.close();
}

// 把画布状态保存到文件
void save_canvas_to_file() {
    std::cout << "Saving canvas state to file..." << std::endl;
    std::ofstream file(CANVAS_STATE_PATH, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open canvas state file for writing." << std::endl;
        return;
    }
    for (const auto& color : memory_canvas) {
        file << color << std::endl;
    }
    std::cout << "Canvas state saved successfully." << std::endl;
    file.close();
}

// 广播消息给所有连接的用户
void broadcast_message(const std::string& message, const std::unordered_map<crow::websocket::connection*, std::string>& connections) {
    users_mutex.lock();
    for (const auto& pair : connections) {
        pair.first->send_text(message);
    }
    users_mutex.unlock();
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
        return crow::response(200, "{\"success\": true, \"message\": \"Login successful\", \"username\": \"" + username + "\"}");
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
        return crow::response(200, "{\"success\": true, \"message\": \"Registration successful\", \"username\": \"" + username + "\"}");
    });
}

// 设置WebSocket相关的路由
void setup_websocket_routes(crow::App<crow::CORSHandler>& app, DBManager& db_manager) {
    CROW_WEBSOCKET_ROUTE(app, "/ws/edit")
        .onopen([&](crow::websocket::connection& conn) {
            std::cout << "WebSocket connection opened. "<< std::endl;
        })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason) {
            time_t now = time(0);
            std::string dt = ctime(&now);
            users_mutex.lock();
            std::string username = active_users[&conn];
            active_users.erase(&conn);
            users_mutex.unlock();
            crow::json::wvalue broadcast_data;
            broadcast_data["type"] = "user_left";
            broadcast_data["username"] = username;
            broadcast_data["time"] = dt;
            broadcast_message(broadcast_data.dump(), active_users);
            std::cout << "WebSocket connection closed. " << std::endl;
        })
        .onmessage([&](crow::websocket::connection& conn, const std::string& message, bool is_binary) {
            time_t now = time(0);
            std::string dt = ctime(&now);
            if (is_binary) return;
            auto x = crow::json::load(message);
            if (!x) {
                std::cerr << "Failed to parse WebSocket message." << std::endl;
                return;
            }

            std::string type = x["type"].s();
            std::string username;
            crow::json::wvalue broadcast_data;
            crow::json::wvalue broadcast_canvas;
            int index;
            std::string color;

            switch (type[0]) {
                case 'j':
                    // 广播用户加入消息
                    if (x["username"].t() != crow::json::type::String || !db_manager.find_user(x["username"].s())) {
                        std::cerr << "Invalid username." << std::endl;
                        conn.send_text("{\"type\": \"invalid_username\", \"message\": \"Invalid username\"}");
                        return;
                    }
                    username = x["username"].s();
                    std::cout << "User " << username << " joined the document." << std::endl;
                    users_mutex.lock();
                    active_users.insert({&conn, username});
                    users_mutex.unlock();
                    broadcast_data["type"] = "user_joined";
                    broadcast_data["username"] = username;
                    broadcast_data["time"] = dt;
                    broadcast_message(broadcast_data.dump(), active_users);

                    // 发送当前画布状态给新加入的用户
                    broadcast_canvas["type"] = "canvas";

                    canvas_mtx.lock();
                    broadcast_canvas["canvas"] = memory_canvas;
                    canvas_mtx.unlock();

                    std::cout << "Sending current canvas state "<< broadcast_canvas.dump() <<" to " << username << std::endl;
                    conn.send_text(broadcast_canvas.dump());
                    break;
                case 'p':
                    index = x["index"].i();
                    color = x["color"].s();
                    if (0<=index && index<CANVAS_SIZE) {
                        canvas_mtx.lock();
                        memory_canvas[index] = color;
                        canvas_mtx.unlock();
                    } else {
                        std::cerr << "Invalid pixel index: " << index << std::endl;
                        return;
                    }
                    broadcast_data["type"] = "pixel_update";
                    broadcast_data["username"] = active_users[&conn];
                    broadcast_data["index"] = index;
                    broadcast_data["color"] = color;
                    broadcast_data["time"] = dt;
                    std::cout << "Received pixel update from " << active_users.at(&conn) << ": " << message << std::endl;
                    broadcast_message(broadcast_data.dump(), active_users);
                    break;
                default:
                    std::cerr << "Unknown message type: " << type << std::endl;
                    return;
            }
        });
}

int main(){
    // 初始化数据库管理器
    DBManager db_manager(DB_PATH);
    if (!db_manager.init_tables()) {  
        std::cerr << "Failed to initialize database tables." << std::endl;
        return 1; 
    }

    // 初始化画布
    load_canvas_from_file();

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
    setup_websocket_routes(app, db_manager);
    app.port(PORT).multithreaded().run();

    // Ctrl+C 后会执行app.run()结束后的代码，保存画布状态
    save_canvas_to_file();

    // 服务器关闭时会自动调用DBManager的析构函数，关闭数据库连接
    return 0;
}
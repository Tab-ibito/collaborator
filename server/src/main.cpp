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
# include <deque>
# include <filesystem>
# include <vector>
# include <unordered_map>

constexpr uint16_t SERVER_PORT = 1145;
constexpr int CANVAS_SIZE = 256;
const std::string DB_PATH = "../../database/users.db";
const std::string CANVAS_PATH = "../../database/canvas/";
constexpr int MAX_EDIT_HISTORY = 10;

// request body json数据
struct AuthPayload {
    bool is_valid;
    std::string username;
    std::string password;
};

// 操作记录格式
struct EditRecord {
    int index;
    std::string color;
};

static std::unordered_map<crow::websocket::connection*, std::string> active_users;
static std::string current_file = "canvas_state"; // 当前使用的画布文件
static std::deque<EditRecord> edit_history; // 编辑历史记录，保存最近10条
static std::mutex users_mutex;
static std::mutex canvas_mtx;
static std::mutex file_mtx;
static std::vector<std::string> memory_canvas(CANVAS_SIZE, "#FFFFFF"); // CANVAS_SIZE个像素的画布，初始颜色为白色

// 解析请求体
AuthPayload parse_auth_request(const crow::request& req) {
    auto x = crow::json::load(req.body);
    // 检查字段
    if (!x || !x.has("username") || !x.has("password")) {
        return {false, "", ""};
    }
    return {true, x["username"].s(), x["password"].s()};
}

// 加载画布（没有加file锁，加了canvas锁）
void load_canvas_from_file() {
    std::cout << "Loading canvas state from file..." << std::endl;
    std::ifstream file(CANVAS_PATH + current_file, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open canvas state file for reading." << std::endl;
        file_mtx.unlock();
        return;
    }
    std::string line;
    int index = 0;
    canvas_mtx.lock();
    while (std::getline(file, line) && index < CANVAS_SIZE) {
        memory_canvas[index++] = line;
    }
    canvas_mtx.unlock();
    std::cout << "Canvas state loaded successfully." << std::endl;
    file.close();
}

// 把画布状态保存到文件（没有加锁）
void save_canvas_to_file() {
    std::cout << "Saving canvas state to file..." << std::endl;
    std::ofstream file(CANVAS_PATH + current_file, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open canvas state file for writing." << std::endl;
        return;
    }
    canvas_mtx.lock();
    for (const auto& color : memory_canvas) {
        file << color << std::endl;
    }
    canvas_mtx.unlock();
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

// 拉取在线用户名单
std::vector<std::string> get_active_usernames(const std::unordered_map<crow::websocket::connection*, std::string>& connections) {
    std::vector<std::string> usernames;
    users_mutex.lock();
    for (const auto& pair : connections) {
        usernames.push_back(pair.second);
    }
    users_mutex.unlock();
    return usernames;
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

            if (type == "join") {
                // 验证用户名是否合法
                if (x["username"].t() != crow::json::type::String || !db_manager.find_user(x["username"].s())) {
                    std::cerr << "Invalid username." << std::endl;
                    conn.send_text("{\"type\": \"exception\", \"message\": \"invalid_username\"}");
                    return;
                }

                // 用户加入，广播给其他用户
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
            } else if (type == "pixel_update") {
                if (!x.has("index") || !x.has("color") || x["index"].t() != crow::json::type::Number || x["color"].t() != crow::json::type::String) {
                    conn.send_text("{\"type\": \"exception\", \"message\": \"invalid_pixel_update_data\"}");
                    std::cerr << "Invalid pixel update data." << std::endl;
                    return;
                }
                index = x["index"].i();
                color = x["color"].s();
                if (0<=index && index<CANVAS_SIZE) {
                    canvas_mtx.lock();
                    edit_history.push_back({index, memory_canvas[index]}); // 记录编辑历史
                    if (edit_history.size() > MAX_EDIT_HISTORY) {
                        edit_history.pop_front();
                    }
                    memory_canvas[index] = color; // 更新内存画布状态
                    canvas_mtx.unlock();
                } else {
                    conn.send_text("{\"type\": \"exception\", \"message\": \"fail_to_update_pixel\"}");
                    std::cerr << "Invalid pixel index: " << index << std::endl;
                    return;
                }
                broadcast_data["type"] = "pixel_update";
                users_mutex.lock();
                broadcast_data["username"] = active_users[&conn];
                users_mutex.unlock();
                broadcast_data["index"] = index;
                broadcast_data["color"] = color;
                broadcast_data["time"] = dt;
                std::cout << "Received pixel update from " << active_users.at(&conn) << ": " << message << std::endl;
                broadcast_message(broadcast_data.dump(), active_users);
            } else if (type == "get_user_list") {
                // 处理获取用户列表的请求
                broadcast_data["type"] = "user_list";
                broadcast_data["users"] = get_active_usernames(active_users);
                std::cout << "Sending user list: " << broadcast_data.dump() << std::endl;
                conn.send_text(broadcast_data.dump());
            } else if (type == "get_file_list"){
                // 处理获取文件列表的请求
                std::vector<std::string> file_list;
                std::string path = CANVAS_PATH;
                file_mtx.lock();
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    if (entry.is_regular_file()) {
                        file_list.push_back(entry.path().filename().string());
                    }
                }
                broadcast_data["current_working"] = current_file; // 当前正在编辑的文件
                file_mtx.unlock();
                broadcast_data["type"] = "file_list";
                broadcast_data["files"] = file_list;
                std::cout << "Sending file list: " << broadcast_data.dump() << std::endl;
                conn.send_text(broadcast_data.dump());
            } else if (type == "create_file"){
                if (!x.has("filename") || x["filename"].t() != crow::json::type::String) {
                    conn.send_text("{\"type\": \"exception\", \"message\": \"filename_required\"}");
                    std::cerr << "Filename is required for switching file." << std::endl;
                    return;
                }
                // 处理创建新文件的请求
                std::string filename = x["filename"].s();
                std::string new_file_path = CANVAS_PATH + filename;
                std::ofstream new_file(new_file_path);
                
                file_mtx.lock();
                if (new_file.is_open()) {
                    for (const auto& color : memory_canvas) {
                        new_file << color << std::endl;
                    }
                    new_file.close();
                    std::cout << "Created new file: " << filename << std::endl;
                } else {
                    conn.send_text("{\"type\": \"exception\", \"message\": \"fail_to_create_file\"}");
                    std::cerr << "Failed to create file: " << filename << std::endl;
                }
                file_mtx.unlock();

            } else if (type == "switch_file"){
                if (!x.has("filename") || x["filename"].t() != crow::json::type::String) {
                    conn.send_text("{\"type\": \"exception\", \"message\": \"filename_required\"}");
                    std::cerr << "Filename is required for switching file." << std::endl;
                    return;
                }
                // 处理切换文件的请求
                std::string filename = x["filename"].s();
                std::string file_path = CANVAS_PATH + filename;
                std::ifstream file(file_path);
                file_mtx.lock();

                // 检查文件名是否存在
                if (!std::filesystem::exists(file_path)) {
                    std::cerr << "File not found: " << filename << std::endl;
                    conn.send_text("{\"type\": \"exception\", \"message\": \"file_not_found\"}");
                    file_mtx.unlock();
                    return;
                }

                if (file.is_open()) {
                    // 切换文件前先保存当前画布状态到当前文件
                    file.close();
                    save_canvas_to_file();
                    // 读取文件，更新画布
                    current_file = filename; // 更新当前文件名
                    load_canvas_from_file();
                    file_mtx.unlock();

                    // 广播用户切换文件的消息
                    broadcast_data["type"] = "user_switched_file";
                    broadcast_data["filename"] = filename;
                    users_mutex.lock();
                    broadcast_data["username"] = active_users[&conn];
                    users_mutex.unlock();
                    broadcast_data["time"] = dt;
                    broadcast_message(broadcast_data.dump(), active_users);
                    std::cout << "Switched to file: " << filename << std::endl;

                    // 切换文件后广播新的画布状态
                    broadcast_canvas["type"] = "canvas";
                    canvas_mtx.lock();
                    edit_history.clear(); // 切换文件后清空编辑历史
                    broadcast_canvas["canvas"] = memory_canvas;
                    canvas_mtx.unlock();
                    broadcast_message(broadcast_canvas.dump(), active_users);
                } else {
                    conn.send_text("{\"type\": \"exception\", \"message\": \"fail_to_open_file\"}");
                    std::cerr << "Failed to open file: " << filename << std::endl;
                    file_mtx.unlock();
                }
            } else if (type == "undo"){
                // 处理撤销操作的请求
                canvas_mtx.lock();
                if (!edit_history.empty()) {
                    EditRecord last_edit = edit_history.back();
                    edit_history.pop_back();

                    // 恢复上一个状态
                    memory_canvas[last_edit.index] = last_edit.color;
                    canvas_mtx.unlock();

                    // 广播撤销操作
                    broadcast_data["type"] = "user_undone";
                    users_mutex.lock();
                    broadcast_data["username"] = active_users[&conn];
                    users_mutex.unlock();
                    broadcast_data["index"] = last_edit.index;
                    broadcast_data["color"] = last_edit.color;
                    broadcast_data["time"] = dt;
                    std::cout << "Undoing last edit by " << active_users.at(&conn) << ": " << message << std::endl;
                    broadcast_message(broadcast_data.dump(), active_users);
                } else {
                    canvas_mtx.unlock();
                    conn.send_text("{\"type\": \"exception\", \"message\": \"fail_to_undo\"}");
                    std::cerr << "No edits to undo." << std::endl;
                }
            } else {
                conn.send_text("{\"type\": \"exception\", \"message\": \"unknown_message_type\"}");
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
    file_mtx.lock();
    load_canvas_from_file();
    file_mtx.unlock();

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
    app.port(SERVER_PORT).multithreaded().run();

    // Ctrl+C 后会执行app.run()结束后的代码，保存画布状态
    file_mtx.lock();
    save_canvas_to_file();
    file_mtx.unlock();

    // 服务器关闭时会自动调用DBManager的析构函数，关闭数据库连接
    return 0;
}
#include "../include/canvas_room.h"
#include "../include/db_manager.h"
#include "../include/event_logger.h"
#include "../include/file_paths.h"
#include "../include/painter.h"
#include "crow.h"
#include "crow/middlewares/cors.h"
#include <csignal>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdio.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


constexpr uint16_t SERVER_PORT = 1145;

// request body json数据
struct AuthPayload {
  bool is_valid;
  std::string username;
  std::string password;
};

struct PixelChange;
struct CanvasRoom;

static std::mutex lobby_mtx; // 给本地磁盘文件上锁
static std::mutex users_mutex; // 给active_users上锁，保护在线用户列表的读写
static std::mutex file_mtx; // 给文件操作上锁，保护文件读写的互斥
static std::mutex time_mtx; // 给时间相关操作上锁，保护时间数据的互斥

static std::unordered_map<std::string, std::unique_ptr<CanvasRoom>>
    active_rooms; // 房间列表，key为文件名
static std::unordered_map<crow::websocket::connection *, std::string>
    active_users; // 在线用户
static std::unordered_map<crow::websocket::connection *, std::string>
    user_current_files; // 记录每个用户当前编辑的文件

// 解析请求体
AuthPayload parse_auth_request(const crow::request &req) {
  auto x = crow::json::load(req.body);
  // 检查字段
  if (!x || !x.has("username") || !x.has("password")) {
    return {false, "", ""};
  }
  return {true, x["username"].s(), x["password"].s()};
}

// 加载画布（没有加file锁，加了canvas锁）
void load_canvas_from_file(const std::string &filename, CanvasRoom *room_ptr) {
  std::cout << "Loading canvas state from file..." << std::endl;
  std::ifstream file(CANVAS_PATH + filename, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Failed to open canvas state file for reading." << std::endl;
    return;
  }
  std::string line;
  int index = 0;
  room_ptr->room_mtx.lock();
  while (std::getline(file, line) && index < CANVAS_SIZE) {
    room_ptr->canvas[index++] = line;
  }
  room_ptr->room_mtx.unlock();
  room_ptr->log_line_count =
      EventLogger::replay_canvas_state(filename, room_ptr);
  std::cout << "Canvas state loaded successfully." << std::endl;
  std::cout << "Replayed " << room_ptr->log_line_count
            << " log lines to restore canvas state." << std::endl;
  file.close();
}

// 广播消息给所有连接的用户
void broadcast_message(const std::string &message,
                       const std::unordered_map<crow::websocket::connection *,
                                                std::string> &connections) {
  users_mutex.lock();
  for (const auto &pair : connections) {
    pair.first->send_text(message);
  }
  users_mutex.unlock();
}

// 广播消息给特定房间
void broadcast_message(
    const std::string &message,
    const std::unordered_set<crow::websocket::connection *> &connections) {
  for (const auto &conn : connections) {
    conn->send_text(message);
  }
}

// 拉取在线用户名单
std::vector<std::string> get_active_usernames(
    const std::unordered_map<crow::websocket::connection *, std::string>
        &connections) {
  std::vector<std::string> usernames;
  users_mutex.lock();
  for (const auto &pair : connections) {
    usernames.push_back(pair.second);
  }
  users_mutex.unlock();
  return usernames;
}

// 打印所有房间信息，测试使用
void print_all_rooms_info() {
  lobby_mtx.lock();
  std::cout << "Current active rooms: " << std::endl;
  for (const auto &pair : active_rooms) {
    std::cout << "Room: " << pair.first
              << ", Connections: " << pair.second->connections.size()
              << std::endl;
  }
  lobby_mtx.unlock();
}

// 设置认证相关的路由
void setup_auth_routes(crow::App<crow::CORSHandler> &app,
                       DBManager &db_manager) {
  CROW_ROUTE(app, "/api/login")
      .methods(crow::HTTPMethod::POST)([&db_manager](const crow::request &req) {
        // 防止用户端可能的恶意攻击或者断网等原因导致服务器崩溃
        auto payload = parse_auth_request(req);
        if (!payload.is_valid) {
          return crow::response(
              400,
              "{\"success\": false, \"message\": \"Invalid request body\"}");
        }

        std::string username = payload.username;
        std::string password = payload.password;

        // 验证用户
        if (!db_manager.verify_user(username, password)) {
          return crow::response(401, "{\"success\": false, \"message\": "
                                     "\"Invalid username or password\"}");
        }

        return crow::response(200, "{\"success\": true, \"message\": \"Login "
                                   "successful\", \"username\": \"" +
                                       username + "\"}");
      });

  CROW_ROUTE(app, "/api/register")
      .methods(crow::HTTPMethod::POST)([&db_manager](const crow::request &req) {
        auto payload = parse_auth_request(req);
        if (!payload.is_valid) {
          return crow::response(
              400,
              "{\"success\": false, \"message\": \"Invalid request body\"}");
        }

        std::string username = payload.username;
        std::string password = payload.password;

        // 添加用户
        if (!db_manager.add_user(username, password)) {
          return crow::response(
              400,
              "{\"success\": false, \"message\": \"Failed to register user\"}");
        }

        return crow::response(200,
                              "{\"success\": true, \"message\": \"Registration "
                              "successful\", \"username\": \"" +
                                  username + "\"}");
      });
}

// 设置WebSocket相关的路由
void setup_websocket_routes(crow::App<crow::CORSHandler> &app,
                            DBManager &db_manager) {
  CROW_WEBSOCKET_ROUTE(app, "/ws/edit")
      .onopen([&](crow::websocket::connection &conn) {
        std::cout << "WebSocket connection opened. " << std::endl;
      })
      .onclose(
          [&](crow::websocket::connection &conn, const std::string &reason) {
            time_mtx.lock();
            time_t now = time(0);
            std::string dt = ctime(&now);
            time_mtx.unlock();

            // 用户离开，先从active_users中移除
            users_mutex.lock();
            std::string username = active_users[&conn];
            active_users.erase(&conn);
            users_mutex.unlock();

            // 从房间中移除用户
            lobby_mtx.lock();
            std::string old_filename = user_current_files[&conn];
            CanvasRoom *old_room_ptr = active_rooms[old_filename].get();
            user_current_files.erase(&conn);
            if (old_room_ptr) {
              old_room_ptr->room_mtx.lock();
              old_room_ptr->connections.erase(&conn);
              if (old_room_ptr->connections.empty() &&
                  old_filename != "canvas_state") {
                // 如果房间空了且不是默认的canvas_state房间，就删除这个房间
                old_room_ptr->room_mtx.unlock();
                active_rooms.erase(old_filename);
              } else {
                old_room_ptr->room_mtx.unlock();
              }
            }
            lobby_mtx.unlock();

            // 用户离开，广播给其他用户
            crow::json::wvalue broadcast_data;
            broadcast_data["type"] = "user_left";
            broadcast_data["username"] = username;
            broadcast_data["time"] = dt;
            broadcast_message(broadcast_data.dump(), active_users);
            std::cout << "WebSocket connection closed. " << std::endl;
          })
      .onmessage([&](crow::websocket::connection &conn,
                     const std::string &message, bool is_binary) {
        time_mtx.lock();
        time_t now = time(0);
        std::string dt = ctime(&now);
        time_mtx.unlock();
        if (is_binary)
          return;
        auto x = crow::json::load(message);
        if (!x) {
          std::cerr << "Failed to parse WebSocket message." << std::endl;
          return;
        }

        std::string type = x["type"].s();
        std::cout << "Received message of type: " << type << std::endl;
        std::string username;
        crow::json::wvalue broadcast_data;
        crow::json::wvalue broadcast_canvas;
        int index;
        std::string color;

        if (type == "join") {
          // 验证用户名是否合法
          if (x["username"].t() != crow::json::type::String ||
              !db_manager.find_user(x["username"].s())) {
            std::cerr << "Invalid username." << std::endl;
            conn.send_text(
                "{\"type\": \"exception\", \"message\": \"invalid_username\"}");
            return;
          }

          // 用户加入，广播给其他用户
          username = x["username"].s();
          std::cout << "User " << username << " joined the document."
                    << std::endl;
          users_mutex.lock();
          active_users.insert({&conn, username});
          users_mutex.unlock();
          broadcast_data["type"] = "user_joined";
          broadcast_data["username"] = username;
          broadcast_data["time"] = dt;
          broadcast_message(broadcast_data.dump(), active_users);

          // 发送当前画布状态给新加入的用户
          lobby_mtx.lock();
          user_current_files.insert(
              {&conn, "canvas_state"}); // 默认加入canvas_state房间
          CanvasRoom *room_ptr =
              active_rooms.find("canvas_state")->second.get();
          lobby_mtx.unlock();
          broadcast_canvas["type"] = "canvas";
          room_ptr->room_mtx.lock();
          room_ptr->connections.insert(&conn); // 将用户加入房间
          broadcast_canvas["canvas"] = room_ptr->canvas;
          room_ptr->room_mtx.unlock();

          std::cout << "Sending current canvas state "
                    << broadcast_canvas.dump() << " to " << username
                    << std::endl;
          conn.send_text(broadcast_canvas.dump());
        } else if (type == "pixel_update") {
          if (!x.has("index") || !x.has("color") ||
              x["index"].t() != crow::json::type::Number ||
              x["color"].t() != crow::json::type::String) {
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"invalid_pixel_update_data\"}");
            std::cerr << "Invalid pixel update data." << std::endl;
            return;
          }
          index = x["index"].i();
          color = x["color"].s();
          // 给大厅上锁，获取room指针
          lobby_mtx.lock();
          CanvasRoom *room_ptr = active_rooms[user_current_files[&conn]].get();
          lobby_mtx.unlock();
          if (0 <= index && index < CANVAS_SIZE) {
            // 给房间上锁，更新画布状态和编辑历史
            room_ptr->room_mtx.lock();
            Painter::pixel_paint(room_ptr, index, color);
            room_ptr->room_mtx.unlock();
          } else {
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"fail_to_update_pixel\"}");
            std::cerr << "Invalid pixel index: " << index << std::endl;
            return;
          }

          std::string username;
          std::string filename;

          broadcast_data["type"] = "pixel_update";
          users_mutex.lock();
          username = active_users[&conn];
          broadcast_data["username"] = username;
          users_mutex.unlock();
          broadcast_data["index"] = index;
          broadcast_data["color"] = color;
          broadcast_data["time"] = dt;
          lobby_mtx.lock();
          filename = user_current_files[&conn];
          broadcast_data["filename"] = filename;
          users_mutex.lock();
          std::cout << "Received pixel update from " << active_users.at(&conn)
                    << ": " << message << " for file " << filename << std::endl;
          users_mutex.unlock();
          lobby_mtx.unlock();
          room_ptr->room_mtx.lock();
          broadcast_message(broadcast_data.dump(), room_ptr->connections);
          if (room_ptr->add_log_line()) {
            room_ptr->add_log_line();
            room_ptr->room_mtx.unlock();
            EventLogger::transfer_log_to_canvas(filename, room_ptr);
          } else {
            room_ptr->room_mtx.unlock();
          }
          EventLogger::PaintedEvent event =
              EventLogger::create_pixel_painted_event(dt, username, index,
                                                      color);
          EventLogger::append_event_to_log(filename, event);

        } else if (type == "square_update") {
          // 处理方形涂色的请求
          if (!x.has("index") || !x.has("size") || !x.has("color") ||
              x["index"].t() != crow::json::type::Number ||
              x["size"].t() != crow::json::type::Number ||
              x["color"].t() != crow::json::type::String) {
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"invalid_square_update_data\"}");
            std::cerr << "Invalid square update data." << std::endl;
            return;
          }
          index = x["index"].i();
          int size = x["size"].i();
          color = x["color"].s();
          // 给大厅上锁，获取room指针
          lobby_mtx.lock();
          CanvasRoom *room_ptr = active_rooms[user_current_files[&conn]].get();
          lobby_mtx.unlock();
          if (0 <= index && index < CANVAS_SIZE && size > 0 && size <= ROW) {
            // 给房间上锁，更新画布状态和编辑历史
            room_ptr->room_mtx.lock();
            Painter::square_paint(room_ptr, index, size, color);
            room_ptr->room_mtx.unlock();
          } else {
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"fail_to_update_square\"}");
            std::cerr << "Invalid square update data: index=" << index
                      << ", size=" << size << std::endl;
            return;
          }

          // 包装广播数据
          std::string username;
          std::string filename;

          broadcast_data["type"] = "square_update";
          users_mutex.lock();
          username = active_users[&conn];
          broadcast_data["username"] = username;
          users_mutex.unlock();
          broadcast_data["index"] = index;
          broadcast_data["color"] = color;
          broadcast_data["time"] = dt;
          broadcast_data["size"] = size;
          lobby_mtx.lock();
          filename = user_current_files[&conn];
          broadcast_data["filename"] = filename;
          users_mutex.lock();
          std::cout << "Received square update from " << username << ": "
                    << message << "in size " << size << " for file " << filename
                    << std::endl;
          users_mutex.unlock();
          lobby_mtx.unlock();
          room_ptr->room_mtx.lock();
          broadcast_message(broadcast_data.dump(), room_ptr->connections);
          if (room_ptr->add_log_line()) {
            room_ptr->add_log_line();
            room_ptr->room_mtx.unlock();
            EventLogger::transfer_log_to_canvas(filename, room_ptr);
          } else {
            room_ptr->room_mtx.unlock();
          }
          EventLogger::PaintedEvent event =
              EventLogger::create_square_painted_event(dt, username, index,
                                                       size, color);
          EventLogger::append_event_to_log(filename, event);

        } else if (type == "get_user_list") {
          // 处理获取用户列表的请求
          broadcast_data["type"] = "user_list";
          broadcast_data["users"] = get_active_usernames(active_users);
          std::cout << "Sending user list: " << broadcast_data.dump()
                    << std::endl;
          conn.send_text(broadcast_data.dump());
        } else if (type == "get_file_list") {
          // 处理获取文件列表的请求
          std::vector<std::string> file_list;
          std::string path = CANVAS_PATH;
          file_mtx.lock();
          for (const auto &entry : std::filesystem::directory_iterator(path)) {
            if (entry.is_regular_file()) {
              file_list.push_back(entry.path().filename().string());
            }
          }

          // 当前正在编辑的文件
          file_mtx.unlock();
          broadcast_data["type"] = "file_list";
          broadcast_data["files"] = file_list;
          std::cout << "Sending file list: " << broadcast_data.dump()
                    << std::endl;
          conn.send_text(broadcast_data.dump());
        } else if (type == "create_file") {
          if (!x.has("filename") ||
              x["filename"].t() != crow::json::type::String) {
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"filename_required\"}");
            std::cerr << "Filename is required for switching file."
                      << std::endl;
            return;
          }
          // 处理创建新文件的请求
          std::string filename = x["filename"].s();
          std::string new_file_path = CANVAS_PATH + filename;
          std::ofstream new_file(new_file_path);

          file_mtx.lock();
          if (new_file.is_open()) {
            for (int i = 0; i < CANVAS_SIZE; i++) {
              new_file << "#FFFFFF" << std::endl;
            }
            new_file.close();
            std::cout << "Created new file: " << filename << std::endl;
          } else {
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"fail_to_create_file\"}");
            std::cerr << "Failed to create file: " << filename << std::endl;
          }
          file_mtx.unlock();

        } else if (type == "switch_file") {
          if (!x.has("filename") ||
              x["filename"].t() != crow::json::type::String) {
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"filename_required\"}");
            std::cerr << "Filename is required for switching file."
                      << std::endl;
            return;
          }
          // 处理切换文件的请求
          std::string filename = x["filename"].s();
          std::string file_path = CANVAS_PATH + filename;
          file_mtx.lock();
          std::ifstream file(file_path);

          // 检查文件名是否存在
          if (!std::filesystem::exists(file_path)) {
            std::cerr << "File not found: " << filename << std::endl;
            conn.send_text(
                "{\"type\": \"exception\", \"message\": \"file_not_found\"}");
            file_mtx.unlock();
            return;
          }

          if (file.is_open()) {
            file.close();
            file_mtx.unlock();

            // 如果房间没有被加载，读取文件，更新画布
            lobby_mtx.lock();
            std::string old_filename = user_current_files[&conn];
            if (old_filename == filename) {
              lobby_mtx.unlock();
              std::cerr << "User is already in the requested file: " << filename
                        << std::endl;
              return;
            }

            CanvasRoom *new_room_ptr =
                active_rooms.find(filename) != active_rooms.end()
                    ? active_rooms[filename].get()
                    : nullptr;
            if (!new_room_ptr) {
              active_rooms.insert({filename, std::make_unique<CanvasRoom>()});
              new_room_ptr = active_rooms.find(filename)->second.get();
              file_mtx.lock();
              load_canvas_from_file(filename, new_room_ptr);
              file_mtx.unlock();
            }

            // 将用户从原房间中移除，更新user_current_files
            CanvasRoom *old_room_ptr =
                active_rooms[user_current_files[&conn]].get();
            user_current_files[&conn] = filename; // 更新用户当前编辑的文件

            if (old_room_ptr) {
              old_room_ptr->room_mtx.lock();
              old_room_ptr->connections.erase(&conn);
              if (old_room_ptr->connections.empty() &&
                  old_room_ptr != active_rooms["canvas_state"].get()) {
                // 如果房间空了且不是默认的canvas_state房间，就删除这个房间
                old_room_ptr->room_mtx.unlock();
                active_rooms.erase(old_filename);
              } else {
                old_room_ptr->room_mtx.unlock();
              }
            }

            // 将用户加入新房间

            new_room_ptr->room_mtx.lock();
            new_room_ptr->connections.insert(&conn);
            new_room_ptr->room_mtx.unlock();

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
            new_room_ptr->room_mtx.lock();
            broadcast_canvas["canvas"] = new_room_ptr->canvas;
            new_room_ptr->room_mtx.unlock();

            lobby_mtx.unlock();

            conn.send_text(broadcast_canvas.dump());
          } else {
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"fail_to_open_file\"}");
            file_mtx.unlock();
            std::cerr << "Failed to open file: " << filename << std::endl;
          }
        } else if (type == "undo") {
          // 处理撤销操作的请求

          lobby_mtx.lock();
          std::string filename = user_current_files[&conn];
          CanvasRoom *room_ptr = active_rooms[filename].get();
          lobby_mtx.unlock();
          room_ptr->room_mtx.lock();
          // 恢复上一个状态
          if (Painter::undo_paint(room_ptr, broadcast_data)) {
            room_ptr->room_mtx.unlock();
            std::cout<< broadcast_data.dump() << std::endl;
            // 广播撤销操作
            std::string username;

            broadcast_data["type"] = "user_undone";
            users_mutex.lock();
            username = active_users[&conn];
            broadcast_data["username"] = username;
            users_mutex.unlock();
            broadcast_data["time"] = dt;
            broadcast_data["filename"] = filename;
            std::cout << "Undoing last edit by " << username
                      << ": " << message << " for file " << filename
                      << std::endl;
            room_ptr->room_mtx.lock();
            broadcast_message(broadcast_data.dump(), room_ptr->connections);
            if (room_ptr->add_log_line()) {
              room_ptr->add_log_line();
              room_ptr->room_mtx.unlock();
              EventLogger::transfer_log_to_canvas(filename, room_ptr);
            } else {
              room_ptr->room_mtx.unlock();
            }
            EventLogger::PaintedEvent event =
                EventLogger::create_undo_event(dt, username);
            EventLogger::append_event_to_log(filename, event);
          } else {
            room_ptr->room_mtx.unlock();
            std::cerr << "Failed to undo for user " << active_users.at(&conn)
                      << " in file " << filename << std::endl;
            return;
          }
        } else {
          conn.send_text("{\"type\": \"exception\", \"message\": "
                         "\"unknown_message_type\"}");
          std::cerr << "Unknown message type: " << type << std::endl;
          return;
        }
      });
}

int main() {
  // 初始化数据库管理器
  DBManager db_manager(DB_PATH);
  if (!db_manager.init_tables()) {
    std::cerr << "Failed to initialize database tables." << std::endl;
    return 1;
  }

  // 创建第一个房间并初始化画布
  active_rooms.insert({"canvas_state", std::make_unique<CanvasRoom>()});
  file_mtx.lock();
  load_canvas_from_file("canvas_state",
                        active_rooms.find("canvas_state")->second.get());
  file_mtx.unlock();

  // 初始化Crow应用和CORS中间件
  crow::App<crow::CORSHandler> app;
  auto &cors = app.get_middleware<crow::CORSHandler>();

  // 全局规则设定
  cors.global()
      .headers("X-Custom-Header", "Content-Type",
               "Accept") // 允许的自定义请求头
      .methods("POST"_method, "GET"_method, "OPTIONS"_method)
      .origin("*")
      .max_age(3600);

  setup_auth_routes(app, db_manager);
  setup_websocket_routes(app, db_manager);
  app.port(SERVER_PORT).multithreaded().run();

  // 服务器关闭时会自动调用DBManager的析构函数，关闭数据库连接
  return 0;
}
#include "../include/canvas_room.h"
#include "../include/db_manager.h"
#include "../include/event_logger.h"
#include "../include/file_paths.h"
#include "../include/painter.h"
#include "crow.h"
#include "crow/middlewares/cors.h"
#include <condition_variable>
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

static std::mutex lobby_mtx;   // 给本地磁盘文件上锁
static std::mutex users_mutex; // 给active_users上锁，保护在线用户列表的读写
static std::mutex time_mtx;    // 给时间相关操作上锁，保护时间数据的互斥

static std::unordered_map<std::string, std::unique_ptr<CanvasRoom>>
    active_rooms; // 房间列表，key为文件名
static std::unordered_map<crow::websocket::connection *, std::string>
    active_users; // 在线用户
static std::unordered_map<crow::websocket::connection *, std::string>
    user_current_files; // 记录每个用户当前编辑的文件

// 读写解耦，负责广播
struct BroadcastTask {
  std::string filename; // 目标房间名
  std::string message;  // 要发送的 JSON 字符串
};

static std::deque<BroadcastTask> broadcast_queue; // 邮筒（任务队列）
static std::mutex bq_mtx;                         // 邮筒专属保护锁
static std::condition_variable bq_cv;             // 邮筒专属条件变量

void broadcast_worker() {
  while (true) {
    BroadcastTask task;

    {
      std::unique_lock<std::mutex> lock(bq_mtx);
      // 等待直到有任务进来
      bq_cv.wait(lock, [] { return !broadcast_queue.empty(); });

      task = broadcast_queue.front();
      broadcast_queue.pop_front();
    }
    std::vector<crow::websocket::connection *> target_conns;

    if (task.filename == "GLOBAL_ALL") {
      users_mutex.lock();
      for (const auto &pair : active_users) {
        target_conns.push_back(pair.first);
      }
      users_mutex.unlock();
    } else {
      lobby_mtx.lock();
      if (active_rooms.find(task.filename) != active_rooms.end()) {
        CanvasRoom *room_ptr = active_rooms[task.filename].get();
        room_ptr->room_mtx.lock();
        for (auto conn : room_ptr->connections) {
          target_conns.push_back(conn);
        }
        room_ptr->room_mtx.unlock();
      }
      lobby_mtx.unlock();
    }

    for (auto conn : target_conns) {
      conn->send_text(task.message);
    }
  }
}

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
void load_canvas_from_file(const std::string &filename, CanvasRoom *room_ptr,
                           DBManager &db_manager) {
  std::cout << "Loading canvas state from file..." << std::endl;
  std::ifstream file(CANVAS_PATH + filename, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Failed to open canvas state file for reading." << std::endl;
    return;
  }
  std::string line;
  int index = 0;
  room_ptr->room_mtx.lock();
  while (std::getline(file, line) && index < room_ptr->get_size()) {
    room_ptr->canvas[index++] = line;
  }
  room_ptr->room_mtx.unlock();
  EventLogger::replay_canvas_state(filename, room_ptr);
  std::cout << "Canvas state loaded successfully." << std::endl;
  file.close();
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
            crow::json::wvalue broadcast_data =
                EventLogger::create_user_left_event(dt, username);

            {
              std::lock_guard<std::mutex> lock(bq_mtx);
              broadcast_queue.push_back({"GLOBAL_ALL", broadcast_data.dump()});
            }
            bq_cv.notify_one();

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
        std::cout << "Received message of type: " << type << message
                  << std::endl;
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
          broadcast_data = EventLogger::create_user_joined_event(dt, username);

          {
            // 发送全局消息
            std::lock_guard<std::mutex> lock(bq_mtx);
            broadcast_queue.push_back({"GLOBAL_ALL", broadcast_data.dump()});
          }

          bq_cv.notify_one();

          // 发送当前画布状态给新加入的用户
          lobby_mtx.lock();
          user_current_files.insert(
              {&conn, "canvas_state"}); // 默认加入canvas_state房间
          CanvasRoom *room_ptr =
              active_rooms.find("canvas_state")->second.get();
          lobby_mtx.unlock();

          std::vector<uint8_t> binary_data;
          binary_data.reserve(room_ptr->get_size() * 3);
          
          room_ptr->room_mtx.lock();
          room_ptr->connections.insert(&conn); // 将用户加入房间
          binary_data = EventLogger::create_binary_canvas_data(room_ptr);
          room_ptr->room_mtx.unlock();

          std::cout << "Sending current canvas state to " << username
                    << std::endl;
          
          crow::json::wvalue broadcast_canvas_incoming = EventLogger::create_canvas_incoming_event(room_ptr->get_width(), room_ptr->get_height());
          conn.send_text(broadcast_canvas_incoming.dump());
          conn.send_binary(std::string(binary_data.begin(), binary_data.end()));
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
          std::string username;
          std::string filename;

          users_mutex.lock();
          username = active_users[&conn];
          users_mutex.unlock();

          lobby_mtx.lock();
          filename = user_current_files[&conn];
          CanvasRoom *room_ptr = active_rooms[user_current_files[&conn]].get();
          room_ptr->room_mtx.lock();
          lobby_mtx.unlock();

          broadcast_data = EventLogger::create_pixel_painted_event(
              dt, username, filename, index, color);
          EventLogger::append_event_to_log(filename, broadcast_data);

          if (0 <= index && index < room_ptr->get_size()) {
            Painter::pixel_paint(room_ptr, index, color);
          } else {
            room_ptr->room_mtx.unlock();
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"fail_to_update_pixel\"}");
            std::cerr << "Invalid pixel index: " << index << std::endl;
            return;
          }

          // 把广播信件塞进邮筒
          {
            std::lock_guard<std::mutex> lock(bq_mtx);
            broadcast_queue.push_back({filename, broadcast_data.dump()});
          }
          bq_cv.notify_one();

          if (room_ptr->add_log_line()) {
            room_ptr->add_log_line();
            std::vector<std::string> canvas_copy = room_ptr->canvas; // 复制当前画布状态，避免在转移过程中被修改
            room_ptr->room_mtx.unlock();
            EventLogger::transfer_log_to_canvas(filename, canvas_copy);
          } else {
            room_ptr->room_mtx.unlock();
          }

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

          std::string username;
          std::string filename;

          users_mutex.lock();
          username = active_users[&conn];
          users_mutex.unlock();

          lobby_mtx.lock();
          filename = user_current_files[&conn];
          CanvasRoom *room_ptr = active_rooms[user_current_files[&conn]].get();
          room_ptr->room_mtx.lock();
          lobby_mtx.unlock();

          // 包装广播数据
          broadcast_data = EventLogger::create_square_painted_event(
              dt, username, filename, index, size, color);
          EventLogger::append_event_to_log(filename, broadcast_data);

          if (0 <= index && index < room_ptr->get_size() && size > 0 &&
              size <= room_ptr->get_height()) {
            // 给房间上锁，更新画布状态和编辑历史
            Painter::square_paint(room_ptr, index, size, color);
          } else {
            room_ptr->room_mtx.unlock();
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"fail_to_update_square\"}");
            std::cerr << "Invalid square update data: index=" << index
                      << ", size=" << size << std::endl;
            return;
          }

          // 把广播信件塞进邮筒
          {
            std::lock_guard<std::mutex> lock(bq_mtx);
            broadcast_queue.push_back({filename, broadcast_data.dump()});
          }
          bq_cv.notify_one();

          if (room_ptr->add_log_line()) {
            room_ptr->add_log_line();
            std::vector<std::string> canvas_copy = room_ptr->canvas; // 复制当前画布状态，避免在转移过程中被修改
            room_ptr->room_mtx.unlock();
            EventLogger::transfer_log_to_canvas(filename, canvas_copy);
          } else {
            room_ptr->room_mtx.unlock();
          }
        } else if (type == "get_user_list") {
          // 处理获取用户列表的请求
          broadcast_data["type"] = "user_list";
          broadcast_data["users"] = get_active_usernames(active_users);
          std::cout << "Sending user list: " << broadcast_data.dump()
                    << std::endl;
          conn.send_text(broadcast_data.dump());
        } else if (type == "get_file_list") {
          // 处理获取文件列表的请求
          broadcast_data["type"] = "file_list";
          broadcast_data["files"] = db_manager.get_canvas_list();
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

          int width = DEFAULT_COL;
          int height = DEFAULT_ROW;

          if (!x.has("width") || !x.has("height") ||
              x["width"].t() != crow::json::type::Number ||
              x["height"].t() != crow::json::type::Number) {
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"invalid_canvas_size\"}");
            std::cerr << "Invalid canvas size for new file." << std::endl;
            return;
          } else {
            width = x["width"].i();
            height = x["height"].i();
            if (width <= 0 || height <= 0) {
              conn.send_text("{\"type\": \"exception\", \"message\": "
                             "\"invalid_canvas_size\"}");
              std::cerr << "Invalid canvas size for new file." << std::endl;
              return;
            }
          }

          db_manager.create_canvas_metadata(filename, width, height, dt);

          file_mtx.lock();
          if (new_file.is_open()) {
            for (int i = 0; i < width * height; i++) {
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

        } else if (type == "line_update") {
          // 处理绘制线条的请求
          if (!x.has("start_index") || !x.has("end_index") || !x.has("color") ||
              x["start_index"].t() != crow::json::type::Number ||
              x["end_index"].t() != crow::json::type::Number ||
              x["color"].t() != crow::json::type::String) {
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"invalid_line_update_data\"}");
            std::cerr << "Invalid line update data." << std::endl;
            return;
          }
          int start_index = x["start_index"].i();
          int end_index = x["end_index"].i();
          std::string color = x["color"].s();

          std::string username;
          std::string filename;

          users_mutex.lock();
          username = active_users[&conn];
          users_mutex.unlock();

          lobby_mtx.lock();
          filename = user_current_files[&conn];
          CanvasRoom *room_ptr = active_rooms[user_current_files[&conn]].get();
          room_ptr->room_mtx.lock();
          lobby_mtx.unlock();

          // 包装广播数据
          if (0 <= start_index && start_index < room_ptr->get_size() &&
              0 <= end_index && end_index < room_ptr->get_size()) {
            Painter::line_paint(room_ptr, start_index, end_index, color);
          } else {
            room_ptr->room_mtx.unlock();
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"fail_to_update_line\"}");
            std::cerr << "Invalid line update data: start_index=" << start_index
                      << ", end_index=" << end_index << std::endl;
            return;
          }

          broadcast_data = EventLogger::create_line_painted_event(
              dt, username, filename, start_index, end_index, color);
          EventLogger::append_event_to_log(filename, broadcast_data);          

          // 把广播信件塞进邮筒
          {
            std::lock_guard<std::mutex> lock(bq_mtx);
            broadcast_queue.push_back({filename, broadcast_data.dump()});
          }
          bq_cv.notify_one();

          if (room_ptr->add_log_line()) {
            room_ptr->add_log_line();
            std::vector<std::string> canvas_copy = room_ptr->canvas; // 复制当前画布状态，避免在转移过程中被修改
            room_ptr->room_mtx.unlock();
            EventLogger::transfer_log_to_canvas(filename, canvas_copy);
          } else {
            room_ptr->room_mtx.unlock();
          }
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

            std::string username;
            users_mutex.lock();
            username = active_users[&conn];
            users_mutex.unlock();

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
              int width, height;
              std::string created_at;
              db_manager.get_canvas_metadata(filename, width, height,
                                             created_at);
              active_rooms.insert({filename, std::make_unique<CanvasRoom>(
                                                 height, width, created_at)});
              new_room_ptr = active_rooms.find(filename)->second.get();
              file_mtx.lock();
              load_canvas_from_file(filename, new_room_ptr, db_manager);
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

            // 广播用户切换文件的消息
            broadcast_data["type"] = "user_switched_file";
            broadcast_data["filename"] = filename;
            broadcast_data["username"] = username;
            broadcast_data["time"] = dt;

            // 将用户加入新房间

            new_room_ptr->room_mtx.lock();
            new_room_ptr->connections.insert(&conn);

            {
              std::lock_guard<std::mutex> lock(bq_mtx);
              broadcast_queue.push_back({"GLOBAL_ALL", broadcast_data.dump()});
            }

            bq_cv.notify_one();
            std::cout << "Switched to file: " << filename << std::endl;

            // 切换文件后广播新的画布状态

            crow::json::wvalue broadcast_canvas_incoming = EventLogger::create_canvas_incoming_event(new_room_ptr->get_width(), new_room_ptr->get_height());
            std::vector<uint8_t> binary_data;
            binary_data = EventLogger::create_binary_canvas_data(new_room_ptr);
            new_room_ptr->room_mtx.unlock();

            lobby_mtx.unlock();

            conn.send_text(broadcast_canvas_incoming.dump());
            conn.send_binary(std::string(binary_data.begin(), binary_data.end()));
          } else {
            conn.send_text("{\"type\": \"exception\", \"message\": "
                           "\"fail_to_open_file\"}");
            file_mtx.unlock();
            std::cerr << "Failed to open file: " << filename << std::endl;
          }
        } else if (type == "undo") {
          // 处理撤销操作的请求
          users_mutex.lock();
          username = active_users[&conn];
          users_mutex.unlock();

          lobby_mtx.lock();
          std::string filename = user_current_files[&conn];
          CanvasRoom *room_ptr = active_rooms[filename].get();
          room_ptr->room_mtx.lock();
          lobby_mtx.unlock();

          // 包装广播数据
          broadcast_data = EventLogger::create_undo_event(dt, username,
                                                            filename, room_ptr);
          room_ptr->edit_history.pop_back();
          EventLogger::append_event_to_log(filename, broadcast_data);

          // 恢复上一个状态
          if (Painter::undo_paint(room_ptr)) {
            std::string username;
            std::cout << broadcast_data.dump() << std::endl;
          
            // 把广播信件塞进邮筒
            {
              std::lock_guard<std::mutex> lock(bq_mtx);
              broadcast_queue.push_back({filename, broadcast_data.dump()});
            }
            bq_cv.notify_one();

            std::cout << "Undoing last edit by " << username << ": " << message
                      << " for file " << filename << std::endl;
            if (room_ptr->add_log_line()) {
              room_ptr->add_log_line();
              std::vector<std::string> canvas_copy = room_ptr->canvas; // 复制当前画布状态，避免在转移过程中被修改
              room_ptr->room_mtx.unlock();
              EventLogger::transfer_log_to_canvas(filename, canvas_copy);
            } else {
              room_ptr->room_mtx.unlock();
            }

          } else {
            room_ptr->room_mtx.unlock();
            std::cerr << "Failed to undo for user " << username << " in file "
                      << filename << std::endl;
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
  if (!db_manager.init_users_table() ||
      !db_manager.init_canvas_metadata_table()) {
    std::cerr << "Failed to initialize database tables." << std::endl;
    return 1;
  }

  // 创建第一个房间并初始化画布
  active_rooms.insert({"canvas_state", std::make_unique<CanvasRoom>(
                                           DEFAULT_COL, DEFAULT_ROW, "")});
  file_mtx.lock();
  load_canvas_from_file("canvas_state",
                        active_rooms.find("canvas_state")->second.get(),
                        db_manager);
  db_manager.create_canvas_metadata("canvas_state", DEFAULT_COL, DEFAULT_ROW,
                                    "");
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

  // 设置路由
  setup_auth_routes(app, db_manager);
  setup_websocket_routes(app, db_manager);

  // 启动广播线程
  std::thread worker(broadcast_worker);
  worker.detach();

  app.port(SERVER_PORT).multithreaded().run();

  std::cout << "Ctrl + C again to stop the server." << std::endl;

  // 服务器关闭时会自动调用DBManager的析构函数，关闭数据库连接
  return 0;
}
#include "../include/event_logger.h"

std::mutex file_mtx; // 给文件操作上锁，保护文件读写的互斥

// 打包json字符串和广播信息
crow::json::wvalue
EventLogger::create_user_joined_event(const std::string &timestamp,
                                      const std::string &username) {
  crow::json::wvalue event;
  event["timestamp"] = timestamp;
  event["username"] = username;
  event["type"] = "user_joined";
  return event;
}

crow::json::wvalue
EventLogger::create_user_left_event(const std::string &timestamp,
                                    const std::string &username) {
  crow::json::wvalue event;
  event["timestamp"] = timestamp;
  event["username"] = username;
  event["type"] = "user_left";
  return event;
}

crow::json::wvalue EventLogger::create_canvas_incoming_event(int width,
                                                             int height) {
  crow::json::wvalue event;
  event["type"] = "canvas_incoming";
  event["width"] = width;
  event["height"] = height;
  return event;
}

// 创建draw信息
crow::json::wvalue EventLogger::create_pixel_painted_event(
    const std::string &timestamp, const std::string &username,
    const std::string &filename, int index, const std::string &color) {
  crow::json::wvalue event;
  event["timestamp"] = timestamp;
  event["username"] = username;
  event["filename"] = filename;
  event["index"] = index;
  event["color"] = color;
  event["type"] = "pixel_update";
  return event;
}

crow::json::wvalue
EventLogger::create_square_painted_event(const std::string &timestamp,
                                         const std::string &username,
                                         const std::string &filename, int index,
                                         int size, const std::string &color) {
  crow::json::wvalue event;
  event["timestamp"] = timestamp;
  event["username"] = username;
  event["filename"] = filename;
  event["index"] = index;
  event["size"] = size;
  event["color"] = color;
  event["type"] = "square_update";
  return event;
}

crow::json::wvalue EventLogger::create_line_painted_event(
    const std::string &timestamp, const std::string &username,
    const std::string &filename, int start_index, int end_index,
    const std::string &color) {
  crow::json::wvalue event;
  event["timestamp"] = timestamp;
  event["username"] = username;
  event["filename"] = filename;
  event["start_index"] = start_index;
  event["end_index"] = end_index;
  event["color"] = color;
  event["type"] = "line_update";
  return event;
}

crow::json::wvalue EventLogger::create_undo_event(const std::string &timestamp,
                                                  const std::string &username,
                                                  const std::string &filename,
                                                  const CanvasRoom *room_ptr) {
  crow::json::wvalue event;
  event["timestamp"] = timestamp;
  event["username"] = username;
  event["filename"] = filename;
  event["type"] = "user_undone";
  std::vector<int> indices;
  std::vector<std::string> colors;

  for (const auto &change : room_ptr->edit_history.back().changes) {
    indices.push_back(change.index);
    colors.push_back(change.color);
  }

  event["indices"] = indices;
  event["colors"] = colors;

  return event;
}

// 创建空日志文件
bool EventLogger::create_blank_log(const std::string &filename) {
  std::string file_path = LOG_PATH + filename + LOG_EXTENSION;
  file_mtx.lock();
  std::ofstream log_file(file_path);
  if (!log_file.is_open()) {
    std::cerr << "Failed to create log file: " << file_path << std::endl;
    file_mtx.unlock();
    return false;
  }
  log_file.close();
  file_mtx.unlock();
  return true;
}

// 从日志文件重放事件恢复画布状态
void EventLogger::replay_canvas_state(const std::string &filename,
                                      CanvasRoom *room_ptr) {
  std::string file_path = LOG_PATH + filename + LOG_EXTENSION;
  std::ifstream log_file(file_path);
  if (!log_file.is_open()) {
    std::cerr << "Failed to open log file for replay: " << file_path
              << std::endl;
    return;
  }
  std::string line;
  while (std::getline(log_file, line)) {
    // 这里假设每行都是一个 JSON 字符串，表示一个 PaintedEvent
    auto x = crow::json::load(line);
    if (!x) {
      std::cerr << "Failed to parse log line: " << line << std::endl;
      continue;
    }

    std::string event_type = x["type"].s();

    // 应用这个事件到画布状态
    if (event_type == "pixel_update") {
      room_ptr->room_mtx.lock();
      Painter::pixel_paint(room_ptr, x["index"].i(), x["color"].s());
      room_ptr->room_mtx.unlock();
    } else if (event_type == "square_update") {
      int event_size = x["size"].i();
      room_ptr->room_mtx.lock();
      Painter::square_paint(room_ptr, x["index"].i(), event_size,
                            x["color"].s());
      room_ptr->room_mtx.unlock();
    } else if (event_type == "line_update") {
      int start_index = x["start_index"].i();
      int end_index = x["end_index"].i();
      std::string color = x["color"].s();
      room_ptr->room_mtx.lock();
      Painter::line_paint(room_ptr, start_index, end_index, color);
      room_ptr->room_mtx.unlock();
    } else if (event_type == "multipixel_update" ||
               event_type == "user_undone") {
      room_ptr->room_mtx.lock();
      Painter::multipixel_paint(x["indices"], x["colors"], room_ptr);
      room_ptr->room_mtx.unlock();
    } else {
      std::cerr << "Unknown event type in log: " << event_type << std::endl;
    }
    room_ptr->add_log_line();
  }
  room_ptr->edit_history.clear(); // 重放日志时不保留编辑历史，避免撤销操作影响重放结果
  log_file.close();
}

// 将事件追加到日志文件
void EventLogger::append_event_to_log(const std::string &filename,
                                      const crow::json::wvalue &event) {
  std::string file_path = LOG_PATH + filename + LOG_EXTENSION;
  file_mtx.lock();
  std::ofstream log_file(file_path, std::ios::app);
  if (!log_file.is_open()) {
    std::cerr << "Failed to open log file for appending: " << file_path
              << std::endl;
    file_mtx.unlock();
    return;
  }
  log_file << event.dump() << std::endl;
  log_file.close();
  file_mtx.unlock();
}

// 清空log文件内容
void EventLogger::clear_log_file(const std::string &filename) {
  std::string file_path = LOG_PATH + filename + LOG_EXTENSION;
  file_mtx.lock();
  std::ofstream log_file(file_path, std::ios::trunc);
  if (!log_file.is_open()) {
    std::cerr << "Failed to open log file for clearing: " << file_path
              << std::endl;
    file_mtx.unlock();
    return;
  }
  log_file.close();
  file_mtx.unlock();
}

// 将日志文件内容转移到新文件（比如canvas文件），并清空日志文件内容
void EventLogger::transfer_log_to_canvas(
    const std::string &filename, const std::vector<uint8_t> &binary_canvas) {
  std::string canvas_path = CANVAS_PATH + filename;
  file_mtx.lock();
  std::ofstream canvas_file(canvas_path, std::ios::binary);
  if (!canvas_file.is_open()) {
    std::cerr << "Failed to open canvas file for writing: " << canvas_path
              << std::endl;
    file_mtx.unlock();
    return;
  }
  for (const uint8_t byte : binary_canvas) {
    canvas_file << byte;
  }
  canvas_file.close();
  file_mtx.unlock();
  EventLogger::clear_log_file(filename);
}
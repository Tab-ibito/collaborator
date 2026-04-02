#include "../include/event_logger.h"

static std::mutex file_mtx; //给文件操作上锁，保护文件读写的互斥

// 创建event实例
EventLogger::PaintedEvent EventLogger::create_pixel_painted_event(const std::string& timestamp, const std::string& username, int index, const std::string& color) {
    EventLogger::PaintedEvent event;
    event.timestamp = timestamp;
    event.username = username;
    event.index = index;
    event.color = color;
    event.type  = "pixel_paint";
    return event;
}

EventLogger::PaintedEvent EventLogger::create_square_painted_event(const std::string& timestamp, const std::string& username, int index, int size, const std::string& color) {
    EventLogger::PaintedEvent event;
    event.timestamp = timestamp;
    event.username = username;
    event.index = index;
    event.size = size;
    event.color = color;
    event.type  = "square_paint";
    return event;
}

EventLogger::PaintedEvent EventLogger::create_undo_event(const std::string& timestamp, const std::string& username) {
    EventLogger::PaintedEvent event;
    event.timestamp = timestamp;
    event.username = username;
    event.type  = "undo_paint";
    return event;
}

// 创建空日志文件
bool EventLogger::create_blank_log(const std::string& filename) {
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

// 从日志文件重放事件恢复画布状态，返回行数
int EventLogger::replay_canvas_state(const std::string& filename, CanvasRoom* room_ptr) {
    std::string file_path = LOG_PATH + filename + LOG_EXTENSION;
    std::ifstream log_file(file_path);
    if (!log_file.is_open()) {
        std::cerr << "Failed to open log file for replay: " << file_path << std::endl;
        return -1;
    }
    std::string line;
    int line_count = 0;
    while (std::getline(log_file, line)) {
        // 这里假设每行都是一个 JSON 字符串，表示一个 PaintedEvent
        auto x = crow::json::load(line);
        if (!x) {
            std::cerr << "Failed to parse log line: " << line << std::endl;
            continue;
        }
        EventLogger::PaintedEvent event;
        event.type = x["type"].s();
        event.timestamp = x["timestamp"].s();
        event.username = x["username"].s();
        event.index = x["index"].i();
        event.color = x["color"].s();

        crow::json::wvalue broadcast_data_trash; // 这个参数在replay过程中不需要用到，可以传一个空的wvalue对象

        // 应用这个事件到画布状态
        if (event.type == "pixel_paint") {
            room_ptr->room_mtx.lock();
            Painter::pixel_paint(room_ptr, event.index, event.color);
            room_ptr->room_mtx.unlock();
        } else if (event.type == "square_paint") {
            event.size = x["size"].i();
            room_ptr->room_mtx.lock();
            Painter::square_paint(room_ptr, event.index, event.size, event.color);
            room_ptr->room_mtx.unlock();
        } else if (event.type == "undo_paint") {
            room_ptr->room_mtx.lock();
            Painter::undo_paint(room_ptr, broadcast_data_trash);
            room_ptr->room_mtx.unlock();
        } else {
            std::cerr << "Unknown event type in log: " << event.type << std::endl;
        }
        line_count++;
    }
    log_file.close();
    return line_count;
}

// 将事件追加到日志文件
void EventLogger::append_event_to_log(const std::string& filename, const PaintedEvent& event) {
    std::string file_path = LOG_PATH + filename + LOG_EXTENSION;
    file_mtx.lock();
    std::ofstream log_file(file_path, std::ios::app);
    if (!log_file.is_open()) {
        std::cerr << "Failed to open log file for appending: " << file_path << std::endl;
        file_mtx.unlock();
        return;
    }
    crow::json::wvalue event_json;
    event_json["timestamp"] = event.timestamp;
    event_json["type"] = event.type;
    event_json["username"] = event.username;
    event_json["index"] = event.index;
    event_json["color"] = event.color;
    if (event.type == "square_paint") {
        event_json["size"] = event.size;
    }
    log_file << event_json.dump() << std::endl;
    log_file.close();
    file_mtx.unlock();
}

// 清空log文件内容
void EventLogger::clear_log_file(const std::string& filename) {
    std::string file_path = LOG_PATH + filename + LOG_EXTENSION;
    file_mtx.lock();
    std::ofstream log_file(file_path, std::ios::trunc);
    if (!log_file.is_open()) {
        std::cerr << "Failed to open log file for clearing: " << file_path << std::endl;
        file_mtx.unlock();
        return;
    }
    log_file.close();
    file_mtx.unlock();
}

// 将日志文件内容转移到新文件（比如canvas文件），并清空日志文件内容
void EventLogger::transfer_log_to_canvas(const std::string& filename, CanvasRoom* room_ptr) {
    std::string canvas_path = CANVAS_PATH + filename;
    file_mtx.lock();
    std::ofstream canvas_file(canvas_path, std::ios::binary);
    if (!canvas_file.is_open()) {
        std::cerr << "Failed to open canvas file for writing: " << canvas_path << std::endl;
        file_mtx.unlock();
        return;
    }
    room_ptr->room_mtx.lock();
    for (const auto& color : room_ptr->canvas) {
        canvas_file << color << std::endl;
    }
    room_ptr->room_mtx.unlock();
    canvas_file.close();
    file_mtx.unlock();
    EventLogger::clear_log_file(filename);
}
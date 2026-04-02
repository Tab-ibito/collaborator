#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <iostream>
#include <string>
#include "crow.h"
#include "crow/middlewares/cors.h"
#include "../include/canvas_room.h"
#include "../include/file_paths.h"
#include "../include/painter.h"

struct CanvasRoom; 
namespace EventLogger {

    struct PaintedEvent {
        std::string type;
        std::string timestamp;
        std::string username;
        int index;
        std::string color;
        int size;
    };

    PaintedEvent create_pixel_painted_event(const std::string& timestamp, const std::string& username, int index, const std::string& color); // 创建像素绘制事件
    
    PaintedEvent create_square_painted_event(const std::string& timestamp, const std::string& username, int index, int size, const std::string& color); // 创建方块绘制事件

    PaintedEvent create_undo_event(const std::string& timestamp, const std::string& username); // 创建撤销事件

    bool create_blank_log(const std::string& filename); // 创建空日志文件
    
    int replay_canvas_state(const std::string& filename, CanvasRoom* room_ptr); // 从日志文件重放事件恢复画布状态，返回行数
    
    void append_event_to_log(const std::string& filename, const PaintedEvent& event); // 将事件追加到日志文件

    void clear_log_file(const std::string& filename); // 清空日志文件内容
    
    void transfer_log_to_canvas(const std::string& filename, CanvasRoom* room_ptr); // 转移日志文件内容到新文件

} // namespace EventLogger
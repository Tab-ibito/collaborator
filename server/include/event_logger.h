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

extern std::mutex file_mtx; //给文件操作上锁，保护文件读写的互斥

struct CanvasRoom; 
namespace EventLogger {
    crow::json::wvalue create_user_joined_event(const std::string& timestamp, const std::string& username); // 创建用户加入事件

    crow::json::wvalue create_user_left_event(const std::string& timestamp, const std::string& username); // 创建用户离开事件

    crow::json::wvalue create_canvas_incoming_event(int width, int height); // 创建画布进入事件

    std::vector<uint8_t> create_binary_canvas_data(const CanvasRoom* room_ptr); // 创建二进制画布数据

    crow::json::wvalue create_pixel_painted_event(const std::string& timestamp, const std::string& username, const std::string& filename, int index, const std::string& color); // 创建像素绘制事件
    
    crow::json::wvalue create_square_painted_event(const std::string& timestamp, const std::string& username, const std::string& filename, int index, int size, const std::string& color); // 创建方块绘制事件

    crow::json::wvalue create_line_painted_event(const std::string& timestamp, const std::string& username, const std::string& filename, int start_index, int end_index, const std::string& color); // 创建线条绘制事件

    crow::json::wvalue create_undo_event(const std::string& timestamp, const std::string& username, const std::string& filename, const CanvasRoom* room_ptr); // 创建多像素绘制事件

    bool create_blank_log(const std::string& filename); // 创建空日志文件
    
    void replay_canvas_state(const std::string& filename, CanvasRoom* room_ptr); // 从日志文件重放事件恢复画布状态，返回行数
    
    void append_event_to_log(const std::string& filename, const crow::json::wvalue& event); // 将事件追加到日志文件

    void clear_log_file(const std::string& filename); // 清空日志文件内容
    
    void transfer_log_to_canvas(const std::string& filename, const std::vector<std::string>& canvas); // 转移日志文件内容到新文件

} // namespace EventLogger
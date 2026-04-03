// include/canvas_room.h
#pragma once

#include "../include/file_paths.h"
#include <vector>
#include <string>
#include <unordered_set>
#include <mutex>
#include <deque>
#include "crow.h"

constexpr int DEFAULT_CANVAS_SIZE = 256;
constexpr int DEFAULT_ROW = 16;
constexpr int DEFAULT_COL = 16;
constexpr int MAX_LOG_LINES = 500;
constexpr int MAX_EDIT_HISTORY = 10;

struct PixelChange {
    int index;
    std::string color;
};

struct Action {
    std::vector<PixelChange> changes;
};

class CanvasRoom {
  public:
    std::vector<std::string> canvas;
    std::unordered_set<crow::websocket::connection *> connections;
    std::mutex room_mtx;
    std::deque<Action> edit_history;

    CanvasRoom() : canvas(DEFAULT_CANVAS_SIZE, "#FFFFFF"), canvas_width(DEFAULT_COL), created_at(""), canvas_height(DEFAULT_ROW), canvas_size(DEFAULT_CANVAS_SIZE) {}
    CanvasRoom(int width, int height, const std::string& created_at) : canvas(width * height, "#FFFFFF"), canvas_width(width), canvas_height(height), created_at(created_at), canvas_size(width * height) {}

    int get_size() const {
        return canvas_size;
    }
    int get_width() const {
        return canvas_width;
    }
    int get_height() const {
        return canvas_height;
    }
    std::string get_created_at() const {
        return created_at;
    }

    bool add_log_line() {
      this->log_line_count++;
      if (this->log_line_count > MAX_LOG_LINES) {
        this->log_line_count = 0;
        return true; // 需要切分日志文件
      } else {
        return false; // 不需要切分日志文件
      }
    }

  private:
    int log_line_count = 0;
    int canvas_width;
    int canvas_height;
    int canvas_size;
    std::string created_at;
};
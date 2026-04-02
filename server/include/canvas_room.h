// include/canvas_room.h
#pragma once

#include "../include/file_paths.h"
#include <vector>
#include <string>
#include <unordered_set>
#include <mutex>
#include <deque>
#include "crow.h"

constexpr int CANVAS_SIZE = 256;
constexpr int ROW = 16;
constexpr int COL = 16;
constexpr int MAX_LOG_LINES = 500;
constexpr int MAX_EDIT_HISTORY = 10;

struct PixelChange {
    int index;
    std::string color;
};

struct Action {
    std::vector<PixelChange> changes;
};

struct CanvasRoom {
  std::vector<std::string> canvas;
  std::unordered_set<crow::websocket::connection *> connections;
  std::mutex room_mtx;
  std::deque<Action> edit_history;
  int log_line_count = 0;

  CanvasRoom() : canvas(CANVAS_SIZE, "#FFFFFF") {} 

  bool add_log_line() {
    log_line_count++;
    if (log_line_count > MAX_LOG_LINES) {
      log_line_count = 0;
      return true; // 需要切分日志文件
    } else {
      return false; // 不需要切分日志文件
    }
  }
};
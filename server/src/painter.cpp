# include "../include/canvas_room.h"
# include "../include/event_logger.h"
# include "../include/painter.h"

void Painter::set_binary_color(const std::string& color, CanvasRoom* room_ptr, int index) {
    uint8_t r = 255, g = 255, b = 255;
    try {
        if (color.size() == 7) { 
            r = std::stoi(color.substr(1, 2), nullptr, 16);
            g = std::stoi(color.substr(3, 2), nullptr, 16);
            b = std::stoi(color.substr(5, 2), nullptr, 16);
        } 
        else if (color.size() == 4) { 
            std::string r_str = color.substr(1, 1); r_str += r_str;
            std::string g_str = color.substr(2, 1); g_str += g_str;
            std::string b_str = color.substr(3, 1); b_str += b_str;
            
            r = std::stoi(r_str, nullptr, 16);
            g = std::stoi(g_str, nullptr, 16);
            b = std::stoi(b_str, nullptr, 16);
        } else {
            std::cerr << "Invalid color format: " << color << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse color: " << color << ", " << e.what() << std::endl;
    }
    room_ptr->binary_canvas[index*3] = r;
    room_ptr->binary_canvas[index*3 + 1] = g;
    room_ptr->binary_canvas[index*3 + 2] = b;
}

std::string Painter::get_string_color(const int index, const CanvasRoom* room_ptr) {
    const auto& color = room_ptr->binary_canvas.data() + index * 3;
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", color[0], color[1], color[2]);
    return std::string(buffer);
}

std::vector<int> Painter::get_square_indices(int index, int size, int width, int height) {
    std::vector<int> indices;
    int row = index / width;
    int col = index % width;
    int offset = size / 2;
    row -= offset;
    col -= offset;
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            int current_row = row + i;
            int current_col = col + j;
            if (0 <= current_row && current_row < height && 0 <= current_col && current_col < width) {
                indices.push_back(current_row * width + current_col);
            }
        }
    }
    return indices;
}

// 绘制单个像素（没上锁）
void Painter::pixel_paint(CanvasRoom* room_ptr, int index, const std::string& color) {
    Action action;
    const uint8_t r = room_ptr->binary_canvas[index*3];
    const uint8_t g = room_ptr->binary_canvas[index*3 + 1];
    const uint8_t b = room_ptr->binary_canvas[index*3 + 2];
    action.changes.push_back({index, Painter::get_string_color(index, room_ptr)}); // 记录共3个字节切片
    room_ptr->edit_history.push_back(action); // 记录编辑历史
    if (room_ptr->edit_history.size() > MAX_EDIT_HISTORY) {
        room_ptr->edit_history.pop_front();
    }
    Painter::set_binary_color(color, room_ptr, index);
}

// 绘制方块（没上锁）
void Painter::square_paint(CanvasRoom* room_ptr, int index, int size, const std::string& color) {
    auto indices = Painter::get_square_indices(index, size, room_ptr->get_width(), room_ptr->get_height());
    Action action;
    for (int current_index : indices) {
        action.changes.push_back({current_index, Painter::get_string_color(current_index, room_ptr)}); // 记录共3个字节切片
        Painter::set_binary_color(color, room_ptr, current_index);
    }
    room_ptr->edit_history.push_back(action); // 记录编辑历史
    if (room_ptr->edit_history.size() > MAX_EDIT_HISTORY) {
        room_ptr->edit_history.pop_front();
    }
}

// 绘制线条（没有上锁）
void Painter::line_paint(CanvasRoom* room_ptr, int start_index, int end_index
, const std::string& color) {
    int width = room_ptr->get_width();
    int height = room_ptr->get_height();
    int x1 = start_index % width;
    int y1 = start_index / width;
    int x2 = end_index % width;
    int y2 = end_index / width;

    Action action;

    int dx = std::abs(x2 - x1);
    int dy = std::abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        int current_index = y1 * width + x1;
        action.changes.push_back({current_index, Painter::get_string_color(current_index, room_ptr)}); // 记录共3个字节切片
        Painter::set_binary_color(color, room_ptr, current_index);

        if (x1 == x2 && y1 == y2) break;
        int err2 = err * 2;
        if (err2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (err2 < dx) {
            err += dx;
            y1 += sy;
        }
    }

    room_ptr->edit_history.push_back(action); // 记录编辑历史
    if (room_ptr->edit_history.size() > MAX_EDIT_HISTORY) {
        room_ptr->edit_history.pop_front();
    }
}

// 画圆操作（没有上锁）
void Painter::circle_paint(CanvasRoom* room_ptr, int center_index, int radius,
const std::string& color) {
    int width = room_ptr->get_width();
    int height = room_ptr->get_height();
    int cx = center_index % width;
    int cy = center_index / width;

    Action action;

    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y) {
        std::vector<std::pair<int, int>> points = {
            {cx + x, cy + y}, {cx + y, cy + x}, {cx - y, cy + x}, {cx - x, cy + y},
            {cx - x, cy - y}, {cx - y, cy - x}, {cx + y, cy - x}, {cx + x, cy - y}
        };
        for (const auto& [px, py] : points) {
            if (0 <= px && px < width && 0 <= py && py < height) {
                int current_index = py * width + px;
                action.changes.push_back({current_index, Painter::get_string_color(current_index, room_ptr)}); // 记录共3个字节切片
                Painter::set_binary_color(color, room_ptr, current_index);
            }
        }
        if (err <= 0) {
            y += 1;
            err += 2*y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }

    room_ptr->edit_history.push_back(action); // 记录编辑历史
    if (room_ptr->edit_history.size() > MAX_EDIT_HISTORY) {
        room_ptr->edit_history.pop_front();
    }
}

// 撤销操作（没有上锁）
bool Painter::undo_paint(CanvasRoom* room_ptr) {
    if (!room_ptr->edit_history.empty()) {
        auto& last_changes = room_ptr->edit_history.back();
        for (const auto& change : last_changes.changes) {
            set_binary_color(change.color, room_ptr, change.index);
        }
        return true; // 成功撤销
    }
    return false; // 没有可撤销的操作
}

// 多像素操作（没有上锁）
void Painter::multipixel_paint(crow::json::rvalue indices, crow::json::rvalue colors, CanvasRoom* room_ptr) {
    Action action;
    std::vector<int> idx_list;
    std::vector<std::string> color_list;
    for (const auto& index : indices) {
        idx_list.push_back(index.i());
    }
    for (const auto& color : colors) {
        color_list.push_back(color.s());
    }
    for (size_t i = 0; i < idx_list.size(); ++i) {
        int index = idx_list[i];
        std::string color = color_list[i];
        action.changes.push_back({index, Painter::get_string_color(index, room_ptr)}); // 记录共3个字节切片
        Painter::set_binary_color(color, room_ptr, index);
    }

    room_ptr->edit_history.push_back(action); // 记录编辑历史
    if (room_ptr->edit_history.size() > MAX_EDIT_HISTORY) {
        room_ptr->edit_history.pop_front();
    }
}
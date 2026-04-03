# include "../include/canvas_room.h"
# include "../include/event_logger.h"
# include "../include/painter.h"

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
    action.changes.push_back({index, room_ptr->canvas[index]});
    room_ptr->edit_history.push_back(action); // 记录编辑历史
    if (room_ptr->edit_history.size() > MAX_EDIT_HISTORY) {
        room_ptr->edit_history.pop_front();
    }
    room_ptr->canvas[index] = color;
}

// 绘制方块（没上锁）
void Painter::square_paint(CanvasRoom* room_ptr, int index, int size, const std::string& color) {
    auto indices = Painter::get_square_indices(index, size, room_ptr->get_width(), room_ptr->get_height());
    Action action;
    for (int current_index : indices) {
        action.changes.push_back({current_index, room_ptr->canvas[current_index]});
        room_ptr->canvas[current_index] = color;
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
            room_ptr->canvas[change.index] = change.color;
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
        action.changes.push_back({index, room_ptr->canvas[index]});
        room_ptr->canvas[index] = color;
    }

    room_ptr->edit_history.push_back(action); // 记录编辑历史
    if (room_ptr->edit_history.size() > MAX_EDIT_HISTORY) {
        room_ptr->edit_history.pop_front();
    }
}
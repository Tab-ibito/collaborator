# include "../include/canvas_room.h"
# include "../include/event_logger.h"
# include "../include/painter.h"

std::vector<int> Painter::get_square_indices(int index, int size) {
    std::vector<int> indices;
    int row = index / COL;
    int col = index % ROW;
    int offset = size / 2;
    row -= offset;
    col -= offset;
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            int current_row = row + i;
            int current_col = col + j;
            if (0 <= current_row && current_row < ROW && 0 <= current_col && current_col < COL) {
                indices.push_back(current_row * COL + current_col);
            }
        }
    }
    return indices;
}

// 绘制单个像素（没上锁）
void Painter::pixel_paint(CanvasRoom* room_ptr, int index, const std::string& color) {
    Action action;
    action.type = "pixel_paint";
    action.changes.push_back({index, room_ptr->canvas[index]});
    room_ptr->edit_history.push_back(action); // 记录编辑历史
    if (room_ptr->edit_history.size() > MAX_EDIT_HISTORY) {
        room_ptr->edit_history.pop_front();
    }
    room_ptr->canvas[index] = color;
}

// 绘制方块（没上锁）
void Painter::square_paint(CanvasRoom* room_ptr, int index, int size, const std::string& color) {
    auto indices = Painter::get_square_indices(index, size);
    Action action;
    action.type = "square_paint";
    for (int current_index : indices) {
        action.changes.push_back({current_index, room_ptr->canvas[current_index]});
        room_ptr->canvas[current_index] = color;
        std::cout << "Painting index " << current_index << " with color " << color << std::endl;
    }
    room_ptr->edit_history.push_back(action); // 记录编辑历史
    if (room_ptr->edit_history.size() > MAX_EDIT_HISTORY) {
        room_ptr->edit_history.pop_front();
    }
}

// 撤销操作（没有上锁）
bool Painter::undo_paint(CanvasRoom* room_ptr, crow::json::wvalue& broadcast_data) {
    std::cout << room_ptr->edit_history.size() << " actions in history before undo." << std::endl;
    if (!room_ptr->edit_history.empty()) {
        auto last_changes = room_ptr->edit_history.back();
        std::vector<int> indices;
        std::vector<std::string> colors;
        for (const auto& change : last_changes.changes) {
            room_ptr->canvas[change.index] = change.color;
            indices.push_back(change.index);
            colors.push_back(change.color);
        }
        room_ptr->edit_history.pop_back();
        broadcast_data["index"] = indices;
        broadcast_data["color"] = colors;
        return true; // 成功撤销
    }
    return false; // 没有可撤销的操作
}
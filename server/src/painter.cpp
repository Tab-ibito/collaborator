# include "../include/canvas_room.h"
# include "../include/event_logger.h"
# include "../include/painter.h"

std::vector<int> get_square_indices(int index, int size) {
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
            if (current_row < ROW && current_col < COL) {
                indices.push_back(current_row * COL + current_col);
            }
        }
    }
    return indices;
}

void square_paint(CanvasRoom* room_ptr, int index, int size, const std::string& color) {
    room_ptr->room_mtx.lock();
    auto indices = get_square_indices(index, size);
    for (int current_index : indices) {
        room_ptr->canvas[current_index] = color;
        room_ptr->edit_history.push_back({current_index, room_ptr->canvas[current_index]}); // 记录编辑历史
        if (room_ptr->edit_history.size() > MAX_EDIT_HISTORY) {
            room_ptr->edit_history.pop_front();
        }
    }
    room_ptr->room_mtx.unlock();
}


bool undo_paint(CanvasRoom* room_ptr) {
    room_ptr->room_mtx.lock();
    if (!room_ptr->edit_history.empty()) {
        auto last_changes = room_ptr->edit_history.back();
        for (const auto& change : last_changes) {
            room_ptr->canvas[change.index] = change.color;
        }
        room_ptr->edit_history.pop_back();
        room_ptr->room_mtx.unlock();
        return true; // 成功撤销
    }
    room_ptr->room_mtx.unlock();
    return false; // 没有可撤销的操作
}
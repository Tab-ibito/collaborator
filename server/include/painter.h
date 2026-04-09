# pragma once

# include "../include/canvas_room.h"
# include "../include/event_logger.h"
# include "../include/file_paths.h"
# include <iostream>
# include <vector>
# include <string>

namespace Painter {
    void set_binary_color(const std::string& color, CanvasRoom* room_ptr, int index);

    std::string get_string_color(const int index, const CanvasRoom* room_ptr);

    std::vector<int> get_square_indices(int index, int size, int width, int height);

    void pixel_paint(CanvasRoom* room_ptr, int index, const std::string& color);

    void square_paint(CanvasRoom* room_ptr, int index, int size, const std::string& color);
    
    void line_paint(CanvasRoom* room_ptr, int start_index, int end_index, const std::string& color);

    void circle_paint(CanvasRoom* room_ptr, int center_index, int radius, const std::string& color);

    bool undo_paint(CanvasRoom* room_ptr);

    void multipixel_paint(crow::json::rvalue indices, crow::json::rvalue colors, CanvasRoom* room_ptr);
}

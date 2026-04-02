# include "../include/canvas_room.h"
# include "../include/event_logger.h"
# include "../include/file_paths.h"
# include <iostream>
# include <vector>
# include <string>

namespace Painter {
    std::vector<int> get_square_indices(int index, int size);

    void pixel_paint(CanvasRoom* room_ptr, int index, const std::string& color);

    void square_paint(CanvasRoom* room_ptr, int index, int size, const std::string& color);
    
    bool undo_paint(CanvasRoom* room_ptr, crow::json::wvalue& broadcast_data);
}
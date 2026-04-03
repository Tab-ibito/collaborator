# pragma once
# include <sqlite3.h>
# include <string>

class DBManager {
public:
    DBManager(const std::string& db_path);
    ~DBManager();

    // 初始化数据库表
    bool init_users_table();
    bool init_canvas_metadata_table();

    // 操作users数据库表
    bool add_user(const std::string& username, const std::string& password);
    bool verify_user(const std::string& username, const std::string& password);
    bool find_user(const std::string& username);

    // 操作canvas_metadata数据库表
    bool create_canvas_metadata(const std::string& filename, const int width, const int height, const std::string& created_at);
    bool get_canvas_metadata(const std::string& filename, int& width, int& height, std::string& created_at);

    // 用于测试    
    bool reset_tables();
    bool print_tables();

private:
    sqlite3* db;
};
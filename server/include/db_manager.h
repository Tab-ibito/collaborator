# pragma once
# include <sqlite3.h>
# include <string>

class DBManager {
public:
    DBManager(const std::string& db_path);
    ~DBManager();

    // 初始化数据库表
    bool init_tables();

    // 操作数据库表
    bool add_user(const std::string& username, const std::string& password);
    bool verify_user(const std::string& username, const std::string& password);

    // 用于测试    
    bool reset_tables();
    bool print_tables();

private:
    sqlite3* db;
};
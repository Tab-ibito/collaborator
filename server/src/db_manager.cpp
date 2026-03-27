# include "../include/db_manager.h"
# include <iostream>

// 构造函数，打开数据库连接
DBManager::DBManager(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
        db = nullptr;
    } else {
        std::cout << "Database opened successfully." << std::endl;
    }
}

// 析构函数，关闭数据库连接
DBManager::~DBManager() {
    if (db) {
        sqlite3_close(db);
        std::cout << "Database closed successfully." << std::endl;
    }
}

// 初始化数据库表
bool DBManager::init_tables() {
    const char* sql = R"(CREATE TABLE IF NOT EXISTS users (
                      id INTEGER PRIMARY KEY AUTOINCREMENT,
                      username TEXT UNIQUE NOT NULL,
                      password TEXT NOT NULL);)";
    char* err_msg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "Failed to create tables: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return false;
    }

    std::cout << "Tables initialized successfully." << std::endl;
    return true;
}

// 添加用户
bool DBManager::add_user(const std::string& username, const std::string& password) {
    const char* sql = "INSERT INTO users (username, password) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {    
        std::cerr << "Failed to execute statement: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);
    std::cout << "User added successfully." << std::endl;
    return true;
}

// 认证用户
bool DBManager::verify_user(const std::string& username, const std::string& password) {
    const char* sql = "SELECT password FROM users WHERE username = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    bool verified = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* stored_password = sqlite3_column_text(stmt, 0);
        if (stored_password && password == reinterpret_cast<const char*>(stored_password)) {
            verified = true;
        }
    } else {
        std::cerr << "User not found: " << username << std::endl;
    }
    sqlite3_finalize(stmt);
    if (verified) {
        std::cout << "User verified successfully." << std::endl;
    } else {
        std::cout << "User verification failed." << std::endl;
    }
    return verified;
}

// 查找用户是否存在
bool DBManager::find_user(const std::string& username) {
    const char* sql = "SELECT password FROM users WHERE username = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        return true;
    }
    return false;
}

// 用于测试，重置数据库表
bool DBManager::reset_tables() {
    const char* sql = R"(DROP TABLE IF EXISTS users;
                      CREATE TABLE IF NOT EXISTS users (
                      id INTEGER PRIMARY KEY AUTOINCREMENT,
                      username TEXT UNIQUE NOT NULL,
                      password TEXT NOT NULL);)";
    
    char* err_msg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "Failed to reset tables: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return false;
    }

    std::cout << "Tables reset successfully." << std::endl;
    return true;
}

// 用于测试，打印数据库表内容
bool DBManager::print_tables() {
    const char* sql = "SELECT id, username, password FROM users;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    std::cout << "Current users in database:" << std::endl;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char* username = sqlite3_column_text(stmt, 1);
        const unsigned char* password = sqlite3_column_text(stmt, 2);
        std::cout << "ID: " << id << ", Username: " << username << ", Password: " << password << std::endl;
    }
    sqlite3_finalize(stmt);
    return true;
}
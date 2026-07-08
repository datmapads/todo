#define _WIN32_WINNT 0x0A00
#include "httplib.h"
#include "json.hpp"
#include "dotenv.h"
#include "sha256.h"
#include <mysql.h>
#include <windows.h>
#include <string>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <random>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

// ================== PHIEN BAN 2.1 - MUC TIEU CONG VIEC ==================
// Ke thua toan bo chuc nang dang nhap + CRUD cong viec cua Phien ban 2.0, bo sung:
//   - Muc tieu ca nhan (goals): danh sach tach biet voi cong viec hang ngay,
//     de nguoi dung dat cac muc tieu dai han (vd "Hoc xong khoa C++ trong thang nay")
//   - Sua loi hien thi nho: nhan trang thai rong cho ca muc tieu va cong viec
// =========================================================================

static std::string DB_HOST;
static std::string DB_USER;
static std::string DB_PASS;
static std::string DB_NAME;
static unsigned int DB_PORT;

void LoadDbConfig()
{
    bool loaded = dotenv::load(".env");
    if (!loaded)
    {
        std::cerr << "[LoadDbConfig] Khong tim thay file .env, se dung gia tri mac dinh.\n"
                     "    Hay tao file .env dua tren .env.example de cau hinh CSDL.\n";
    }

    DB_HOST = dotenv::get("DB_HOST", "127.0.0.1");
    DB_USER = dotenv::get("DB_USER", "root");
    DB_PASS = dotenv::get("DB_PASS", "");
    DB_NAME = dotenv::get("DB_NAME", "todo_db_v21");
    DB_PORT = static_cast<unsigned int>(std::stoul(dotenv::get("DB_PORT", "3306")));
}

// ---------- Dinh nghia con tro ham MySQL (nap dong tu libmysql.dll) ----------
typedef MYSQL *(__stdcall *mysql_init_t)(MYSQL *);
typedef MYSQL *(__stdcall *mysql_real_connect_t)(MYSQL *, const char *, const char *, const char *, const char *, unsigned int, const char *, unsigned long);
typedef void(__stdcall *mysql_close_t)(MYSQL *);
typedef const char *(__stdcall *mysql_error_t)(MYSQL *);
typedef int(__stdcall *mysql_query_t)(MYSQL *, const char *);
typedef MYSQL_RES *(__stdcall *mysql_store_result_t)(MYSQL *);
typedef MYSQL_ROW(__stdcall *mysql_fetch_row_t)(MYSQL_RES *);
typedef void(__stdcall *mysql_free_result_t)(MYSQL_RES *);
typedef int(__stdcall *mysql_options_t)(MYSQL *, enum mysql_option, const void *);
typedef int(__stdcall *mysql_ssl_set_t)(MYSQL *, const char *, const char *, const char *, const char *, const char *);
typedef unsigned long(__stdcall *mysql_real_escape_string_t)(MYSQL *, char *, const char *, unsigned long);
typedef my_ulonglong(__stdcall *mysql_affected_rows_t)(MYSQL *);

mysql_init_t p_mysql_init = nullptr;
mysql_real_connect_t p_mysql_real_connect = nullptr;
mysql_close_t p_mysql_close = nullptr;
mysql_error_t p_mysql_error = nullptr;
mysql_query_t p_mysql_query = nullptr;
mysql_store_result_t p_mysql_store_result = nullptr;
mysql_fetch_row_t p_mysql_fetch_row = nullptr;
mysql_free_result_t p_mysql_free_result = nullptr;
mysql_options_t p_mysql_options = nullptr;
mysql_ssl_set_t p_mysql_ssl_set = nullptr;
mysql_real_escape_string_t p_mysql_real_escape_string = nullptr;
mysql_affected_rows_t p_mysql_affected_rows = nullptr;

bool LoadMySQL()
{
    static bool loaded = false;
    if (loaded)
        return true;

    HMODULE h = LoadLibraryA("libmysql.dll");
    if (!h)
    {
        std::cerr << "[LoadMySQL] Khong tim thay libmysql.dll (phai dat cung thu muc voi file .exe)" << std::endl;
        return false;
    }
    p_mysql_init = (mysql_init_t)GetProcAddress(h, "mysql_init");
    p_mysql_real_connect = (mysql_real_connect_t)GetProcAddress(h, "mysql_real_connect");
    p_mysql_close = (mysql_close_t)GetProcAddress(h, "mysql_close");
    p_mysql_error = (mysql_error_t)GetProcAddress(h, "mysql_error");
    p_mysql_query = (mysql_query_t)GetProcAddress(h, "mysql_query");
    p_mysql_store_result = (mysql_store_result_t)GetProcAddress(h, "mysql_store_result");
    p_mysql_fetch_row = (mysql_fetch_row_t)GetProcAddress(h, "mysql_fetch_row");
    p_mysql_free_result = (mysql_free_result_t)GetProcAddress(h, "mysql_free_result");
    p_mysql_options = (mysql_options_t)GetProcAddress(h, "mysql_options");
    p_mysql_ssl_set = (mysql_ssl_set_t)GetProcAddress(h, "mysql_ssl_set");
    p_mysql_real_escape_string = (mysql_real_escape_string_t)GetProcAddress(h, "mysql_real_escape_string");
    p_mysql_affected_rows = (mysql_affected_rows_t)GetProcAddress(h, "mysql_affected_rows");

    if (!p_mysql_init || !p_mysql_real_connect || !p_mysql_close || !p_mysql_error ||
        !p_mysql_query || !p_mysql_store_result || !p_mysql_fetch_row || !p_mysql_free_result ||
        !p_mysql_real_escape_string || !p_mysql_affected_rows)
    {
        std::cerr << "[LoadMySQL] GetProcAddress that bai cho mot so ham can thiet" << std::endl;
        return false;
    }
    loaded = true;
    return true;
}

MYSQL *ConnectDB()
{
    if (!LoadMySQL())
        return nullptr;

    MYSQL *conn = p_mysql_init(nullptr);
    if (!conn)
    {
        std::cerr << "[ConnectDB] mysql_init that bai" << std::endl;
        return nullptr;
    }

    if (p_mysql_options)
    {
        int mode = SSL_MODE_DISABLED;
        p_mysql_options(conn, MYSQL_OPT_SSL_MODE, (const char *)&mode);
    }
    else if (p_mysql_ssl_set)
    {
        p_mysql_ssl_set(conn, nullptr, nullptr, nullptr, nullptr, nullptr);
    }

    if (!p_mysql_real_connect(conn, DB_HOST.c_str(), DB_USER.c_str(), DB_PASS.c_str(), DB_NAME.c_str(), DB_PORT, nullptr, 0))
    {
        std::cerr << "[ConnectDB] Loi ket noi MySQL: " << p_mysql_error(conn) << std::endl;
        p_mysql_close(conn);
        return nullptr;
    }
    return conn;
}

std::string Escape(MYSQL *conn, const std::string &input)
{
    std::string out(input.size() * 2 + 1, '\0');
    unsigned long len = p_mysql_real_escape_string(conn, &out[0], input.c_str(), (unsigned long)input.size());
    out.resize(len);
    return out;
}

// =================== SESSION / AUTH TRONG BO NHO SERVER ===================
struct SessionData
{
    int userId;
    std::string username;
};

static std::mutex g_sessionMutex;
static std::unordered_map<std::string, SessionData> g_sessions;

std::string RandomHex(int numBytes)
{
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<int> byteDist(0, 255);
    std::ostringstream oss;
    for (int i = 0; i < numBytes; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << byteDist(gen);
    return oss.str();
}

std::string GetCookieValue(const httplib::Request &req, const std::string &name)
{
    std::string header = req.get_header_value("Cookie");
    size_t pos = 0;
    while (pos < header.size())
    {
        size_t semi = header.find(';', pos);
        std::string pair = header.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos)
        {
            std::string key = pair.substr(0, eq);
            size_t start = key.find_first_not_of(' ');
            if (start != std::string::npos)
                key = key.substr(start);
            else
                key = "";
            if (key == name)
                return pair.substr(eq + 1);
        }
        if (semi == std::string::npos)
            break;
        pos = semi + 1;
    }
    return "";
}

// Tra ve true neu tim thay phien hop le, dong thoi gan userId/username ra ngoai.
bool GetCurrentUser(const httplib::Request &req, int &userId, std::string &username)
{
    std::string token = GetCookieValue(req, "session_id");
    if (token.empty())
        return false;
    std::lock_guard<std::mutex> lock(g_sessionMutex);
    auto it = g_sessions.find(token);
    if (it == g_sessions.end())
        return false;
    userId = it->second.userId;
    username = it->second.username;
    return true;
}

void SendUnauthorized(httplib::Response &res)
{
    res.status = 401;
    res.set_content("{\"error\":\"Ban chua dang nhap\"}", "application/json");
}

// =================== TASKS (gan voi user_id) ===================
json GetTasks(int userId)
{
    json arr = json::array();
    MYSQL *conn = ConnectDB();
    if (!conn)
        return arr;

    std::string q = "SELECT id, name, category, priority, completed FROM tasks WHERE user_id = " +
                     std::to_string(userId) + " ORDER BY id DESC";
    if (p_mysql_query(conn, q.c_str()))
    {
        std::cerr << "[GetTasks] Query loi: " << p_mysql_error(conn) << std::endl;
        p_mysql_close(conn);
        return arr;
    }
    MYSQL_RES *res = p_mysql_store_result(conn);
    if (res)
    {
        MYSQL_ROW row;
        while ((row = p_mysql_fetch_row(res)))
        {
            json obj;
            obj["id"] = std::stoi(row[0]);
            obj["name"] = row[1] ? row[1] : "";
            obj["category"] = row[2] ? row[2] : "";
            obj["priority"] = row[3] ? row[3] : "";
            obj["completed"] = row[4] && std::string(row[4]) == "1";
            arr.push_back(obj);
        }
        p_mysql_free_result(res);
    }
    p_mysql_close(conn);
    return arr;
}

bool AddTask(int userId, const std::string &name, const std::string &category, const std::string &priority)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;

    std::string q = "INSERT INTO tasks (user_id, name, category, priority, completed) VALUES (" +
                     std::to_string(userId) + ", '" + Escape(conn, name) + "','" + Escape(conn, category) + "','" +
                     Escape(conn, priority) + "', 0)";
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[AddTask] Loi: " << p_mysql_error(conn) << std::endl;
    p_mysql_close(conn);
    return ret == 0;
}

bool ToggleTask(int userId, int id)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;
    std::string q = "UPDATE tasks SET completed = 1 - completed WHERE id = " + std::to_string(id) +
                     " AND user_id = " + std::to_string(userId);
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[ToggleTask] Loi: " << p_mysql_error(conn) << std::endl;
    // mysql_query() chi bao loi cu phap/ket noi - phai kiem tra them affected_rows
    // de biet task co ton tai va thuoc ve dung user hay khong.
    bool ok = ret == 0 && p_mysql_affected_rows(conn) > 0;
    p_mysql_close(conn);
    return ok;
}

bool DeleteTask(int userId, int id)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;
    std::string q = "DELETE FROM tasks WHERE id = " + std::to_string(id) +
                     " AND user_id = " + std::to_string(userId);
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[DeleteTask] Loi: " << p_mysql_error(conn) << std::endl;
    bool ok = ret == 0 && p_mysql_affected_rows(conn) > 0;
    p_mysql_close(conn);
    return ok;
}

// =================== MUC TIEU CONG VIEC (goals, gan voi user_id) ===================
json GetGoals(int userId)
{
    json arr = json::array();
    MYSQL *conn = ConnectDB();
    if (!conn)
        return arr;

    std::string q = "SELECT id, title, done FROM goals WHERE user_id = " +
                     std::to_string(userId) + " ORDER BY id DESC";
    if (p_mysql_query(conn, q.c_str()))
    {
        std::cerr << "[GetGoals] Query loi: " << p_mysql_error(conn) << std::endl;
        p_mysql_close(conn);
        return arr;
    }
    MYSQL_RES *res = p_mysql_store_result(conn);
    if (res)
    {
        MYSQL_ROW row;
        while ((row = p_mysql_fetch_row(res)))
        {
            json obj;
            obj["id"] = std::stoi(row[0]);
            obj["title"] = row[1] ? row[1] : "";
            obj["done"] = row[2] && std::string(row[2]) == "1";
            arr.push_back(obj);
        }
        p_mysql_free_result(res);
    }
    p_mysql_close(conn);
    return arr;
}

bool AddGoal(int userId, const std::string &title)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;

    std::string q = "INSERT INTO goals (user_id, title, done) VALUES (" +
                     std::to_string(userId) + ", '" + Escape(conn, title) + "', 0)";
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[AddGoal] Loi: " << p_mysql_error(conn) << std::endl;
    p_mysql_close(conn);
    return ret == 0;
}

bool ToggleGoal(int userId, int id)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;
    std::string q = "UPDATE goals SET done = 1 - done WHERE id = " + std::to_string(id) +
                     " AND user_id = " + std::to_string(userId);
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[ToggleGoal] Loi: " << p_mysql_error(conn) << std::endl;
    bool ok = ret == 0 && p_mysql_affected_rows(conn) > 0;
    p_mysql_close(conn);
    return ok;
}

bool DeleteGoal(int userId, int id)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;
    std::string q = "DELETE FROM goals WHERE id = " + std::to_string(id) +
                     " AND user_id = " + std::to_string(userId);
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[DeleteGoal] Loi: " << p_mysql_error(conn) << std::endl;
    bool ok = ret == 0 && p_mysql_affected_rows(conn) > 0;
    p_mysql_close(conn);
    return ok;
}

// =================== USERS (dang ky / dang nhap) ===================
// Tra ve: 0 = thanh cong, 1 = trung ten dang nhap, 2 = loi khac
int CreateUser(const std::string &username, const std::string &password)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return 2;

    std::string checkQ = "SELECT id FROM users WHERE username = '" + Escape(conn, username) + "'";
    if (p_mysql_query(conn, checkQ.c_str()) == 0)
    {
        MYSQL_RES *res = p_mysql_store_result(conn);
        if (res)
        {
            bool exists = p_mysql_fetch_row(res) != nullptr;
            p_mysql_free_result(res);
            if (exists)
            {
                p_mysql_close(conn);
                return 1;
            }
        }
    }

    std::string salt = RandomHex(8);
    std::string hash = Sha256Hex(salt + password);

    std::string q = "INSERT INTO users (username, password_hash, salt) VALUES ('" +
                     Escape(conn, username) + "', '" + hash + "', '" + salt + "')";
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[CreateUser] Loi: " << p_mysql_error(conn) << std::endl;
    p_mysql_close(conn);
    return ret == 0 ? 0 : 2;
}

// Tra ve userId neu dang nhap dung, -1 neu sai thong tin
int VerifyLogin(const std::string &username, const std::string &password)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return -1;

    std::string q = "SELECT id, password_hash, salt FROM users WHERE username = '" + Escape(conn, username) + "'";
    if (p_mysql_query(conn, q.c_str()))
    {
        p_mysql_close(conn);
        return -1;
    }
    MYSQL_RES *res = p_mysql_store_result(conn);
    int userId = -1;
    if (res)
    {
        MYSQL_ROW row = p_mysql_fetch_row(res);
        if (row && row[0] && row[1] && row[2])
        {
            std::string storedHash = row[1];
            std::string salt = row[2];
            if (Sha256Hex(salt + password) == storedHash)
                userId = std::stoi(row[0]);
        }
        p_mysql_free_result(res);
    }
    p_mysql_close(conn);
    return userId;
}

int main()
{
    LoadDbConfig();

    MYSQL *test = ConnectDB();
    if (test)
    {
        std::cout << "Ket noi MySQL thanh cong! (" << DB_HOST << ":" << DB_PORT << "/" << DB_NAME << ")" << std::endl;
        p_mysql_close(test);
    }
    else
    {
        std::cerr << "!!! KHONG ket noi duoc MySQL. Server van chay nhung cac API se loi.\n"
                     "    Kiem tra lai:\n"
                     "    1) MySQL server (XAMPP/Service) da BAT chua?\n"
                     "    2) Thong tin trong file .env co dung khong?\n"
                     "    3) Da chay schema.sql de tao database/bang chua?\n";
    }

    httplib::Server svr;
    svr.set_mount_point("/", "./");

    // ---------- AUTH API ----------
    svr.Post("/api/register", [](const httplib::Request &req, httplib::Response &res)
             {
        try {
            auto data = json::parse(req.body);
            std::string username = data.value("username", "");
            std::string password = data.value("password", "");
            if (username.size() < 3 || password.size() < 4) {
                res.status = 400;
                res.set_content("{\"error\":\"Ten dang nhap can >= 3 ky tu, mat khau >= 4 ky tu\"}", "application/json");
                return;
            }
            int ret = CreateUser(username, password);
            if (ret == 1) {
                res.status = 409;
                res.set_content("{\"error\":\"Ten dang nhap da ton tai\"}", "application/json");
            } else if (ret == 0) {
                res.status = 201;
                res.set_content("{\"status\":\"ok\"}", "application/json");
            } else {
                res.status = 500;
                res.set_content("{\"error\":\"Dang ky that bai\"}", "application/json");
            }
        } catch (const std::exception &e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        } });

    svr.Post("/api/login", [](const httplib::Request &req, httplib::Response &res)
             {
        try {
            auto data = json::parse(req.body);
            std::string username = data.value("username", "");
            std::string password = data.value("password", "");
            int userId = VerifyLogin(username, password);
            if (userId < 0) {
                res.status = 401;
                res.set_content("{\"error\":\"Sai ten dang nhap hoac mat khau\"}", "application/json");
                return;
            }
            std::string token = RandomHex(32);
            {
                std::lock_guard<std::mutex> lock(g_sessionMutex);
                g_sessions[token] = { userId, username };
            }
            res.set_header("Set-Cookie", "session_id=" + token + "; Path=/; HttpOnly; SameSite=Lax");
            json out;
            out["status"] = "ok";
            out["username"] = username;
            res.set_content(out.dump(), "application/json");
        } catch (const std::exception &e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        } });

    svr.Post("/api/logout", [](const httplib::Request &req, httplib::Response &res)
             {
        std::string token = GetCookieValue(req, "session_id");
        if (!token.empty()) {
            std::lock_guard<std::mutex> lock(g_sessionMutex);
            g_sessions.erase(token);
        }
        res.set_header("Set-Cookie", "session_id=deleted; Path=/; HttpOnly; Max-Age=0");
        res.set_content("{\"status\":\"ok\"}", "application/json"); });

    svr.Get("/api/me", [](const httplib::Request &req, httplib::Response &res)
            {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        json out; out["username"] = username;
        res.set_content(out.dump(), "application/json"); });

    // ---------- TASKS API (yeu cau dang nhap) ----------
    svr.Get("/tasks", [](const httplib::Request &req, httplib::Response &res)
            {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        res.set_content(GetTasks(userId).dump(), "application/json"); });

    svr.Post("/tasks", [](const httplib::Request &req, httplib::Response &res)
             {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            auto data = json::parse(req.body);
            std::string name = data.value("name", "");
            std::string category = data.value("category", "");
            std::string priority = data.value("priority", "");
            if (name.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"name khong duoc rong\"}", "application/json");
                return;
            }
            bool ok = AddTask(userId, name, category, priority);
            res.status = ok ? 201 : 500;
            res.set_content(ok ? "{\"status\":\"ok\"}" : "{\"error\":\"insert that bai\"}", "application/json");
        } catch (const std::exception &e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        } });

    svr.Put(R"(/tasks/(\d+))", [](const httplib::Request &req, httplib::Response &res)
            {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            int id = std::stoi(req.matches[1].str());
            bool ok = ToggleTask(userId, id);
            res.status = ok ? 200 : 500;
            res.set_content(ok ? "{\"status\":\"ok\"}" : "{\"error\":\"update that bai\"}", "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"id khong hop le\"}", "application/json");
        } });

    svr.Delete(R"(/tasks/(\d+))", [](const httplib::Request &req, httplib::Response &res)
               {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            int id = std::stoi(req.matches[1].str());
            bool ok = DeleteTask(userId, id);
            res.status = ok ? 200 : 500;
            res.set_content(ok ? "{\"status\":\"ok\"}" : "{\"error\":\"delete that bai\"}", "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"id khong hop le\"}", "application/json");
        } });

    // ---------- GOALS API (muc tieu ca nhan, yeu cau dang nhap) ----------
    svr.Get("/goals", [](const httplib::Request &req, httplib::Response &res)
            {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        res.set_content(GetGoals(userId).dump(), "application/json"); });

    svr.Post("/goals", [](const httplib::Request &req, httplib::Response &res)
             {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            auto data = json::parse(req.body);
            std::string title = data.value("title", "");
            if (title.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"title khong duoc rong\"}", "application/json");
                return;
            }
            bool ok = AddGoal(userId, title);
            res.status = ok ? 201 : 500;
            res.set_content(ok ? "{\"status\":\"ok\"}" : "{\"error\":\"insert that bai\"}", "application/json");
        } catch (const std::exception &e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        } });

    svr.Put(R"(/goals/(\d+))", [](const httplib::Request &req, httplib::Response &res)
            {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            int id = std::stoi(req.matches[1].str());
            bool ok = ToggleGoal(userId, id);
            res.status = ok ? 200 : 500;
            res.set_content(ok ? "{\"status\":\"ok\"}" : "{\"error\":\"update that bai\"}", "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"id khong hop le\"}", "application/json");
        } });

    svr.Delete(R"(/goals/(\d+))", [](const httplib::Request &req, httplib::Response &res)
               {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            int id = std::stoi(req.matches[1].str());
            bool ok = DeleteGoal(userId, id);
            res.status = ok ? 200 : 500;
            res.set_content(ok ? "{\"status\":\"ok\"}" : "{\"error\":\"delete that bai\"}", "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"id khong hop le\"}", "application/json");
        } });

    std::cout << "== PHIEN BAN 2.1 - MUC TIEU CONG VIEC ==\n";
    std::cout << "Server dang chay tai http://localhost:8080 (va http://127.0.0.1:8080)\n";
    svr.listen("0.0.0.0", 8080);
    return 0;
}

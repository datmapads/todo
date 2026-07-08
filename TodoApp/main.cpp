#define _WIN32_WINNT 0x0A00
#include "httplib.h"
#include "json.hpp"
#include "dotenv.h"
#include "mysql.h"
#include <windows.h>
#include <string>
#include <iostream>
#include "services/TaskService.h"
#include "MySQLWrapper.h"
using json = nlohmann::json;

// ================== CẤU HÌNH KẾT NỐI DATABASE ==================
// Các giá trị nhạy cảm (host/user/password/database/port) KHÔNG còn
// hard-code trong mã nguồn. Chúng được nạp từ file ".env" (không commit
// lên git, xem ".env.example" để biết định dạng) thông qua dotenv.h.
// Nếu thiếu ".env", giá trị mặc định bên dưới chỉ dùng để chạy thử cục bộ.
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
    DB_NAME = dotenv::get("DB_NAME", "todo_db");
    DB_PORT = static_cast<unsigned int>(std::stoul(dotenv::get("DB_PORT", "3306")));

}
// =================================================================

// ---------- Định nghĩa con trỏ hàm MySQL (nạp động từ libmysql.dll) ----------
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

    if (!p_mysql_init || !p_mysql_real_connect || !p_mysql_close || !p_mysql_error ||
        !p_mysql_query || !p_mysql_store_result || !p_mysql_fetch_row || !p_mysql_free_result ||
        !p_mysql_real_escape_string)
    {
        std::cerr << "[LoadMySQL] GetProcAddress that bai cho mot so ham can thiet" << std::endl;
        return false;
    }
    loaded = true;
    return true;
}

MYSQL* ConnectDB()
{
    if (!LoadMySQL())
        return nullptr;

    MYSQL* conn = p_mysql_init(nullptr);

    if (!conn)
    {
        std::cerr
            << "[ConnectDB] mysql_init that bai"
            << std::endl;

        return nullptr;
    }

    // Ket noi truc tiep, KHONG dung SSL
    if (!p_mysql_real_connect(
            conn,
            DB_HOST.c_str(),
            DB_USER.c_str(),
            DB_PASS.c_str(),
            DB_NAME.c_str(),
            DB_PORT,
            nullptr,
            CLIENT_MULTI_STATEMENTS
        ))
    {
        std::cerr
            << "[ConnectDB] Loi ket noi MySQL: "
            << p_mysql_error(conn)
            << std::endl;

        p_mysql_close(conn);

        return nullptr;
    }

    std::cout
        << "[ConnectDB] Ket noi MySQL thanh cong!"
        << std::endl;

    return conn;
}

std::string Escape(MYSQL *conn, const std::string &input)
{
    std::string out(input.size() * 2 + 1, '\0');
    unsigned long len = p_mysql_real_escape_string(conn, &out[0], input.c_str(), (unsigned long)input.size());
    out.resize(len);
    return out;
}


int main()
{
    TaskService taskService;
    LoadDbConfig();

    MYSQL *test = ConnectDB();
    if (test)
    {
        std::cout << "Ket noi MySQL thanh cong! (" << DB_HOST << ":" << DB_PORT << "/" << DB_NAME << ")" << std::endl;
        p_mysql_close(test);
    }
    else
    {
        std::cout << "[main] Loi ket noi MySQL: " << DB_HOST << ":" << DB_PORT << "/" << DB_NAME << " with user " << DB_USER << " and password " << DB_PASS << std::endl;
        std::cerr << "!!! KHONG ket noi duoc MySQL. Server van chay nhung cac API se loi.\n"
                     "    Kiem tra lai:\n"
                     "    1) MySQL server (XAMPP/Service) da BAT chua?\n"
                     "    2) User 'root' / pass 'Gasama060222@' co dung voi MySQL tren may nay khong?\n"
                     "    3) Database 'todo_db' va bang 'tasks' da duoc tao chua? (xem file schema.sql)\n";
    }

    httplib::Server svr;

    // Phục vụ file tĩnh (index.html, css, js...) nằm cùng thư mục với main.cpp / file .exe
    svr.set_mount_point("/", "./");

   svr.Get("/tasks",[&taskService]
                (const httplib::Request&,
                httplib::Response& res)
                {
                    res.set_content(
                        taskService
                            .getTasks()
                            .dump(),
                        "application/json"
                    );
                });

    svr.Post("/api/login",[](const httplib::Request& req,
        httplib::Response& res)

        {
            std::cout << "POST /tasks received" << std::endl;
            std::cout << req.body << std::endl;
            auto data = json::parse(req.body);

            std::string user =
                data.value("username","");

            std::string pass =
                data.value("password","");

            if(user == "admin" &&
            pass == "123456")
            {
                json result;
                result["username"] = user;

                res.set_content(
                    result.dump(),
                    "application/json"
                );
            }
            else
            {
                res.status = 401;

                res.set_content(
                    R"({"error":"Sai tài khoản hoặc mật khẩu"})",
                    "application/json"
                );
            }
            
        });
        
        svr.Post("/tasks",[&taskService]
    (const httplib::Request& req,
    httplib::Response& res)
    {
        try
        {
            auto data = json::parse(req.body);

            bool ok =
                taskService.addTask(
                    data.value("name",""),
                    data.value("category",""),
                    data.value("priority","")
                );

            if(ok)
            {
                res.status = 201;
                res.set_content(
                    R"({"status":"ok"})",
                    "application/json"
                );
            }
            else
            {
                res.status = 500;
                res.set_content(
                    R"({"error":"insert failed"})",
                    "application/json"
                );
            }
        }
        catch(...)
        {
            res.status = 400;
            res.set_content(
                R"({"error":"bad request"})",
                "application/json"
            );
        }
    });

        // Luu y: httplib 0.12.3 khong ho tro cu phap ":id" + path_params (chi co o ban moi hon).
        // Ban nay dung regex thuan cho route, nen phai bat id bang capture group (\d+) va lay qua req.matches[1].
        svr.Put(R"(/tasks/(\d+))",[&taskService]
                (const httplib::Request& req,
                httplib::Response& res)
                {
                    int id =
                        std::stoi(
                            req.matches[1].str()
                        );

                    bool ok =
                        taskService.toggleTask(id);

                    res.status =
                        ok ? 200 : 500;
                });
        svr.Delete(R"(/tasks/(\d+))",[&taskService]
                    (const httplib::Request& req,
                    httplib::Response& res)
                    {
                        int id =
                            std::stoi(
                                req.matches[1].str()
                            );

                        bool ok =
                            taskService.deleteTask(id);

                        res.status =
                            ok ? 200 : 500;
                    });
                    svr.Get("/api/stats",[](const httplib::Request&,
                    httplib::Response& res)
                    {
                        json j;

                        j["total"] = 0;
                        j["completed"] = 0;
                        j["pending"] = 0;

                        res.set_content(
                            j.dump(),
                            "application/json"
                        );
                    });
                    svr.Get("/api/work/history",[](const httplib::Request&,
                    httplib::Response& res)
                    {
                        json arr = json::array();

                        res.set_content(
                            arr.dump(),
                            "application/json"
                        );
                    });
                    svr.Get("/api/stats",[](const httplib::Request&,
                httplib::Response& res)
                {
                    json result;

                    result["health"] = 100;
                    result["avgRating"] = 4.8;

                    result["trend"] = {
                        {
                            {"date","2026-07-01"},
                            {"count",3}
                        },
                        {
                            {"date","2026-07-02"},
                            {"count",5}
                        },
                        {
                            {"date","2026-07-03"},
                            {"count",2}
                        }
                    };

                    res.set_content(
                        result.dump(),
                        "application/json"
                    );
                });
    std::cout << "Server dang chay tai http://localhost:8080 (va http://127.0.0.1:8080)\n";
    // Dung "0.0.0.0" thay vi "localhost" de ep bind IPv4 ro rang,
    // tranh truong hop he thong resolve "localhost" thanh IPv6 (::1) khien 127.0.0.1 khong ket noi duoc.
    svr.listen("0.0.0.0", 8080);
    return 0;
}

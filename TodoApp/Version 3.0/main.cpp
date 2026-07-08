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
#include <algorithm>
#include <map>
#include <cstring>
#include <ctime>
#include <cmath>
#include <fstream>
#include <winhttp.h>

using json = nlohmann::json;

// ================== PHIEN BAN 3.0 - CHATBOX & KHUYEN KHICH NGUOI DUNG ==================
// Ke thua toan bo chuc nang cua Phien ban 2.3 (dang nhap, CRUD, muc tieu, tien do,
// han chot, danh gia, thong ke, lich cong viec), bo sung:
//   - Chatbox tro ly AI (goi Anthropic Claude API qua WinHTTP) hieu ngu canh
//     cong viec hien tai cua nguoi dung de tra loi/goi y phu hop
//   - Neu chua cau hinh ANTHROPIC_API_KEY, chatbox van chay on dinh va tra ve
//     cau tra loi mau (fallback) thay vi bao loi
//   - Banner khuyen khich tren Dashboard (chuoi ngay hoan thanh cong viec, loi dong vien)
//   - Theo doi tien trinh lam viec theo thoi gian thuc: bam "Bat dau lam" tren
//     mot cong viec se mo phien lam viec (work_sessions), dong ho dem chay
//     lien tuc tren giao dien; tab "Theo doi" hien thi phien dang chay,
//     tong thoi gian hom nay/tuan nay va lich su cac phien da hoan thanh
// =========================================================================

static std::string DB_HOST;
static std::string DB_USER;
static std::string DB_PASS;
static std::string DB_NAME;
static unsigned int DB_PORT;
static std::string ANTHROPIC_API_KEY;
static std::string ANTHROPIC_MODEL;

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
    DB_NAME = dotenv::get("DB_NAME", "todo_db_v30");
    DB_PORT = static_cast<unsigned int>(std::stoul(dotenv::get("DB_PORT", "3306")));

    ANTHROPIC_API_KEY = dotenv::get("ANTHROPIC_API_KEY", "");
    ANTHROPIC_MODEL = dotenv::get("ANTHROPIC_MODEL", "claude-3-5-haiku-20241022");
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

// =================== CHATBOX: GOI ANTHROPIC CLAUDE API QUA WINHTTP ===================
// Dung WinHTTP (co san trong Windows, khong can cai them thu vien TLS) de goi
// HTTPS toi api.anthropic.com. Neu khong cau hinh API key hoac co loi mang,
// ham tra ve chuoi rong va ghi ly do vao errorOut - noi goi phai tu xu ly
// fallback, khong duoc de server sap hoac tra loi 500 cho nguoi dung.
std::wstring ToWide(const std::string &s)
{
    if (s.empty())
        return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

std::string CallAnthropicAPI(const std::string &systemPrompt, const std::string &userMessage, std::string &errorOut)
{
    if (ANTHROPIC_API_KEY.empty())
    {
        errorOut = "missing_api_key";
        return "";
    }

    json body;
    body["model"] = ANTHROPIC_MODEL;
    body["max_tokens"] = 400;
    body["system"] = systemPrompt;
    body["messages"] = json::array({ {{"role", "user"}, {"content", userMessage}} });
    std::string bodyStr = body.dump();

    std::string result;
    HINTERNET hSession = nullptr, hConnect = nullptr, hRequest = nullptr;
    bool ok = false;

    hSession = WinHttpOpen(L"TaskFlowChatbox/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession)
        hConnect = WinHttpConnect(hSession, L"api.anthropic.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hConnect)
        hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/v1/messages", nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

    if (hRequest)
    {
        std::wstring headers = L"content-type: application/json\r\n"
                                L"anthropic-version: 2023-06-01\r\n"
                                L"x-api-key: " + ToWide(ANTHROPIC_API_KEY) + L"\r\n";
        BOOL sent = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1L,
                                        (LPVOID)bodyStr.data(), (DWORD)bodyStr.size(), (DWORD)bodyStr.size(), 0);
        if (sent && WinHttpReceiveResponse(hRequest, nullptr))
        {
            DWORD statusCode = 0, statusSize = sizeof(statusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

            std::string responseBody;
            DWORD available = 0;
            do
            {
                available = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &available) || available == 0)
                    break;
                std::string chunk(available, '\0');
                DWORD bytesRead = 0;
                if (!WinHttpReadData(hRequest, &chunk[0], available, &bytesRead))
                    break;
                responseBody.append(chunk.data(), bytesRead);
            } while (available > 0);

            if (statusCode == 200)
            {
                try
                {
                    auto j = json::parse(responseBody);
                    result = j["content"][0]["text"].get<std::string>();
                    ok = true;
                }
                catch (...)
                {
                    errorOut = "parse_error";
                }
            }
            else
            {
                errorOut = "api_status_" + std::to_string(statusCode);
                std::cerr << "[CallAnthropicAPI] Loi API (" << statusCode << "): " << responseBody << std::endl;
            }
        }
        else
        {
            errorOut = "request_failed";
        }
    }
    else
    {
        errorOut = "connection_failed";
    }

    if (hRequest)
        WinHttpCloseHandle(hRequest);
    if (hConnect)
        WinHttpCloseHandle(hConnect);
    if (hSession)
        WinHttpCloseHandle(hSession);

    return ok ? result : "";
}
   
// Cau tra loi du phong khi chua co API key hoac goi API that bai, dam bao
// chatbox luon phan hoi thay vi de trang hoac loi cho nguoi dung.
std::string FallbackChatReply(const std::string &userMessage, int pendingCount, int overdueCount)
{
    std::string lower = userMessage;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

    if (overdueCount > 0)
        return "Bạn đang có " + std::to_string(overdueCount) + " công việc quá hạn. Hãy ưu tiên xử lý chúng trước, "
               "chia nhỏ từng việc để không bị quá tải nhé! (Chatbox đang chạy ở chế độ trả lời mẫu vì chưa cấu hình ANTHROPIC_API_KEY.)";
    if (pendingCount == 0)
        return "Tuyệt vời, bạn không còn công việc nào đang chờ! Đây là lúc tốt để đặt thêm mục tiêu mới. "
               "(Chatbox đang chạy ở chế độ trả lời mẫu vì chưa cấu hình ANTHROPIC_API_KEY.)";
    return "Bạn còn " + std::to_string(pendingCount) + " công việc đang chờ. Cố gắng hoàn thành từng việc một, "
           "đừng quên nghỉ ngơi giữa các phiên làm việc nhé! (Chatbox đang chạy ở chế độ trả lời mẫu vì chưa cấu hình ANTHROPIC_API_KEY.)";
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

// localtime() dung bo nho tinh (static) ben trong CRT nen khong an toan khi
// nhieu luong goi dong thoi -> khoa bang mutex de tranh xung dot du lieu.
static std::mutex g_timeMutex;
std::string FormatDateDaysAgo(int daysAgo)
{
    std::lock_guard<std::mutex> lock(g_timeMutex);
    time_t t = time(nullptr) - static_cast<time_t>(daysAgo) * 86400;
    tm *lt = localtime(&t);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", lt);
    return std::string(buf);
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

    std::string q = "SELECT t.id, t.name, t.category, t.priority, t.completed, t.progress, t.due_date, "
                     "t.quality_rating, t.completed_at, t.estimated_minutes, "
                     "COALESCE((SELECT SUM(duration_seconds) FROM work_sessions WHERE task_id = t.id AND ended_at IS NOT NULL), 0) "
                     "FROM tasks t WHERE t.user_id = " + std::to_string(userId) + " ORDER BY t.id DESC";
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
            obj["progress"] = row[5] ? std::stoi(row[5]) : 0;
            obj["due_date"] = row[6] ? json(row[6]) : json(nullptr);
            obj["quality_rating"] = row[7] ? json(std::stoi(row[7])) : json(nullptr);
            obj["completed_at"] = row[8] ? json(row[8]) : json(nullptr);
            obj["estimated_minutes"] = row[9] ? json(std::stoi(row[9])) : json(nullptr);
            obj["tracked_seconds"] = row[10] ? std::stoi(row[10]) : 0;
            arr.push_back(obj);
        }
        p_mysql_free_result(res);
    }
    p_mysql_close(conn);
    return arr;
}

bool AddTask(int userId, const std::string &name, const std::string &category, const std::string &priority,
             const std::string &dueDate, int estimatedMinutes)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;

    std::string dueDateSql = dueDate.empty() ? "NULL" : ("'" + Escape(conn, dueDate) + "'");
    std::string estimateSql = estimatedMinutes > 0 ? std::to_string(estimatedMinutes) : "NULL";
    std::string q = "INSERT INTO tasks (user_id, name, category, priority, completed, progress, due_date, estimated_minutes) VALUES (" +
                     std::to_string(userId) + ", '" + Escape(conn, name) + "','" + Escape(conn, category) + "','" +
                     Escape(conn, priority) + "', 0, 0, " + dueDateSql + ", " + estimateSql + ")";
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[AddTask] Loi: " << p_mysql_error(conn) << std::endl;
    p_mysql_close(conn);
    return ret == 0;
}

// Tu dong tinh lai % tien do cua mot cong viec dua tren tong thoi gian da
// theo doi (work_sessions) so voi thoi gian du kien (estimated_minutes).
// Chi ap dung khi cong viec co dat estimated_minutes; neu khong, % tien do
// van do nguoi dung tu keo thanh truot nhu truoc (khong bi ghi de).
void UpdateTaskAutoProgress(int userId, int taskId)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return;

    std::string q1 = "SELECT estimated_minutes FROM tasks WHERE id = " + std::to_string(taskId) +
                      " AND user_id = " + std::to_string(userId);
    int estimatedMinutes = 0;
    if (p_mysql_query(conn, q1.c_str()) == 0)
    {
        MYSQL_RES *res = p_mysql_store_result(conn);
        if (res)
        {
            MYSQL_ROW row = p_mysql_fetch_row(res);
            if (row && row[0])
                estimatedMinutes = std::stoi(row[0]);
            p_mysql_free_result(res);
        }
    }
    if (estimatedMinutes <= 0)
    {
        p_mysql_close(conn);
        return;
    }

    std::string q2 = "SELECT COALESCE(SUM(duration_seconds), 0) FROM work_sessions WHERE task_id = " +
                      std::to_string(taskId) + " AND user_id = " + std::to_string(userId) + " AND ended_at IS NOT NULL";
    int trackedSeconds = 0;
    if (p_mysql_query(conn, q2.c_str()) == 0)
    {
        MYSQL_RES *res = p_mysql_store_result(conn);
        if (res)
        {
            MYSQL_ROW row = p_mysql_fetch_row(res);
            if (row && row[0])
                trackedSeconds = std::stoi(row[0]);
            p_mysql_free_result(res);
        }
    }

    int estimatedSeconds = estimatedMinutes * 60;
    int progress = estimatedSeconds > 0
                       ? std::min(100, static_cast<int>(std::lround(trackedSeconds * 100.0 / estimatedSeconds)))
                       : 0;

    std::string q3 = "UPDATE tasks SET progress = " + std::to_string(progress) +
                      (progress >= 100 ? ", completed = 1, completed_at = NOW()" : "") +
                      " WHERE id = " + std::to_string(taskId) + " AND user_id = " + std::to_string(userId);
    if (p_mysql_query(conn, q3.c_str()))
        std::cerr << "[UpdateTaskAutoProgress] Loi: " << p_mysql_error(conn) << std::endl;

    p_mysql_close(conn);
}

// Dao trang thai hoan thanh. Khi chuyen sang hoan thanh: progress = 100.
// Khi bo hoan thanh: giu nguyen progress de nguoi dung tu dieu chinh tiep.
bool ToggleTask(int userId, int id)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;

    std::string checkQ = "SELECT completed FROM tasks WHERE id = " + std::to_string(id) +
                          " AND user_id = " + std::to_string(userId);
    if (p_mysql_query(conn, checkQ.c_str()))
    {
        p_mysql_close(conn);
        return false;
    }
    MYSQL_RES *res = p_mysql_store_result(conn);
    if (!res)
    {
        p_mysql_close(conn);
        return false;
    }
    MYSQL_ROW row = p_mysql_fetch_row(res);
    if (!row)
    {
        p_mysql_free_result(res);
        p_mysql_close(conn);
        return false;
    }
    bool wasCompleted = row[0] && std::string(row[0]) == "1";
    p_mysql_free_result(res);

    std::string q;
    if (wasCompleted)
        q = "UPDATE tasks SET completed = 0, completed_at = NULL WHERE id = " + std::to_string(id) + " AND user_id = " + std::to_string(userId);
    else
        q = "UPDATE tasks SET completed = 1, completed_at = NOW(), progress = 100 WHERE id = " + std::to_string(id) + " AND user_id = " + std::to_string(userId);

    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[ToggleTask] Loi: " << p_mysql_error(conn) << std::endl;
    p_mysql_close(conn);
    return ret == 0;
}

bool UpdateProgress(int userId, int id, int progress)
{
    progress = std::max(0, std::min(100, progress));
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;
    std::string q = "UPDATE tasks SET progress = " + std::to_string(progress) +
                     (progress == 100 ? ", completed = 1, completed_at = NOW()" : "") +
                     " WHERE id = " + std::to_string(id) + " AND user_id = " + std::to_string(userId);
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[UpdateProgress] Loi: " << p_mysql_error(conn) << std::endl;
    bool ok = ret == 0 && p_mysql_affected_rows(conn) > 0;
    p_mysql_close(conn);
    return ok;
}

bool UpdateTaskEstimate(int userId, int id, int estimatedMinutes)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;
    std::string estimateSql = estimatedMinutes > 0 ? std::to_string(estimatedMinutes) : "NULL";
    std::string q = "UPDATE tasks SET estimated_minutes = " + estimateSql +
                     " WHERE id = " + std::to_string(id) + " AND user_id = " + std::to_string(userId);
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[UpdateTaskEstimate] Loi: " << p_mysql_error(conn) << std::endl;
    bool ok = ret == 0 && p_mysql_affected_rows(conn) > 0;
    p_mysql_close(conn);
    if (ok)
        UpdateTaskAutoProgress(userId, id);
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

// Tra ve: 0 = thanh cong, 1 = task chua hoan thanh (khong the danh gia),
//         2 = loi khac/khong tim thay, 3 = rating ngoai khoang 1-5
int UpdateRating(int userId, int id, int rating)
{
    if (rating < 1 || rating > 5)
        return 3;

    MYSQL *conn = ConnectDB();
    if (!conn)
        return 2;

    std::string checkQ = "SELECT completed FROM tasks WHERE id = " + std::to_string(id) +
                          " AND user_id = " + std::to_string(userId);
    if (p_mysql_query(conn, checkQ.c_str()))
    {
        p_mysql_close(conn);
        return 2;
    }
    MYSQL_RES *res = p_mysql_store_result(conn);
    if (!res)
    {
        p_mysql_close(conn);
        return 2;
    }
    MYSQL_ROW row = p_mysql_fetch_row(res);
    if (!row)
    {
        p_mysql_free_result(res);
        p_mysql_close(conn);
        return 2;
    }
    bool isCompleted = row[0] && std::string(row[0]) == "1";
    p_mysql_free_result(res);

    if (!isCompleted)
    {
        p_mysql_close(conn);
        return 1;
    }

    std::string q = "UPDATE tasks SET quality_rating = " + std::to_string(rating) + " WHERE id = " + std::to_string(id) +
                     " AND user_id = " + std::to_string(userId);
    int ret = p_mysql_query(conn, q.c_str());
    p_mysql_close(conn);
    return ret == 0 ? 0 : 2;
}

// =================== THONG KE / PHAN TICH ===================
json GetStats(int userId)
{
    json out;
    out["total"] = 0;
    out["completed"] = 0;
    out["pending"] = 0;
    out["avgProgress"] = 0;
    out["avgRating"] = 0.0;
    out["byCategory"] = { {"health", 0}, {"work", 0}, {"mental", 0}, {"others", 0} };
    out["byPriority"] = { {"p-high", 0}, {"p-medium", 0}, {"p-low", 0} };
    out["ratingDistribution"] = { {"1", 0}, {"2", 0}, {"3", 0}, {"4", 0}, {"5", 0} };
    out["onTime"] = 0;
    out["late"] = 0;
    out["trend"] = json::array();

    MYSQL *conn = ConnectDB();
    if (!conn)
        return out;

    std::string q = "SELECT category, priority, completed, progress, due_date, quality_rating, completed_at "
                     "FROM tasks WHERE user_id = " + std::to_string(userId);
    if (p_mysql_query(conn, q.c_str()))
    {
        std::cerr << "[GetStats] Query loi: " << p_mysql_error(conn) << std::endl;
        p_mysql_close(conn);
        return out;
    }

    // Chuan bi khung 7 ngay gan nhat (ke ca hom nay) de ve bieu do xu huong.
    std::map<std::string, int> trendMap;
    for (int i = 6; i >= 0; --i)
        trendMap[FormatDateDaysAgo(i)] = 0;

    int total = 0, completedCount = 0;
    long long progressSum = 0;
    int ratingSum = 0, ratingCount = 0;
    int onTime = 0, late = 0;
    std::unordered_map<std::string, int> catCounts{ {"health", 0}, {"work", 0}, {"mental", 0}, {"others", 0} };
    std::unordered_map<std::string, int> prioCounts{ {"p-high", 0}, {"p-medium", 0}, {"p-low", 0} };
    std::unordered_map<std::string, int> ratingDist{ {"1", 0}, {"2", 0}, {"3", 0}, {"4", 0}, {"5", 0} };

    MYSQL_RES *res = p_mysql_store_result(conn);
    if (res)
    {
        MYSQL_ROW row;
        while ((row = p_mysql_fetch_row(res)))
        {
            total++;
            std::string category = row[0] ? row[0] : "others";
            std::string priority = row[1] ? row[1] : "p-low";
            bool completed = row[2] && std::string(row[2]) == "1";
            int progress = row[3] ? std::stoi(row[3]) : 0;
            const char *dueDate = row[4];
            const char *rating = row[5];
            const char *completedAt = row[6];

            if (catCounts.find(category) == catCounts.end())
                category = "others";
            catCounts[category]++;

            if (prioCounts.find(priority) != prioCounts.end())
                prioCounts[priority]++;

            progressSum += progress;

            if (completed)
            {
                completedCount++;
                if (rating)
                {
                    int r = std::stoi(rating);
                    ratingSum += r;
                    ratingCount++;
                    std::string key = std::to_string(r);
                    if (ratingDist.find(key) != ratingDist.end())
                        ratingDist[key]++;
                }
                if (dueDate && completedAt)
                {
                    std::string completedDateOnly(completedAt, completedAt + std::min<size_t>(10, strlen(completedAt)));
                    if (completedDateOnly <= std::string(dueDate))
                        onTime++;
                    else
                        late++;
                }
                if (completedAt)
                {
                    std::string dateOnly(completedAt, completedAt + std::min<size_t>(10, strlen(completedAt)));
                    auto it = trendMap.find(dateOnly);
                    if (it != trendMap.end())
                        it->second++;
                }
            }
        }
        p_mysql_free_result(res);
    }
    p_mysql_close(conn);

    out["total"] = total;
    out["completed"] = completedCount;
    out["pending"] = total - completedCount;
    out["avgProgress"] = total ? static_cast<int>(progressSum / total) : 0;
    out["avgRating"] = ratingCount ? (static_cast<double>(ratingSum) / ratingCount) : 0.0;
    out["byCategory"] = { {"health", catCounts["health"]}, {"work", catCounts["work"]}, {"mental", catCounts["mental"]}, {"others", catCounts["others"]} };
    out["byPriority"] = { {"p-high", prioCounts["p-high"]}, {"p-medium", prioCounts["p-medium"]}, {"p-low", prioCounts["p-low"]} };
    out["ratingDistribution"] = { {"1", ratingDist["1"]}, {"2", ratingDist["2"]}, {"3", ratingDist["3"]}, {"4", ratingDist["4"]}, {"5", ratingDist["5"]} };
    out["onTime"] = onTime;
    out["late"] = late;

    json trendArr = json::array();
    for (auto &kv : trendMap)
    {
        json point;
        point["date"] = kv.first;
        point["count"] = kv.second;
        trendArr.push_back(point);
    }
    out["trend"] = trendArr;

    return out;
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

// =================== KHONG GIAN LAM VIEC (workspaces) ===================
// Khong gian lam viec la mot "context" lam viec do nguoi dung tu dat ten
// (vd "Lap trinh", "Hoc tieng Anh"), doc lap voi tung cong viec cu the -
// dung khi muon theo doi thoi gian cho ca mot mang cong viec thay vi 1 task.
json GetWorkspaces(int userId)
{
    json arr = json::array();
    MYSQL *conn = ConnectDB();
    if (!conn)
        return arr;

    std::string q = "SELECT w.id, w.name, w.color, "
                     "COALESCE((SELECT SUM(duration_seconds) FROM work_sessions WHERE workspace_id = w.id AND ended_at IS NOT NULL), 0) "
                     "FROM workspaces w WHERE w.user_id = " + std::to_string(userId) + " ORDER BY w.id DESC";
    if (p_mysql_query(conn, q.c_str()) == 0)
    {
        MYSQL_RES *res = p_mysql_store_result(conn);
        if (res)
        {
            MYSQL_ROW row;
            while ((row = p_mysql_fetch_row(res)))
            {
                json obj;
                obj["id"] = std::stoi(row[0]);
                obj["name"] = row[1] ? row[1] : "";
                obj["color"] = row[2] ? row[2] : "#5b5bf5";
                obj["tracked_seconds"] = row[3] ? std::stoi(row[3]) : 0;
                arr.push_back(obj);
            }
            p_mysql_free_result(res);
        }
    }
    p_mysql_close(conn);
    return arr;
}

bool AddWorkspace(int userId, const std::string &name, const std::string &color)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;
    std::string colorSql = color.empty() ? "#5b5bf5" : color;
    std::string q = "INSERT INTO workspaces (user_id, name, color) VALUES (" +
                     std::to_string(userId) + ", '" + Escape(conn, name) + "', '" + Escape(conn, colorSql) + "')";
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[AddWorkspace] Loi: " << p_mysql_error(conn) << std::endl;
    p_mysql_close(conn);
    return ret == 0;
}

bool DeleteWorkspace(int userId, int id)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;
    std::string q = "DELETE FROM workspaces WHERE id = " + std::to_string(id) +
                     " AND user_id = " + std::to_string(userId);
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[DeleteWorkspace] Loi: " << p_mysql_error(conn) << std::endl;
    bool ok = ret == 0 && p_mysql_affected_rows(conn) > 0;
    p_mysql_close(conn);
    return ok;
}

// =================== THEO DOI TIEN TRINH LAM VIEC (work_sessions) ===================
// Chi cho phep moi nguoi dung co MOT phien lam viec dang chay tai mot thoi
// diem: bat dau phien moi se tu dong dung phien dang chay (neu co) truoc.

// Dung phien dang chay cua user (neu co). Neu phien dang tam dung, cong don
// khoang thoi gian tam dung cuoi cung vao paused_seconds truoc khi tinh
// duration_seconds (= tong thoi gian - thoi gian tam dung). Neu phien do gan
// voi 1 cong viec co dat estimated_minutes, tu dong tinh lai % tien do.
// Tra ve true neu co phien vua dung.
bool StopActiveSession(int userId)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;

    int taskId = -1;
    std::string checkQ = "SELECT task_id FROM work_sessions WHERE user_id = " +
                          std::to_string(userId) + " AND ended_at IS NULL LIMIT 1";
    if (p_mysql_query(conn, checkQ.c_str()) == 0)
    {
        MYSQL_RES *res = p_mysql_store_result(conn);
        if (res)
        {
            MYSQL_ROW row = p_mysql_fetch_row(res);
            if (row && row[0])
                taskId = std::stoi(row[0]);
            p_mysql_free_result(res);
        }
    }

    std::string q =
        "UPDATE work_sessions SET "
        "paused_seconds = paused_seconds + IF(paused_at IS NOT NULL, TIMESTAMPDIFF(SECOND, paused_at, NOW()), 0), "
        "ended_at = NOW(), "
        "duration_seconds = TIMESTAMPDIFF(SECOND, started_at, NOW()) - "
        "(paused_seconds + IF(paused_at IS NOT NULL, TIMESTAMPDIFF(SECOND, paused_at, NOW()), 0)), "
        "paused_at = NULL "
        "WHERE user_id = " + std::to_string(userId) + " AND ended_at IS NULL";
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[StopActiveSession] Loi: " << p_mysql_error(conn) << std::endl;
    bool ok = ret == 0 && p_mysql_affected_rows(conn) > 0;
    p_mysql_close(conn);

    if (ok && taskId > 0)
        UpdateTaskAutoProgress(userId, taskId);

    return ok;
}

// Bat dau phien lam viec moi. Chi truyen 1 trong 2: taskId (theo doi mot
// cong viec cu the) hoac workspaceId (theo doi mot khong gian lam viec).
// Neu ca hai <= 0, phien duoc tinh la "lam viec chung".
bool StartWorkSession(int userId, int taskId, int workspaceId)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;

    StopActiveSession(userId);

    std::string taskIdSql = taskId > 0 ? std::to_string(taskId) : "NULL";
    std::string workspaceIdSql = workspaceId > 0 ? std::to_string(workspaceId) : "NULL";
    std::string q = "INSERT INTO work_sessions (user_id, task_id, workspace_id, started_at) VALUES (" +
                     std::to_string(userId) + ", " + taskIdSql + ", " + workspaceIdSql + ", NOW())";
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[StartWorkSession] Loi: " << p_mysql_error(conn) << std::endl;
    p_mysql_close(conn);
    return ret == 0;
}

// Tam dung phien dang chay (chi co tac dung neu dang chay, khong phai da tam dung).
bool PauseActiveSession(int userId)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;
    std::string q = "UPDATE work_sessions SET paused_at = NOW() WHERE user_id = " +
                     std::to_string(userId) + " AND ended_at IS NULL AND paused_at IS NULL";
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[PauseActiveSession] Loi: " << p_mysql_error(conn) << std::endl;
    bool ok = ret == 0 && p_mysql_affected_rows(conn) > 0;
    p_mysql_close(conn);
    return ok;
}

// Tiep tuc phien dang tam dung: cong don khoang thoi gian vua tam dung vao
// paused_seconds roi xoa paused_at (tro lai trang thai dang chay).
bool ResumeActiveSession(int userId)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;
    std::string q = "UPDATE work_sessions SET "
                     "paused_seconds = paused_seconds + TIMESTAMPDIFF(SECOND, paused_at, NOW()), "
                     "paused_at = NULL "
                     "WHERE user_id = " + std::to_string(userId) + " AND ended_at IS NULL AND paused_at IS NOT NULL";
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[ResumeActiveSession] Loi: " << p_mysql_error(conn) << std::endl;
    bool ok = ret == 0 && p_mysql_affected_rows(conn) > 0;
    p_mysql_close(conn);
    return ok;
}

// Ghi/cap nhat ghi chu cho mot phien lam viec (dang chay hoac da ket thuc).
bool UpdateSessionNote(int userId, int sessionId, const std::string &note)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;
    std::string q = "UPDATE work_sessions SET note = '" + Escape(conn, note) + "' WHERE id = " +
                     std::to_string(sessionId) + " AND user_id = " + std::to_string(userId);
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[UpdateSessionNote] Loi: " << p_mysql_error(conn) << std::endl;
    bool ok = ret == 0 && p_mysql_affected_rows(conn) > 0;
    p_mysql_close(conn);
    return ok;
}

// Luu duong dan file dinh kem (da duoc ghi ra dia truoc do) vao phien lam viec.
bool SaveSessionAttachment(int userId, int sessionId, const std::string &fileName, const std::string &storedPath)
{
    MYSQL *conn = ConnectDB();
    if (!conn)
        return false;
    std::string q = "UPDATE work_sessions SET attachment_name = '" + Escape(conn, fileName) +
                     "', attachment_path = '" + Escape(conn, storedPath) + "' WHERE id = " +
                     std::to_string(sessionId) + " AND user_id = " + std::to_string(userId);
    int ret = p_mysql_query(conn, q.c_str());
    if (ret)
        std::cerr << "[SaveSessionAttachment] Loi: " << p_mysql_error(conn) << std::endl;
    bool ok = ret == 0 && p_mysql_affected_rows(conn) > 0;
    p_mysql_close(conn);
    return ok;
}

// Tra ve phien dang chay (neu co) kem so giay da troi qua tinh tai thoi
// diem goi ham, de client dong bo dong ho dem thoi gian thuc. Neu phien
// dang tam dung, elapsed_seconds duoc "dong bang" tai thoi diem tam dung.
json GetActiveSession(int userId)
{
    json out;
    out["active"] = false;

    MYSQL *conn = ConnectDB();
    if (!conn)
        return out;

    std::string q = "SELECT ws.id, ws.task_id, t.name, ws.workspace_id, w.name, w.color, "
                     "ws.paused_at, ws.note, ws.attachment_name, "
                     "CASE WHEN ws.paused_at IS NOT NULL "
                     "     THEN TIMESTAMPDIFF(SECOND, ws.started_at, ws.paused_at) - ws.paused_seconds "
                     "     ELSE TIMESTAMPDIFF(SECOND, ws.started_at, NOW()) - ws.paused_seconds END "
                     "FROM work_sessions ws LEFT JOIN tasks t ON ws.task_id = t.id "
                     "LEFT JOIN workspaces w ON ws.workspace_id = w.id "
                     "WHERE ws.user_id = " + std::to_string(userId) + " AND ws.ended_at IS NULL LIMIT 1";
    if (p_mysql_query(conn, q.c_str()) == 0)
    {
        MYSQL_RES *res = p_mysql_store_result(conn);
        if (res)
        {
            MYSQL_ROW row = p_mysql_fetch_row(res);
            if (row)
            {
                out["active"] = true;
                out["session_id"] = std::stoi(row[0]);
                out["task_id"] = row[1] ? json(std::stoi(row[1])) : json(nullptr);
                out["workspace_id"] = row[3] ? json(std::stoi(row[3])) : json(nullptr);
                out["workspace_color"] = row[5] ? row[5] : "#5b5bf5";
                out["label"] = row[2] ? row[2] : (row[4] ? row[4] : "Làm việc chung");
                out["paused"] = row[6] != nullptr;
                out["note"] = row[7] ? row[7] : "";
                out["attachment_name"] = row[8] ? row[8] : "";
                out["elapsed_seconds"] = row[9] ? std::max(0, std::stoi(row[9])) : 0;
            }
            p_mysql_free_result(res);
        }
    }
    p_mysql_close(conn);
    return out;
}

json GetWorkHistory(int userId, int limit)
{
    json arr = json::array();
    MYSQL *conn = ConnectDB();
    if (!conn)
        return arr;

    std::string q = "SELECT ws.id, ws.task_id, t.name, ws.workspace_id, w.name, ws.started_at, ws.ended_at, "
                     "ws.duration_seconds, ws.note, ws.attachment_name, ws.attachment_path "
                     "FROM work_sessions ws LEFT JOIN tasks t ON ws.task_id = t.id "
                     "LEFT JOIN workspaces w ON ws.workspace_id = w.id "
                     "WHERE ws.user_id = " + std::to_string(userId) + " AND ws.ended_at IS NOT NULL "
                     "ORDER BY ws.started_at DESC LIMIT " + std::to_string(limit);
    if (p_mysql_query(conn, q.c_str()) == 0)
    {
        MYSQL_RES *res = p_mysql_store_result(conn);
        if (res)
        {
            MYSQL_ROW row;
            while ((row = p_mysql_fetch_row(res)))
            {
                json obj;
                obj["id"] = std::stoi(row[0]);
                obj["task_id"] = row[1] ? json(std::stoi(row[1])) : json(nullptr);
                obj["workspace_id"] = row[3] ? json(std::stoi(row[3])) : json(nullptr);
                obj["label"] = row[2] ? row[2] : (row[4] ? row[4] : "Làm việc chung");
                obj["started_at"] = row[5] ? row[5] : "";
                obj["ended_at"] = row[6] ? row[6] : "";
                obj["duration_seconds"] = row[7] ? std::stoi(row[7]) : 0;
                obj["note"] = row[8] ? row[8] : "";
                obj["attachment_name"] = row[9] ? row[9] : "";
                obj["attachment_url"] = row[10] ? json("/uploads/" + std::string(row[10])) : json(nullptr);
                arr.push_back(obj);
            }
            p_mysql_free_result(res);
        }
    }
    p_mysql_close(conn);
    return arr;
}

// Muc tieu thoi gian lam viec moi ngay (giay) dung de tinh % hoan thanh tu
// dong - 4 gio/ngay la muc tieu tap trung hop ly cho hoc sinh/sinh vien.
static const int DAILY_WORK_GOAL_SECONDS = 4 * 3600;

json GetWorkSummary(int userId)
{
    json out;
    out["todaySeconds"] = 0;
    out["weekSeconds"] = 0;
    out["todayGoalPercent"] = 0;
    out["weekGoalPercent"] = 0;
    out["byTask"] = json::array();
    out["byWorkspace"] = json::array();
    out["dailyTrend"] = json::array();

    MYSQL *conn = ConnectDB();
    if (!conn)
        return out;

    int todaySeconds = 0, weekSeconds = 0;

    std::string todayQ = "SELECT COALESCE(SUM(duration_seconds), 0) FROM work_sessions "
                          "WHERE user_id = " + std::to_string(userId) + " AND ended_at IS NOT NULL AND DATE(started_at) = CURDATE()";
    if (p_mysql_query(conn, todayQ.c_str()) == 0)
    {
        MYSQL_RES *res = p_mysql_store_result(conn);
        if (res)
        {
            MYSQL_ROW row = p_mysql_fetch_row(res);
            if (row && row[0])
                todaySeconds = std::stoi(row[0]);
            p_mysql_free_result(res);
        }
    }

    std::string weekQ = "SELECT COALESCE(SUM(duration_seconds), 0) FROM work_sessions "
                         "WHERE user_id = " + std::to_string(userId) + " AND ended_at IS NOT NULL AND started_at >= (CURDATE() - INTERVAL 6 DAY)";
    if (p_mysql_query(conn, weekQ.c_str()) == 0)
    {
        MYSQL_RES *res = p_mysql_store_result(conn);
        if (res)
        {
            MYSQL_ROW row = p_mysql_fetch_row(res);
            if (row && row[0])
                weekSeconds = std::stoi(row[0]);
            p_mysql_free_result(res);
        }
    }

    out["todaySeconds"] = todaySeconds;
    out["weekSeconds"] = weekSeconds;
    out["todayGoalPercent"] = static_cast<int>(std::lround(todaySeconds * 100.0 / DAILY_WORK_GOAL_SECONDS));
    out["weekGoalPercent"] = static_cast<int>(std::lround(weekSeconds * 100.0 / (DAILY_WORK_GOAL_SECONDS * 7)));

    std::string byTaskQ = "SELECT COALESCE(t.name, 'Làm việc chung'), SUM(ws.duration_seconds) AS total "
                          "FROM work_sessions ws LEFT JOIN tasks t ON ws.task_id = t.id "
                          "WHERE ws.user_id = " + std::to_string(userId) + " AND ws.ended_at IS NOT NULL AND ws.task_id IS NOT NULL "
                          "GROUP BY ws.task_id, t.name ORDER BY total DESC LIMIT 8";
    if (p_mysql_query(conn, byTaskQ.c_str()) == 0)
    {
        MYSQL_RES *res = p_mysql_store_result(conn);
        if (res)
        {
            json byTask = json::array();
            MYSQL_ROW row;
            while ((row = p_mysql_fetch_row(res)))
            {
                json item;
                item["name"] = row[0] ? row[0] : "Làm việc chung";
                item["seconds"] = row[1] ? std::stoi(row[1]) : 0;
                byTask.push_back(item);
            }
            out["byTask"] = byTask;
            p_mysql_free_result(res);
        }
    }

    std::string byWorkspaceQ = "SELECT w.name, w.color, SUM(ws.duration_seconds) AS total "
                          "FROM work_sessions ws JOIN workspaces w ON ws.workspace_id = w.id "
                          "WHERE ws.user_id = " + std::to_string(userId) + " AND ws.ended_at IS NOT NULL "
                          "GROUP BY ws.workspace_id, w.name, w.color ORDER BY total DESC LIMIT 8";
    if (p_mysql_query(conn, byWorkspaceQ.c_str()) == 0)
    {
        MYSQL_RES *res = p_mysql_store_result(conn);
        if (res)
        {
            json byWorkspace = json::array();
            MYSQL_ROW row;
            while ((row = p_mysql_fetch_row(res)))
            {
                json item;
                item["name"] = row[0] ? row[0] : "";
                item["color"] = row[1] ? row[1] : "#5b5bf5";
                item["seconds"] = row[2] ? std::stoi(row[2]) : 0;
                byWorkspace.push_back(item);
            }
            out["byWorkspace"] = byWorkspace;
            p_mysql_free_result(res);
        }
    }

    std::map<std::string, int> dailyMap;
    for (int i = 6; i >= 0; --i)
        dailyMap[FormatDateDaysAgo(i)] = 0;

    std::string dailyQ = "SELECT DATE(started_at), SUM(duration_seconds) FROM work_sessions "
                          "WHERE user_id = " + std::to_string(userId) + " AND ended_at IS NOT NULL "
                          "AND started_at >= (CURDATE() - INTERVAL 6 DAY) GROUP BY DATE(started_at)";
    if (p_mysql_query(conn, dailyQ.c_str()) == 0)
    {
        MYSQL_RES *res = p_mysql_store_result(conn);
        if (res)
        {
            MYSQL_ROW row;
            while ((row = p_mysql_fetch_row(res)))
            {
                if (!row[0])
                    continue;
                auto it = dailyMap.find(row[0]);
                if (it != dailyMap.end())
                    it->second = row[1] ? std::stoi(row[1]) : 0;
            }
            p_mysql_free_result(res);
        }
    }
    json dailyTrend = json::array();
    for (auto &kv : dailyMap)
    {
        json point;
        point["date"] = kv.first;
        point["seconds"] = kv.second;
        dailyTrend.push_back(point);
    }
    out["dailyTrend"] = dailyTrend;

    p_mysql_close(conn);
    return out;
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
    CreateDirectoryA("uploads", nullptr); // bo qua neu da ton tai

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
    svr.set_mount_point("/uploads", "./uploads");

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
            std::string dueDate = data.value("due_date", "");
            int estimatedMinutes = data.value("estimated_minutes", 0);
            if (name.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"name khong duoc rong\"}", "application/json");
                return;
            }
            bool ok = AddTask(userId, name, category, priority, dueDate, estimatedMinutes);
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

    svr.Patch(R"(/tasks/(\d+)/progress)", [](const httplib::Request &req, httplib::Response &res)
              {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            int id = std::stoi(req.matches[1].str());
            auto data = json::parse(req.body);
            int progress = data.value("progress", 0);
            bool ok = UpdateProgress(userId, id, progress);
            res.status = ok ? 200 : 500;
            res.set_content(ok ? "{\"status\":\"ok\"}" : "{\"error\":\"update that bai\"}", "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"du lieu khong hop le\"}", "application/json");
        } });

    svr.Patch(R"(/tasks/(\d+)/estimate)", [](const httplib::Request &req, httplib::Response &res)
              {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            int id = std::stoi(req.matches[1].str());
            auto data = json::parse(req.body);
            int estimatedMinutes = data.value("estimated_minutes", 0);
            bool ok = UpdateTaskEstimate(userId, id, estimatedMinutes);
            res.status = ok ? 200 : 500;
            res.set_content(ok ? "{\"status\":\"ok\"}" : "{\"error\":\"update that bai\"}", "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"du lieu khong hop le\"}", "application/json");
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

    svr.Put(R"(/tasks/(\d+)/rating)", [](const httplib::Request &req, httplib::Response &res)
            {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            int id = std::stoi(req.matches[1].str());
            auto data = json::parse(req.body);
            int rating = data.value("rating", 0);
            int ret = UpdateRating(userId, id, rating);
            if (ret == 0) {
                res.set_content("{\"status\":\"ok\"}", "application/json");
            } else if (ret == 1) {
                res.status = 400;
                res.set_content("{\"error\":\"Chi co the danh gia cong viec da hoan thanh\"}", "application/json");
            } else if (ret == 3) {
                res.status = 400;
                res.set_content("{\"error\":\"Diem danh gia phai tu 1 den 5\"}", "application/json");
            } else {
                res.status = 500;
                res.set_content("{\"error\":\"Danh gia that bai\"}", "application/json");
            }
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"du lieu khong hop le\"}", "application/json");
        } });

    // ---------- THONG KE API (yeu cau dang nhap) ----------
    svr.Get("/api/stats", [](const httplib::Request &req, httplib::Response &res)
            {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        res.set_content(GetStats(userId).dump(), "application/json"); });

    // ---------- CHATBOX API (tro ly AI, yeu cau dang nhap) ----------
    svr.Post("/api/chat", [](const httplib::Request &req, httplib::Response &res)
             {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            auto data = json::parse(req.body);
            std::string message = data.value("message", "");
            if (message.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"message khong duoc rong\"}", "application/json");
                return;
            }

            json tasksJson = GetTasks(userId);
            int pendingCount = 0, overdueCount = 0;
            std::string today = FormatDateDaysAgo(0);
            for (auto &t : tasksJson) {
                bool completed = t.value("completed", false);
                if (!completed) {
                    pendingCount++;
                    if (!t["due_date"].is_null() && t["due_date"].get<std::string>() < today)
                        overdueCount++;
                }
            }

            std::string systemPrompt =
                "Ban la tro ly quan ly cong viec than thien, luon dong vien va khich le nguoi dung ten " + username +
                ". Nguoi dung hien co " + std::to_string(pendingCount) + " cong viec dang cho, trong do " +
                std::to_string(overdueCount) + " cong viec da qua han. Hay tra loi ngan gon (duoi 100 tu), "
                "bang tieng Viet, tap trung dong vien va goi y thiet thuc de nguoi dung hoan thanh cong viec.";

            std::string apiError;
            std::string reply = CallAnthropicAPI(systemPrompt, message, apiError);
            json out;
            if (!reply.empty()) {
                out["reply"] = reply;
                out["source"] = "ai";
            } else {
                out["reply"] = FallbackChatReply(message, pendingCount, overdueCount);
                out["source"] = "fallback";
                if (!apiError.empty())
                    std::cerr << "[api/chat] Fallback ly do: " << apiError << std::endl;
            }
            res.set_content(out.dump(), "application/json");
        } catch (const std::exception &e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        } });

    // ---------- THEO DOI TIEN TRINH LAM VIEC (real-time, yeu cau dang nhap) ----------
    svr.Post("/api/work/start", [](const httplib::Request &req, httplib::Response &res)
             {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            auto data = json::parse(req.body);
            int taskId = data.value("task_id", 0);
            int workspaceId = data.value("workspace_id", 0);
            bool ok = StartWorkSession(userId, taskId, workspaceId);
            res.status = ok ? 201 : 500;
            res.set_content(ok ? GetActiveSession(userId).dump() : "{\"error\":\"khong the bat dau phien lam viec\"}", "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"du lieu khong hop le\"}", "application/json");
        } });

    svr.Post("/api/work/stop", [](const httplib::Request &req, httplib::Response &res)
             {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        bool ok = StopActiveSession(userId);
        res.status = ok ? 200 : 400;
        res.set_content(ok ? "{\"status\":\"ok\"}" : "{\"error\":\"khong co phien nao dang chay\"}", "application/json"); });

    svr.Post("/api/work/pause", [](const httplib::Request &req, httplib::Response &res)
             {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        bool ok = PauseActiveSession(userId);
        res.status = ok ? 200 : 400;
        res.set_content(ok ? GetActiveSession(userId).dump() : "{\"error\":\"khong the tam dung\"}", "application/json"); });

    svr.Post("/api/work/resume", [](const httplib::Request &req, httplib::Response &res)
             {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        bool ok = ResumeActiveSession(userId);
        res.status = ok ? 200 : 400;
        res.set_content(ok ? GetActiveSession(userId).dump() : "{\"error\":\"khong the tiep tuc\"}", "application/json"); });

    svr.Patch(R"(/api/work/(\d+)/note)", [](const httplib::Request &req, httplib::Response &res)
              {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            int sessionId = std::stoi(req.matches[1].str());
            auto data = json::parse(req.body);
            std::string note = data.value("note", "");
            bool ok = UpdateSessionNote(userId, sessionId, note);
            res.status = ok ? 200 : 500;
            res.set_content(ok ? "{\"status\":\"ok\"}" : "{\"error\":\"khong the luu ghi chu\"}", "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"du lieu khong hop le\"}", "application/json");
        } });

    svr.Post(R"(/api/work/(\d+)/attachment)", [](const httplib::Request &req, httplib::Response &res)
             {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        int sessionId;
        try { sessionId = std::stoi(req.matches[1].str()); }
        catch (...) { res.status = 400; res.set_content("{\"error\":\"id khong hop le\"}", "application/json"); return; }

        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content("{\"error\":\"thieu file dinh kem\"}", "application/json");
            return;
        }
        const auto &file = req.get_file_value("file");
        if (file.content.size() > 10 * 1024 * 1024) {
            res.status = 400;
            res.set_content("{\"error\":\"file qua lon (toi da 10MB)\"}", "application/json");
            return;
        }

        // Dat ten file duy nhat tren dia de tranh trung/ghi de: <userId>_<sessionId>_<random>_<ten goc>.
        std::string safeOriginal = file.filename;
        for (auto &c : safeOriginal)
            if (c == '/' || c == '\\' || c == ':')
                c = '_';
        std::string storedName = std::to_string(userId) + "_" + std::to_string(sessionId) + "_" + RandomHex(4) + "_" + safeOriginal;
        std::string storedFullPath = "./uploads/" + storedName;

        std::ofstream out(storedFullPath, std::ios::binary);
        if (!out) {
            res.status = 500;
            res.set_content("{\"error\":\"khong the luu file\"}", "application/json");
            return;
        }
        out.write(file.content.data(), file.content.size());
        out.close();

        bool ok = SaveSessionAttachment(userId, sessionId, safeOriginal, storedName);
        res.status = ok ? 200 : 500;
        res.set_content(ok ? "{\"status\":\"ok\"}" : "{\"error\":\"khong the luu thong tin file\"}", "application/json"); });

    svr.Get("/api/work/active", [](const httplib::Request &req, httplib::Response &res)
            {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        res.set_content(GetActiveSession(userId).dump(), "application/json"); });

    svr.Get("/api/work/history", [](const httplib::Request &req, httplib::Response &res)
            {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        res.set_content(GetWorkHistory(userId, 30).dump(), "application/json"); });

    svr.Get("/api/work/summary", [](const httplib::Request &req, httplib::Response &res)
            {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        res.set_content(GetWorkSummary(userId).dump(), "application/json"); });

    // ---------- KHONG GIAN LAM VIEC API (yeu cau dang nhap) ----------
    svr.Get("/workspaces", [](const httplib::Request &req, httplib::Response &res)
            {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        res.set_content(GetWorkspaces(userId).dump(), "application/json"); });

    svr.Post("/workspaces", [](const httplib::Request &req, httplib::Response &res)
             {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            auto data = json::parse(req.body);
            std::string name = data.value("name", "");
            std::string color = data.value("color", "#5b5bf5");
            if (name.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"ten khong gian khong duoc rong\"}", "application/json");
                return;
            }
            bool ok = AddWorkspace(userId, name, color);
            res.status = ok ? 201 : 500;
            res.set_content(ok ? "{\"status\":\"ok\"}" : "{\"error\":\"insert that bai\"}", "application/json");
        } catch (const std::exception &e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        } });

    svr.Delete(R"(/workspaces/(\d+))", [](const httplib::Request &req, httplib::Response &res)
               {
        int userId; std::string username;
        if (!GetCurrentUser(req, userId, username)) { SendUnauthorized(res); return; }
        try {
            int id = std::stoi(req.matches[1].str());
            bool ok = DeleteWorkspace(userId, id);
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

    std::cout << "== PHIEN BAN 3.0 - CHATBOX, KHUYEN KHICH & THEO DOI TIEN TRINH LAM VIEC ==\n";
    if (ANTHROPIC_API_KEY.empty())
        std::cout << "   (Chua cau hinh ANTHROPIC_API_KEY trong .env -> chatbox se dung cau tra loi mau)\n";
    std::cout << "Server dang chay tai http://localhost:8080 (va http://127.0.0.1:8080)\n";
    svr.listen("0.0.0.0", 8080);
    return 0;
}

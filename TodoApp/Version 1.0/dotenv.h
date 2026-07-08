// dotenv.h — thư viện header-only, đơn giản, không phụ thuộc ngoài,
// dùng để nạp biến cấu hình từ file ".env" cho ứng dụng C++.
//
// Vì C++ không có gói "dotenv" chính thức/chuẩn hoá như Node.js (dotenv)
// hay Python (python-dotenv), file này cài đặt lại đúng hành vi cốt lõi
// của dotenv:
//   - Đọc file .env (mỗi dòng dạng KEY=VALUE)
//   - Bỏ qua dòng trống và dòng comment (bắt đầu bằng '#')
//   - Cho phép giá trị có dấu nháy đơn/kép bao quanh
//   - KHÔNG ghi đè biến môi trường đã tồn tại sẵn của hệ điều hành
//     (giống hành vi mặc định của thư viện dotenv gốc)
//
// Cách dùng:
//   dotenv::load(".env");                       // nạp file .env (nếu có)
//   std::string user = dotenv::get("DB_USER", "root"); // lấy giá trị, có default
//
#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <cstdlib>
#include <algorithm>

namespace dotenv
{
    namespace detail
    {
        inline std::unordered_map<std::string, std::string> &store()
        {
            static std::unordered_map<std::string, std::string> data;
            return data;
        }

        inline std::string trim(const std::string &s)
        {
            size_t start = s.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
                return "";
            size_t end = s.find_last_not_of(" \t\r\n");
            return s.substr(start, end - start + 1);
        }

        inline std::string stripQuotes(const std::string &s)
        {
            if (s.size() >= 2)
            {
                char first = s.front();
                char last = s.back();
                if ((first == '"' && last == '"') || (first == '\'' && last == '\''))
                    return s.substr(1, s.size() - 2);
            }
            return s;
        }
    }

    // Nạp file .env (mặc định là ".env" ở thư mục hiện hành).
    // Trả về true nếu đọc được file, false nếu không tìm thấy.
    inline bool load(const std::string &path = ".env")
    {
        std::ifstream file(path);
        if (!file.is_open())
            return false;

        std::string line;
        while (std::getline(file, line))
        {
            std::string trimmed = detail::trim(line);
            if (trimmed.empty() || trimmed[0] == '#')
                continue;

            size_t eq = trimmed.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = detail::trim(trimmed.substr(0, eq));
            std::string value = detail::trim(trimmed.substr(eq + 1));
            value = detail::stripQuotes(value);

            if (key.empty())
                continue;

            detail::store()[key] = value;
        }
        return true;
    }

    // Lấy giá trị cấu hình theo thứ tự ưu tiên:
    //   1) Biến môi trường thật của hệ điều hành (nếu có) — cho phép override lúc deploy
    //   2) Giá trị đọc được từ file .env
    //   3) Giá trị mặc định truyền vào (defaultValue)
    inline std::string get(const std::string &key, const std::string &defaultValue = "")
    {
        if (const char *envVal = std::getenv(key.c_str()))
            return std::string(envVal);

        auto &data = detail::store();
        auto it = data.find(key);
        if (it != data.end())
            return it->second;

        return defaultValue;
    }
}

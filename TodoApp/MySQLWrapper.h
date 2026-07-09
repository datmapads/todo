#pragma once

#include "C:/Program Files/MySQL/MySQL Server 8.0/include/mysql.h"
#include <string>

// typedef
typedef int (__stdcall *mysql_query_t)(MYSQL *, const char *);
typedef void (__stdcall *mysql_close_t)(MYSQL *);
typedef const char *(__stdcall *mysql_error_t)(MYSQL *);
typedef MYSQL_RES *(__stdcall *mysql_store_result_t)(MYSQL *);
typedef MYSQL_ROW (__stdcall *mysql_fetch_row_t)(MYSQL_RES *);
typedef void (__stdcall *mysql_free_result_t)(MYSQL_RES *);
typedef unsigned long (__stdcall *mysql_real_escape_string_t)(
    MYSQL *, char *, const char *, unsigned long);

// extern
extern mysql_query_t p_mysql_query;
extern mysql_close_t p_mysql_close;
extern mysql_error_t p_mysql_error;
extern mysql_store_result_t p_mysql_store_result;
extern mysql_fetch_row_t p_mysql_fetch_row;
extern mysql_free_result_t p_mysql_free_result;
extern mysql_real_escape_string_t p_mysql_real_escape_string;

// các hàm nằm trong main.cpp
MYSQL* ConnectDB();
std::string Escape(MYSQL* conn, const std::string& input);
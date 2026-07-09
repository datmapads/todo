#include "TaskService.h"

#include <mysql.h>
#include <iostream>
#include "..\MySQLWrapper.h"
extern MYSQL* ConnectDB();

extern std::string Escape(
    MYSQL* conn,
    const std::string& input
);

extern mysql_query_t p_mysql_query;
extern mysql_store_result_t p_mysql_store_result;
extern mysql_fetch_row_t p_mysql_fetch_row;
extern mysql_free_result_t p_mysql_free_result;
extern mysql_close_t p_mysql_close;
extern mysql_error_t p_mysql_error;

json TaskService::getTasks()
{
    json arr = json::array();

    MYSQL* conn = ConnectDB();

    if(!conn)
        return arr;

    const char* sql =
    "SELECT "
    "id,"
    "name,"
    "category,"
    "priority,"
    "due_date,"
    "completed,"
    "rating "
    "FROM tasks "
    "ORDER BY id DESC";

    if(p_mysql_query(conn, sql))
    {
        std::cerr
            << "[GetTasks] "
            << p_mysql_error(conn)
            << std::endl;

        p_mysql_close(conn);

        return arr;
    }

    MYSQL_RES* res =
        p_mysql_store_result(conn);

    if(res)
    {
        MYSQL_ROW row;

        while(
            (row =
                p_mysql_fetch_row(res))
        )
        {
           arr.push_back({
                        {"id",
                            std::stoi(row[0])},
                        {"name",
                            row[1] ? row[1] : ""},
                        {"category",
                            row[2] ? row[2] : ""},
                        {"priority",
                            row[3] ? row[3] : ""},
                        {"due_date",
                            row[4] ? row[4] : ""},
                        {"completed",
                            row[5] &&
                            std::string(row[5])=="1"},
                        {"rating",
                            row[6]
                            ? std::stoi(row[6])
                            : 0}
                    });
        }

        p_mysql_free_result(res);
    }

    p_mysql_close(conn);

    return arr;
}

bool TaskService::addTask(
    const std::string& name,
    const std::string& category,
    const std::string& priority,
    const std::string& dueDate,
    int rating
)
{
    MYSQL* conn = ConnectDB();

    if(!conn)
        return false;

    std::string dueValue;

    if(dueDate.empty())
    {
        dueValue = "NULL";
    }
    else
    {
        dueValue =
            "'" +
            Escape(conn,dueDate) +
            "'";
    }

    std::string query =
    "INSERT INTO tasks "
    "(name,category,priority,due_date,completed,rating) "
    "VALUES ('" +
    Escape(conn,name) + "','" +
    Escape(conn,category) + "','" +
    Escape(conn,priority) + "'," +
    dueValue +
    ",0," +
    std::to_string(rating) +
    ")";

    std::cout
        << query
        << std::endl;

    int ret =
        p_mysql_query(
            conn,
            query.c_str()
        );

    if(ret)
    {
        std::cerr
            << "[AddTask] "
            << p_mysql_error(conn)
            << std::endl;
    }

    p_mysql_close(conn);

    return ret == 0;
}

bool TaskService::toggleTask(
    int id
)
{
    MYSQL* conn = ConnectDB();

    if(!conn)
        return false;

    std::string query =
        "UPDATE tasks "
        "SET completed = 1 - completed "
        "WHERE id = " +
        std::to_string(id);

    int ret =
        p_mysql_query(
            conn,
            query.c_str()
        );

    p_mysql_close(conn);

    return ret == 0;
}

bool TaskService::deleteTask(
    int id
)
{
    MYSQL* conn = ConnectDB();

    if(!conn)
        return false;

    std::string query =
        "DELETE FROM tasks "
        "WHERE id = " +
        std::to_string(id);

    int ret =
        p_mysql_query(
            conn,
            query.c_str()
        );

    p_mysql_close(conn);

    return ret == 0;
}

#include "TaskService.h"
#include "../MySQLWrapper.h"

#include <iostream>
#include "mysql.h"

json TaskService::getTasks()
{
    json arr = json::array();

    MYSQL* conn = ConnectDB();

    if (!conn)
        return arr;

    if (
        p_mysql_query(
            conn,
            "SELECT id,name,category,priority,completed "
            "FROM tasks ORDER BY id"
        )
    )
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

    if (res)
    {
        MYSQL_ROW row;

        while ((row = p_mysql_fetch_row(res)))
        {
            arr.push_back({
                {"id", std::stoi(row[0])},
                {"name", row[1] ? row[1] : ""},
                {"category", row[2] ? row[2] : ""},
                {"priority", row[3] ? row[3] : ""},
                {"completed",
                    row[4] &&
                    std::string(row[4]) == "1"}
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
    const std::string& priority
)
{
    MYSQL* conn = ConnectDB();

    if (!conn)
        return false;

    std::string query =
        "INSERT INTO tasks "
        "(name,category,priority,completed) "
        "VALUES ('" +
        Escape(conn, name) + "','" +
        Escape(conn, category) + "','" +
        Escape(conn, priority) +
        "',0)";

    int ret =
        p_mysql_query(
            conn,
            query.c_str()
        );

    if (ret)
    {
        std::cerr
            << "[AddTask] "
            << p_mysql_error(conn)
            << std::endl;
    }

    p_mysql_close(conn);

    return ret == 0;
}

bool TaskService::toggleTask(int id)
{
    MYSQL* conn = ConnectDB();

    if (!conn)
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

    if (ret)
    {
        std::cerr
            << "[ToggleTask] "
            << p_mysql_error(conn)
            << std::endl;
    }

    p_mysql_close(conn);

    return ret == 0;
}

bool TaskService::deleteTask(int id)
{
    MYSQL* conn = ConnectDB();

    if (!conn)
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

    if (ret)
    {
        std::cerr
            << "[DeleteTask] "
            << p_mysql_error(conn)
            << std::endl;
    }

    p_mysql_close(conn);

    return ret == 0;
}

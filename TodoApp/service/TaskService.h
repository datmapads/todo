#pragma once

#include "../json.hpp"
#include <string>

using json = nlohmann::json;

class TaskService
{
public:

    json getTasks();

    bool addTask(
        const std::string& name,
        const std::string& category,
        const std::string& priority
    );

    bool toggleTask(int id);

    bool deleteTask(int id);
};
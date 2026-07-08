-- Phien ban 2.1 - Sua loi hien thi + Muc tieu cong viec
-- Chay file nay trong MySQL (vi du: mysql -u root -p < schema.sql)
-- truoc khi khoi dong server. Khac Phien ban 2.0: them bang goals
-- (muc tieu ca nhan, tach biet voi danh sach cong viec hang ngay).

CREATE DATABASE IF NOT EXISTS todo_db_v21 CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE todo_db_v21;

CREATE TABLE IF NOT EXISTS users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) NOT NULL UNIQUE,
    password_hash CHAR(64) NOT NULL,
    salt CHAR(16) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS tasks (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    name VARCHAR(255) NOT NULL,
    category VARCHAR(50) NOT NULL DEFAULT 'work',
    priority VARCHAR(20) NOT NULL DEFAULT 'p-low',
    completed TINYINT(1) NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_tasks_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS goals (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    title VARCHAR(255) NOT NULL,
    done TINYINT(1) NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_goals_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Khong seed san tai khoan mau: hay dang ky tai khoan dau tien ngay tren giao dien.

-- Phien ban 3.0 - Chatbox, khuyen khich nguoi dung, theo doi tien trinh &
-- khong gian lam viec. Chay file nay trong MySQL (vi du: mysql -u root -p <
-- schema.sql) truoc khi khoi dong server. Khac Phien ban 2.3: them bang
-- work_sessions (theo doi thoi gian lam viec thuc te, ho tro tam dung/tiep
-- tuc qua paused_at/paused_seconds, va ghi chu/file dinh kem), bang
-- workspaces (khong gian lam viec do nguoi dung tu tao), va cot
-- estimated_minutes trong bang tasks de tu dong tinh % tien do.

CREATE DATABASE IF NOT EXISTS todo_db_v30 CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE todo_db_v30;

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
    progress TINYINT UNSIGNED NOT NULL DEFAULT 0,
    due_date DATE NULL,
    estimated_minutes INT UNSIGNED NULL,
    quality_rating TINYINT UNSIGNED NULL,
    completed_at DATETIME NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_tasks_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    CONSTRAINT chk_progress CHECK (progress BETWEEN 0 AND 100),
    CONSTRAINT chk_rating CHECK (quality_rating IS NULL OR quality_rating BETWEEN 1 AND 5)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS goals (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    title VARCHAR(255) NOT NULL,
    done TINYINT(1) NOT NULL DEFAULT 0,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_goals_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS workspaces (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    name VARCHAR(100) NOT NULL,
    color VARCHAR(20) NOT NULL DEFAULT '#5b5bf5',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_workspace_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS work_sessions (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    task_id INT NULL,
    workspace_id INT NULL,
    started_at DATETIME NOT NULL,
    paused_at DATETIME NULL,
    paused_seconds INT UNSIGNED NOT NULL DEFAULT 0,
    ended_at DATETIME NULL,
    duration_seconds INT UNSIGNED NULL,
    note TEXT NULL,
    attachment_name VARCHAR(255) NULL,
    attachment_path VARCHAR(500) NULL,
    CONSTRAINT fk_work_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    CONSTRAINT fk_work_task FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE SET NULL,
    CONSTRAINT fk_work_workspace FOREIGN KEY (workspace_id) REFERENCES workspaces(id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Khong seed san tai khoan mau: hay dang ky tai khoan dau tien ngay tren giao dien.

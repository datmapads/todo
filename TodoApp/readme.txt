# TodoApp - Database Setup

## Yêu cầu

Trước khi chạy project, cần tạo database:

```sql
CREATE DATABASE todo_db;
USE todo_db;
```

---

## Bảng tasks

Project hiện tại phụ thuộc vào bảng `tasks`.

Chạy SQL sau:

```sql
CREATE TABLE tasks
(
    id INT AUTO_INCREMENT PRIMARY KEY,

    name VARCHAR(255) NOT NULL,

    category VARCHAR(50) DEFAULT 'others',

    priority VARCHAR(20) DEFAULT 'p-medium',

    due_date DATE NULL,

    completed BOOLEAN DEFAULT FALSE,

    rating INT DEFAULT 0
);
```

---

## Dữ liệu mẫu

```sql
INSERT INTO tasks
(
    name,
    category,
    priority,
    due_date,
    completed,
    rating
)
VALUES
(
    'Hoc C++',
    'work',
    'p-high',
    '2026-07-15',
    1,
    5
),
(
    'Tap gym',
    'health',
    'p-medium',
    '2026-07-20',
    0,
    0
);
```

---

## Bảng goals (nếu dùng)

```sql
CREATE TABLE goals
(
    id INT AUTO_INCREMENT PRIMARY KEY,

    title VARCHAR(255) NOT NULL,

    completed BOOLEAN DEFAULT FALSE,

    created_at TIMESTAMP
    DEFAULT CURRENT_TIMESTAMP,

    completed_at DATETIME NULL,

    rating INT DEFAULT 0
);
```

---

## File .env

Tạo file `.env`

```env
DB_HOST=127.0.0.1
DB_PORT=3306
DB_USER=root
DB_PASS=YOUR_PASSWORD
DB_NAME=todo_db
```

---

## Kiểm tra nhanh

Mở trình duyệt:

```
http://localhost:8080/tasks
```

Nếu thấy JSON trả về thì kết nối database đã thành công.

---

## Lưu ý

Nếu thiếu bảng `tasks`:

```text
GET /tasks
POST /tasks
PUT /tasks/{id}
DELETE /tasks/{id}
```

sẽ trả lỗi 500.

Nếu thiếu cột `rating`:

```text
Statistics
Weekly Review
```

sẽ hiển thị sai dữ liệu.
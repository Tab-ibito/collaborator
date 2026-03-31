# Declaration

---

## 发起用户注册 / 登录的接口

```javascript
POST / api / login & /api/
register

Request
Body:
{
    username: "string",
        password
:
    "string"
}
```

## 用户注册的response

```javascript
Response
Body:
{
    success: true,
        message
:
    "string",
}
```

## 用户登录的response

```javascript
Response
Body:
{
    success: true,
        message
:
    "string"
}
```

## Websocket 用户端发送的type

| type            | 声明     | 其他数据项           |
|-----------------|--------|-----------------| 
| `join`          | 用户加入   | `username`      |
| `pixel_update`  | 更新像素   | `color` `index` |
| `get_file_list` | 获取文件列表 | 无               |
| `get_user_list` | 获取用户列表 | 无               |
| `create_file`   | 创建文件   | `filename`      |
| `switch_file`   | 切换文件   | `filename`      |
| `undo`          | 撤销     | 无               |

## Websocket 服务器端发送的type

| type                 | 声明     | 其他数据项                                    |
|----------------------|--------|------------------------------------------|
| `error`              | 错误信息   | `message`                                |
| `user_joined`        | 用户加入   | `username` `time`                        |
| `user_left`          | 用户离开   | `username` `time`                        |
| `pixel_update`       | 更新像素   | `username` `color` `index` `time`        |
| `area_update`        | 更新区域   | `username` `color` `index` `time` `size` |
| `canvas`             | 画布     | `canvas`                                 |
| `file_list`          | 文件列表   | `files` `current_working`                |
| `user_list`          | 用户列表   | `users`                                  |
| `user_switched_file` | 用户切换文件 | `username` `filename` `time`             |
| `user_undone`        | 用户撤销操作 | `username` `time` `index` `color`        |

### 错误信息分类

| message                     | 说明        |
|-----------------------------|-----------|
| `invalid_username`          | 无效用户名     |
| `fail_to_create_file`       | 创建文件失败    |
| `fail_to_switch_file`       | 切换文件失败    |
| `fail_to_open_file`         | 打开文件失败    |
| `file_not_found`            | 文件未找到     |
| `fail_to_update_pixel`      | 更新像素失败    |
| `fail_to_undo`              | 撤销操作失败    |
| `invalid_pixel_update_data` | 无效的像素更新数据 |
| `filename_required`         | 缺少文件名     |
| `unknown_message_type`      | 未知消息类型    |

## 文件系统设计

```
/database
    /canvas
        canvas_state # 默认
        file1
        file2
        ...
    /logs
        canvas_state.log # 默认
        file1.log
        file2.log
        ...
```
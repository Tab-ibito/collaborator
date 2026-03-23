# Declaration

---

## 发起用户注册 / 登录的接口

```javascript
POST /api/login & /api/register

Request Body:
{
  username: "string",
  password: "string"
}
```

## 用户注册的response

```javascript
Response Body:
{
    success: true,
    message: "string",
}
```

## 用户登录的response

```javascript
Response Body:
{
    success: true,
    message: "string",
}
```

## Websocket 用户端发送的type
| type           | 声明   | 其他数据项                     |
|----------------|------|---------------------------| 
| `join`         | 用户加入 | `username`                |
| `pixel_update` | 更新像素 | `color` `index` |

## Websocket 服务器端发送的type
| type               | 声明       | 其他数据项                             |
|--------------------|----------|-----------------------------------|
| `invalid_username` | 非法用户名    | 无                                |
| `user_joined`      | 用户加入     | `username` `time`                 |
| `user_left`        | 用户离开     | `username` `time`                 |
| `pixel_update`     | 更新像素     | `username` `color` `index` `time` |
| `canvas`           | 画布（给新用户） | `canvas`                          |
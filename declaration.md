# Declaration

---

## 发起用户注册 / 登录的接口

```javascript
POST /api/login & /api/register

Request Body:
{
  "username": "string",
  "password": "string"
}
```

## 用户注册的response

```javascript
Response Body:
{
    "success": true,
    "message": "string",
}
```

## 用户登录的response

```javascript
Response Body:
{
    "success": true,
    "message": "string",
}
```
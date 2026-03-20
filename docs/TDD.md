# Technical Design Document

---

## 前期技术难点分析

* 数据库的管理
* 服务端与客户端的网络连接和数据传输
* 多线程之间的编辑数据读取与权限处理，如何对用户反馈结果
* txt文档编辑本身的困难性

> :question: 用户A与用户B同时按下了backspace键，他们是不是想要删同一个字符？
> :confused: 用户A输入了a，用户B输入了b，用户C却按下了delete，他们意见不合应该听谁的还是都听？

因此先从 **维护一个简单的4*4像素画** 的规则开始搭建。

## Problem 1 detected 2026.3.19 solved 2026.3.20

* 我在 main.cpp 挂了一个 `http://localhost:8080/api/login` 的路由跑
* 在js文件中尝试直接使用 `/api/login` 使用POST发包，得到 `404 Not Found`
* 我改成 `http://localhost:8080/api/login`，得到报错

`Access to fetch at 'http://localhost:8080/api/login' from origin 'http://localhost:63342' has been blocked by CORS policy: Response to preflight request doesn't pass access control check: No 'Access-Control-Allow-Origin' header is present on the requested resource.`

原因：
* 虽然传数据是在HTTP协议基础上进行，但是寻址需要**手动固定socket地址**
* 如果在两个不同端口间用POST传包，可能触发 CORS policy 的报错

尝试声明
```cpp
    crow::App<crow::CORSHandler> app;
    auto& cors = app.get_middleware<crow::CORSHandler>();
    cors.global()
        .headers("Origin", "Content-Type", "Accept")
        .methods("POST"_method, "GET"_method, "OPTIONS"_method)
        .origin("*");

```
发现**兼容性问题**，编译失败。

手动配置OPTIONS检测依然无法解决问题。

解决方案：添加
```C
#include "crow/middlewares/cors.h"
```
的显式依赖。

## Feat: A Simple Userdata Management System 2026.3.20

建立了一个简单的数据库，给用户分配uid，提供简单的通过 username 和 password 的注册 / 登录功能。

## Part II WebSocket 和稳定长期链接

在初期的过程中，我们并不过度涉及到线程操作，因为原理上只需要**给服务器发送一个 POST 请求**，然后收到请求之后**完成登录**即可。

但接下来，我们面临**多用户编辑同一个文档，长期和服务器联系**的问题，我们需要使用Websocket。

从用户更改的角度出发：



## Feat: Sync Update 2026.3.20

实现了用户端和用户端之间的像素画传输和更改。


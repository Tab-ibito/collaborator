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

## Feat: Sync Update 2026.3.20

实现了用户端和用户端之间的像素画传输和更改。

还需要实现：
* userdata绑定
* 文件时间戳
* 自动保存
* 撤销
* 多文件切换

## Feat: More Robust Authentication & Saving / Loading in Server 2026.3.23

* 验证?username=1的有效性，自动踢掉invalid user
* 服务器加载时从磁盘文件加载画布，关掉之后自动保存进度到磁盘，服务器内部维护一张画布缓存
* 完成了接口的规范定义

注意到原生的一些特性：
* `switch` 里面分支直接定义变量会编译爆错
* `app.run` 之后进程被 Event Loop 阻塞


接下来还需要完成的主要功能：
* 时间戳与自动保存
* 操作撤销与回退
* 用户数据进一步统计
* 多文件读取操作

## Feat: New Features 2026.3.25

实现了新的功能：

* `create_file` 创建新文件
* `switch_file` 切换当前工作文件
* `undo` 使用deque实现撤销操作（最大量为10）
* `get_file_list` 查看文件列表
* `get_user_list` 查看在线用户
* `opHistory` 栏目查看操作历史
* 完善了相关接口的定义环境

## Debug: Stress Test & Draw Test 2026.3.25

通过 `test/stress_test.py` 测试50个用户的注册登录和随意修改像素行为，结果成功

通过 `test/draw_test.py` 测试60个用户写作完成2张 `heart` 和 `oak_plunk` 图片，结果出现**内容错乱**的问题

## Problem 2 detected 2026.3.25

> 通过 `test/draw_test.py` 测试60个用户写作完成2张 `heart` 和 `oak_plunk` 图片，结果出现**内容错乱**的问题

原因分析：虽然通过了 `switch_file` 方法切换文件，但是由于异步性我们**没办法确定**各个更改像素的行为是发生在 `switch_file` 的之前还是之后，从而导致时间的错乱。

解决方法1：
* `switch_file` 的json发包中加上 `filename` 属性，以确认**修改的文件对象是否正确**
* 服务器端如果收到了无法对应的 `filename`，将对应操作记录缓存，等到切换到对应文件之后还原
* 服务器关闭之后自动执行还原程序，清空操作记录

此方案问题：存储对空间的消耗可能很大，且依然可能造成数据丢失，或者需要依赖**服务器关闭后自动还原**的情况，逻辑复杂

解决方法2：
计划**维护分配不同的房间**。
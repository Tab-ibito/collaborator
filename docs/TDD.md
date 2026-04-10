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

## Problem 2 detected 2026.3.25 solved 2026.3.26

> 通过 `test/draw_test.py` 测试60个用户写作完成2张 `heart` 和 `oak_plunk` 图片，结果出现**内容错乱**的问题

原因分析：虽然通过了 `switch_file` 方法切换文件，但是由于异步性我们**没办法确定**各个更改像素的行为是发生在 `switch_file` 的之前还是之后，从而导致时间的错乱。

解决方法1：
* `switch_file` 的json发包中加上 `filename` 属性，以确认**修改的文件对象是否正确**
* 服务器端如果收到了无法对应的 `filename`，将对应操作记录缓存，等到切换到对应文件之后还原
* 服务器关闭之后自动执行还原程序，清空操作记录

此方案问题：存储对空间的消耗可能很大，且依然可能造成数据丢失，或者需要依赖**服务器关闭后自动还原**的情况，逻辑复杂

解决方法2：
计划**维护分配不同的房间**，通过 `lobby_mtx` 和每个房间单独的 `room_mtx` 维护。  
注意到 `mutex` 不能被复制，整个结构体需要通过**指针访问**。

```C++
// 房间管理
struct CanvasRoom {
  std::vector<std::string> canvas;
  std::unordered_set<crow::websocket::connection *> connections;
  std::mutex room_mtx;
  std::deque<PixelChange> edit_history;

  CanvasRoom() : canvas(CANVAS_SIZE, "#FFFFFF") {} // 初始化画布为白色
};

static std::mutex lobby_mtx;
static std::unordered_map<std::string, std::unique_ptr<CanvasRoom>>
    active_rooms;
```

我们可以做到
* 允许不同用户同时编辑不同文件
* 部分广播以room为单位进行

目前缺陷：
* 还没有完善空房间的**内存释放**处理，存在**内存泄漏**问题隐患
* 部分操作依旧依赖 `O(n)` 遍历进行，完全可以通过哈希表优化，例如 `<key, val> = <&conn, filename>`
  * 因为我们拿到手的是 `&conn`，完全不应该去依赖低效遍历找filename
  * 且**遍历过程本身与文件操作指令无关**，却极容易引发**锁的权限问题**

因此我们接下来围绕**读写权限和性能优化**展开。

## Update: A Better Search 2026.3.27

改进了遍历逻辑，引入了哈希表建立从 `&conn` 到 `filename` 的映射。

## Problem 3 detected 2026.3.25 solved 2026.3.26

之前弃用的逻辑是在所有事情结束之后清理，发现有未能解决的神秘问题，遂改逻辑：**用户退出时候自动检测删除**。

在引入自动回收删除的时候，又遇到了一些**神秘问题**。

### 首先在引入 `switch_file` 的时候发现了会报**段错误**或者是**死锁**。

* 解决1：将 `lobby_mtx` 在整个 `switch_file` 过程中全覆盖。

  * 在此之前尝试过加一个**保护删除操作的大锁**，发现作用有限遂弃用。

* 解决2：优化 `switch_file` 的部分操作逻辑，包括但不限于一些数据删除使用暂存的**逻辑顺序优化**。

  * 这里又引入了另一个更加神秘的错误：`switch_file` 目标对象是原文件时会莫名其妙爆炸:
```
(2026-03-27 13:17:29) [ERROR   ] Worker Crash: An uncaught exception occurred: vector::_M_default_append
```

### 但我们尝试把同样的代码赋值给 .onclose() 的时候，又出现了神秘错误。

我们先后做了以下改进方法，但这里的神秘错误很可能是多因素引发的：
* `ctime` 并非线程安全，改用线程安全的time库，由于懒我们采用了 `time_mtx` 手动加锁。
* 跑的 `draw_test.py` 测试自己有一点神秘的异步性问题。
* `switch_file` 的检测特判是不是自身，避免神秘逻辑问题。
* `old_room_ptr != active_rooms["canvas_state"].get()` 由于 `unordered_map` 自身线程不安全，哈希表内部结构不稳定性导致的并发问题，我们应该写的是 `old_filename != "canvas_state"` 更保险

经过一系列调整和debug，修复了相关bug，并添加了退出时**保存特定文件到磁盘**的机制。

同时也预留有**缓存结构**实现的可能性。

**线程和并发模块**的问题基本解决，接下来我们的任务是：
* 服务端的文件管理，二进制文件的编码和.png格式的导出支持
* 网络端如何udp广播，或者跨局域网进行nginx认证
* .txt文档的维护，参考借鉴CodiMD的做法

**目前总代码量1.4k行。**

## Feat: Combining Snapshots And Event Tracing 2026.3.30

更新了后端的文件管理方式，现在更新后支持 snapshot + 追加时间戳的形式进行文件的读取和修改。

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

优势在于通过追加的方式实现向**页表内存**的实时追加转写，追加方式方法效率高；

而经过每若干项操作后**自动存储一次快照到磁盘上**。

以时间戳为基础我们可以尝试*尝试重写撤销重做逻辑*，抑或以实际时间为标准**建立自动保存快照副本**。

## Feat: Square Brush 2026.4.1

现在支持**正方形区域**范围内的笔刷与撤销。

## Update: Bettering Architecture 2026.4.2

更新了后端的数据组织方式方法。现在 `.log` 文件中的存储采用了与服务器发包**完全相同的** json 字符串格式。

例如：
```json
{"type":"pixel_update","color":"#999","filename":"canvas_state","index":86,"username":"1","timestamp":"Thu Apr  2 15:08:12 2026\n"}
```

目前暂时支持 `pixel_update`, `square_update`, `user_undone` (aka `multipixel_update` ) 这些子项，  
它们都可以**不经特殊处理** 
* 直接通过 `event_logger` 的 `create_pixel_painted_event` 等一系列方法生成，
* 直接存入 `.log`，直存直取。
* 亦可**直接发包**。

优点：
* 可拓展性强，在基础的 `pixel_update` 和 `multipixel_update` 上我们可以拓展出**直线，椭圆**等，并通过**数学方程**表达确定。
* 尽量减少了数据存读的压力。

接下来我们可以加入**圆 / 直线**

## Feat: Document Metadata Storage, Customize Size And Asynchronous Broadcast Decoupling 2026.4.3

* 将 `users.db` 更改为 `server.db`，加入一张  `canvas_metadata` 的表格，可以**维护画布的属性参数**，例如创建日期，长宽尺寸
  * 配合地升级了CanvasRoom，做了 public 与 private 的权限区分。
* 配合升级了前端，以此支持用户自定义画布尺寸。
* 异步消息发送（例如房间为单位，或者全局群发）通过**独立线程**单独维护
  * 严格保证时序性，同时防止线程被消息收发堵塞

## Fix: Added Room Mutex 2026.4.3

在塞独立进程信箱的整个外面应该用 `room_mtx` 覆盖，严格的保证了**对应的时序性**。

同时**优化**了各个mutex锁的管理情况，修复了死锁。

*代码习惯：如果有A和B两个锁，务必保证所有A和B满足严格的先拿一个再拿另一个的习惯*  
*还锁顺序不会影响死锁，但是会影响性能*

但是当前程序仍然存在**TOCTOU**问题需要解决。

暂时解决了三处，主要在于 `lobby_mtx` 与 `room_mtx` 的时序微调。

还需要解决在 `switch_file` 中对 `lobby_mtx` 的长期占用。

## Feat: Added Line Painter 2026.4.6

前后端都加入了支持直线绘制的工具，基于bresenham's line algorithm实现。

## Update: Binary Network 2026.4.7

将所有通过网络传输的 `canvas` 改成了 `canvas_incoming` 和二进制字节流，节约包体空间，防止内存爆炸。

## Update: Speed Optimization 2026.4.9

优化了

* 前端的**渲染机制**，不再使用笨重的很多个 `<div>` ，改为使用 `<canvas>` 渲染。
* 增加了 `scale` 选项设置，可以在前端设置放大比例
* 后端的画布文件也改为了**二进制字节流**处理

## Feat: Image Upload 2026.4.10

支持用户端直接上传图片给服务端。通过二进制字节流实现。

二进制字节流前四个字节分别存储了图片的宽和高，后续字节存储了每个像素的RGBA值，编写解析函数进行写入处理。

## Feat: Frontend Upgrade 2026.4.10

主要优化了前端的表现，包括自适应缩放，各个按键之间的用户的操作逻辑等。

## Feat: Circle Drawing 2026.4.10

支持了画圆功能。

结项之前需要实现的**最后两个features**:
* udp局域网内广播，连接功能
* 服务端程序的鲁棒性，异常处理

目前代码量（未大规模优化）为 3.2k lines。
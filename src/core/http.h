// 宁州一梦 · C++ 版 — 极简 HTTP 服务
//
// 为什么不引第三方库：整个项目到现在没有任何外部依赖，
// 而这里需要的只是一个本地单线程的静态文件服务 + JSON 接口。
// 直接用 Winsock / POSIX socket 写，几百行够了，你也能直接读懂。
//
// 设计：
//   - 单线程循环处理（本地单人游戏，不需要并发）
//   - 只监听 127.0.0.1，不对外暴露
//   - GET  /             -> 返回前端 index.html
//   - GET  /styles.css   -> 静态文件
//   - POST /api/<动作>    -> 调用 core 里的规则函数，返回 JSON
#pragma once

#include <functional>
#include <string>

namespace nz::http {

// 请求
struct Request {
    std::string method;   // GET / POST
    std::string path;     // /api/state
    std::string body;     // POST 的请求体（JSON 文本）
    std::string query;    // ? 后面的部分
};

// 响应
struct Response {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
};

// 路由函数：拿到请求，返回响应
using Handler = std::function<Response(const Request&)>;

// 启动服务并阻塞。返回进程退出码。
//   port      监听端口
//   staticDir 静态文件根目录（前端 index.html 所在处）
//   api       /api/ 开头的请求交给它
int serve(int port, const std::string& staticDir, Handler api);

} // namespace nz::http

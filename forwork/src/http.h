#pragma once

#include <map>
#include <string>

// HTTP 增量解析的结果
enum class ParseResult {
    NeedMore,  // 数据不足，需要继续读
    Ok,        // 已得到一个完整请求
    Error,     // 格式错误
};

// 解析后的 HTTP 请求
struct HttpRequest {
    std::string method;    // GET / POST / ...
    std::string path;      // /index.html
    std::string version;   // HTTP/1.1 或 HTTP/1.0
    std::map<std::string, std::string> headers;  // 键统一转小写
    std::string body;

    // 增量解析：判断 [data, data+len) 里是否已有完整请求。
    // 完整时返回 Ok 并设置 consumed；不足返回 NeedMore；错误返回 Error。
    static ParseResult tryParse(const char* data, size_t len,
                                HttpRequest& req, size_t& consumed);

    std::string header(const std::string& name) const;  // 大小写不敏感
    bool keepAlive() const;                             // 是否保持连接
};

// 用于构造 HTTP 响应
struct HttpResponse {
    int status = 200;
    bool keepAlive = false;   // 是否保持连接（决定 Connection 头）
    std::map<std::string, std::string> headers;
    std::string body;

    std::string toString() const;

    static HttpResponse makeText(int status, const std::string& text);
    static HttpResponse makeHtml(int status, const std::string& html);
    static HttpResponse make404();
};

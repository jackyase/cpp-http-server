#include "http.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    auto isSpace = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    size_t b = 0, e = s.size();
    while (b < e && isSpace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && isSpace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

}  // namespace

std::string HttpRequest::header(const std::string& name) const {
    auto it = headers.find(toLower(name));
    return it == headers.end() ? std::string{} : it->second;
}

bool HttpRequest::keepAlive() const {
    std::string c = toLower(header("connection"));
    if (version == "HTTP/1.1") {
        return c != "close";      // HTTP/1.1 默认 keep-alive
    }
    return c == "keep-alive";     // HTTP/1.0 默认 close
}

ParseResult HttpRequest::tryParse(const char* data, size_t len,
                                  HttpRequest& req, size_t& consumed) {
    std::string_view buf(data, len);

    // 1. 找到请求头结束标记 \r\n\r\n
    size_t sep = buf.find("\r\n\r\n");
    if (sep == std::string_view::npos) return ParseResult::NeedMore;

    std::string_view head = buf.substr(0, sep);
    HttpRequest tmp;

    // 2. 请求行 "GET /path HTTP/1.1"
    size_t lineEnd = head.find("\r\n");
    std::string_view reqLine = (lineEnd == std::string_view::npos)
                                   ? head : head.substr(0, lineEnd);
    {
        std::istringstream ls{std::string(reqLine)};
        ls >> tmp.method >> tmp.path >> tmp.version;
        if (tmp.method.empty() || tmp.path.empty()) return ParseResult::Error;
    }

    // 3. 逐行解析 header
    size_t start = (lineEnd == std::string_view::npos) ? head.size() : lineEnd + 2;
    while (start < head.size()) {
        size_t le = head.find("\r\n", start);
        std::string_view line = (le == std::string_view::npos)
                                    ? head.substr(start)
                                    : head.substr(start, le - start);
        if (line.empty()) break;
        size_t colon = line.find(':');
        if (colon == std::string_view::npos) return ParseResult::Error;
        tmp.headers[toLower(std::string(line.substr(0, colon)))] =
            trim(std::string(line.substr(colon + 1)));
        if (le == std::string_view::npos) break;
        start = le + 2;
    }

    // 4. 根据 Content-Length 计算 body 长度
    size_t contentLen = 0;
    std::string lenStr = tmp.header("content-length");
    if (!lenStr.empty()) {
        try {
            contentLen = std::stoul(lenStr);
        } catch (...) {
            return ParseResult::Error;
        }
    }

    size_t total = sep + 4 + contentLen;
    if (buf.size() < total) return ParseResult::NeedMore;

    tmp.body = std::string(buf.substr(sep + 4, contentLen));
    req = std::move(tmp);
    consumed = total;
    return ParseResult::Ok;
}

std::string HttpResponse::toString() const {
    static const std::map<int, std::string> kReason = {
        {200, "OK"},
        {400, "Bad Request"},
        {404, "Not Found"},
        {413, "Payload Too Large"},
        {500, "Internal Server Error"},
    };

    std::string reason = "OK";
    auto it = kReason.find(status);
    if (it != kReason.end()) reason = it->second;

    // 拷贝一份 header，补上默认字段（保持 const 正确性）
    auto hdrs = headers;
    auto setIfMissing = [&](const std::string& k, const std::string& v) {
        if (hdrs.find(k) == hdrs.end()) hdrs[k] = v;
    };
    setIfMissing("Content-Length", std::to_string(body.size()));
    setIfMissing("Content-Type", "text/plain; charset=utf-8");
    setIfMissing("Connection", keepAlive ? "keep-alive" : "close");

    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << reason << "\r\n";
    for (const auto& [k, v] : hdrs) {
        out << k << ": " << v << "\r\n";
    }
    out << "\r\n" << body;
    return out.str();
}

HttpResponse HttpResponse::makeText(int status, const std::string& text) {
    HttpResponse r;
    r.status = status;
    r.body = text;
    r.headers["Content-Type"] = "text/plain; charset=utf-8";
    return r;
}

HttpResponse HttpResponse::makeHtml(int status, const std::string& html) {
    HttpResponse r;
    r.status = status;
    r.body = html;
    r.headers["Content-Type"] = "text/html; charset=utf-8";
    return r;
}

HttpResponse HttpResponse::make404() {
    return makeHtml(404, "<h1>404 Not Found</h1>");
}

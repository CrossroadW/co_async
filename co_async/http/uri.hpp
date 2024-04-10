#pragma once /*{export module co_async:http.uri;}*/

#include <cmake/clang_std_modules_source/std.hpp>/*{import std;}*/
#include <co_async/utils/simple_map.hpp>         /*{import :utils.simple_map;}*/
#include <co_async/utils/string_utils.hpp>/*{import :utils.string_utils;}*/

namespace co_async {

struct URIParams : SimpleMap<std::string, std::string> {
    using SimpleMap<std::string, std::string>::SimpleMap;
};

struct URI {
    std::string path;
    URIParams params;

private:
    static std::uint8_t fromHex(char c) {
        if ('0' <= c && c <= '9')
            return c - '0';
        else if ('A' <= c && c <= 'F')
            return c - 'A' + 10;
        else [[unlikely]]
            return 0;
    }

    static bool isCharUrlSafe(char c) {
        if ('0' <= c && c <= '9') {
            return true;
        }
        if ('a' <= c && c <= 'z') {
            return true;
        }
        if ('A' <= c && c <= 'Z') {
            return true;
        }
        if (c == '-' || c == '_' || c == '.') {
            return true;
        }
        return false;
    }

public:
    static void urlDecode(std::string &r, std::string_view s) {
        std::size_t b = 0;
        while (true) {
            auto i = s.find('%', b);
            if (i == std::string_view::npos || i + 3 > s.size()) {
                r.append(s.data() + b, s.data() + s.size());
                break;
            }
            r.append(s.data() + b, s.data() + i);
            char c1 = s[i + 1];
            char c2 = s[i + 2];
            r.push_back((fromHex(c1) << 4) | fromHex(c2));
            b = i + 3;
        }
    }

    static std::string urlDecode(std::string_view s) {
        std::string r;
        urlDecode(r, s);
        return r;
    }

    static void urlEncode(std::string &r, std::string_view s) {
        static constexpr char lut[] = "0123456789ABCDEF";
        for (char c: s) {
            if (isCharUrlSafe(c)) {
                r.push_back(c);
            } else {
                r.push_back('%');
                r.push_back(lut[static_cast<std::uint8_t>(c) >> 4]);
                r.push_back(lut[static_cast<std::uint8_t>(c) & 0xF]);
            }
        }
    }

    static std::string urlEncode(std::string_view s) {
        std::string r;
        urlEncode(r, s);
        return r;
    }

    static URI parse(std::string_view uri) {
        auto path = uri;
        URIParams params;

        auto i = uri.find('?');
        if (i != std::string_view::npos) {
            path = uri.substr(0, i);
            do {
                uri.remove_prefix(i);
                i = uri.find('&');
                auto pair = uri.substr(0, i);
                auto m = pair.find('=');
                if (m != std::string_view::npos) {
                    auto k = pair.substr(0, m);
                    auto v = pair.substr(m + 1);
                    params.insert_or_assign(std::string(k), urlDecode(v));
                }
            } while (i != std::string_view::npos);
        }

        return {std::string(path), std::move(params)};
    }

    void dump(std::string &r) const {
        r.append(path);
        char queryChar = '?';
        for (auto &[k, v]: params) {
            r.push_back(queryChar);
            urlEncode(r, k);
            r.push_back('=');
            urlEncode(r, v);
            queryChar = '&';
        }
    }

    std::string dump() const {
        std::string r;
        dump(r);
        return r;
    }
};

} // namespace co_async

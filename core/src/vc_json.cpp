/*
 * Copyright (c) 2026 Veriland Consulting Ltd.
 *
 * SPDX-License-Identifier: FSL-1.1-Apache-2.0
 *
 * Licensed under the Functional Source License, Version 1.1, Apache 2.0 Future
 * License. See the LICENSE file in the project root for the full terms.
 */

#include "vc/vc_json.h"

#include <cmath>
#include <cstdio>

namespace vc
{
    /* ------------------------------------------------------------------ */
    /* Construction                                                        */
    /* ------------------------------------------------------------------ */

    Json Json::boolean(bool b)
    {
        Json j;
        j.type_ = Type::Bool;
        j.bool_ = b;
        return j;
    }

    Json Json::number(double n)
    {
        Json j;
        j.type_ = Type::Number;
        j.num_ = n;
        return j;
    }

    Json Json::array()
    {
        Json j;
        j.type_ = Type::Array;
        return j;
    }

    Json Json::object()
    {
        Json j;
        j.type_ = Type::Object;
        return j;
    }

    Json Json::string(std::string s)
    {
        Json j;
        j.type_ = Type::String;
        j.str_ = std::move(s);
        return j;
    }

    /* ------------------------------------------------------------------ */
    /* Access                                                              */
    /* ------------------------------------------------------------------ */

    bool Json::as_bool(bool def) const noexcept
    {
        return type_ == Type::Bool ? bool_ : def;
    }

    double Json::as_number(double def) const noexcept
    {
        return type_ == Type::Number ? num_ : def;
    }

    std::string_view Json::as_string(std::string_view def) const noexcept
    {
        return type_ == Type::String ? std::string_view(str_) : def;
    }

    namespace
    {
        bool iequals(std::string_view a, std::string_view b) noexcept
        {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); i++)
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }
    } // namespace

    const Json* Json::find(std::string_view key) const noexcept
    {
        if (type_ != Type::Object) return nullptr;
        for (const auto& [k, v] : obj_)
            if (k == key) return &v;
        return nullptr;
    }

    const Json* Json::find_ci(std::string_view key) const noexcept
    {
        if (type_ != Type::Object) return nullptr;
        for (const auto& [k, v] : obj_)
            if (iequals(k, key)) return &v;
        return nullptr;
    }

    std::string_view Json::get_str(std::string_view key, std::string_view def) const noexcept
    {
        const Json* v = find_ci(key);
        return (v && v->is_string()) ? std::string_view(v->str_) : def;
    }

    double Json::get_num(std::string_view key, double def) const noexcept
    {
        const Json* v = find_ci(key);
        return (v && v->is_number()) ? v->num_ : def;
    }

    bool Json::get_bool(std::string_view key, bool def) const noexcept
    {
        const Json* v = find_ci(key);
        return (v && v->is_bool()) ? v->bool_ : def;
    }

    std::size_t Json::size() const noexcept
    {
        if (type_ == Type::Array) return arr_.size();
        if (type_ == Type::Object) return obj_.size();
        return 0;
    }

    Json& Json::set(std::string key, Json value)
    {
        obj_.emplace_back(std::move(key), std::move(value));
        return *this;
    }

    Json& Json::add(Json value)
    {
        arr_.push_back(std::move(value));
        return *this;
    }

    /* ------------------------------------------------------------------ */
    /* Parser                                                              */
    /* ------------------------------------------------------------------ */

    namespace
    {
        constexpr int kMaxDepth = 128;

        struct Parser
        {
            const char* p;
            const char* end;
            int depth = 0;

            void skip_ws()
            {
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
                    p++;
            }
        };

        void append_utf8(std::string& b, std::uint32_t cp)
        {
            if (cp < 0x80)
            {
                b += static_cast<char>(cp);
            }
            else if (cp < 0x800)
            {
                b += static_cast<char>(0xC0 | (cp >> 6));
                b += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                b += static_cast<char>(0xE0 | (cp >> 12));
                b += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                b += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                b += static_cast<char>(0xF0 | (cp >> 18));
                b += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                b += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                b += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        bool parse_hex4(Parser& ps, std::uint32_t& out)
        {
            if (ps.end - ps.p < 4) return false;
            std::uint32_t v = 0;
            for (int i = 0; i < 4; i++)
            {
                char c = ps.p[i];
                v <<= 4;
                if (c >= '0' && c <= '9')
                    v |= static_cast<std::uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f')
                    v |= static_cast<std::uint32_t>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F')
                    v |= static_cast<std::uint32_t>(c - 'A' + 10);
                else
                    return false;
            }
            ps.p += 4;
            out = v;
            return true;
        }

        /* Parse a JSON string literal (cursor on opening quote). */
        bool parse_string_raw(Parser& ps, std::string& out)
        {
            if (ps.p >= ps.end || *ps.p != '"') return false;
            ps.p++;
            out.clear();
            while (ps.p < ps.end)
            {
                unsigned char c = static_cast<unsigned char>(*ps.p);
                if (c == '"')
                {
                    ps.p++;
                    return true;
                }
                if (c == '\\')
                {
                    ps.p++;
                    if (ps.p >= ps.end) return false;
                    char e = *ps.p++;
                    switch (e)
                    {
                    case '"':
                        out += '"';
                        break;
                    case '\\':
                        out += '\\';
                        break;
                    case '/':
                        out += '/';
                        break;
                    case 'b':
                        out += '\b';
                        break;
                    case 'f':
                        out += '\f';
                        break;
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case 'u':
                    {
                        std::uint32_t cp;
                        if (!parse_hex4(ps, cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF)
                        {
                            if (ps.end - ps.p < 6 || ps.p[0] != '\\' || ps.p[1] != 'u')
                                return false;
                            ps.p += 2;
                            std::uint32_t lo;
                            if (!parse_hex4(ps, lo)) return false;
                            if (lo < 0xDC00 || lo > 0xDFFF) return false;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        else if (cp >= 0xDC00 && cp <= 0xDFFF)
                        {
                            return false;
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default:
                        return false;
                    }
                }
                else if (c < 0x20)
                {
                    return false; /* unescaped control char */
                }
                else
                {
                    out += static_cast<char>(c);
                    ps.p++;
                }
            }
            return false;
        }

        bool parse_value(Parser& ps, Json& out);

        bool parse_object(Parser& ps, Json& out)
        {
            out = Json::object();
            ps.p++; /* '{' */
            ps.skip_ws();
            if (ps.p < ps.end && *ps.p == '}')
            {
                ps.p++;
                return true;
            }
            for (;;)
            {
                ps.skip_ws();
                std::string key;
                if (!parse_string_raw(ps, key)) return false;
                ps.skip_ws();
                if (ps.p >= ps.end || *ps.p != ':') return false;
                ps.p++;
                Json val;
                if (!parse_value(ps, val)) return false;
                out.set(std::move(key), std::move(val));
                ps.skip_ws();
                if (ps.p >= ps.end) return false;
                if (*ps.p == ',')
                {
                    ps.p++;
                    continue;
                }
                if (*ps.p == '}')
                {
                    ps.p++;
                    return true;
                }
                return false;
            }
        }

        bool parse_array(Parser& ps, Json& out)
        {
            out = Json::array();
            ps.p++; /* '[' */
            ps.skip_ws();
            if (ps.p < ps.end && *ps.p == ']')
            {
                ps.p++;
                return true;
            }
            for (;;)
            {
                Json val;
                if (!parse_value(ps, val)) return false;
                out.add(std::move(val));
                ps.skip_ws();
                if (ps.p >= ps.end) return false;
                if (*ps.p == ',')
                {
                    ps.p++;
                    continue;
                }
                if (*ps.p == ']')
                {
                    ps.p++;
                    return true;
                }
                return false;
            }
        }

        bool parse_value(Parser& ps, Json& out)
        {
            ps.skip_ws();
            if (ps.p >= ps.end) return false;
            if (++ps.depth > kMaxDepth)
            {
                ps.depth--;
                return false;
            }

            bool ok = false;
            char c = *ps.p;

            if (c == '{')
            {
                ok = parse_object(ps, out);
            }
            else if (c == '[')
            {
                ok = parse_array(ps, out);
            }
            else if (c == '"')
            {
                std::string s;
                if (parse_string_raw(ps, s))
                {
                    out = Json::string(std::move(s));
                    ok = true;
                }
            }
            else if (c == 't' && ps.end - ps.p >= 4 && !std::memcmp(ps.p, "true", 4))
            {
                ps.p += 4;
                out = Json::boolean(true);
                ok = true;
            }
            else if (c == 'f' && ps.end - ps.p >= 5 && !std::memcmp(ps.p, "false", 5))
            {
                ps.p += 5;
                out = Json::boolean(false);
                ok = true;
            }
            else if (c == 'n' && ps.end - ps.p >= 4 && !std::memcmp(ps.p, "null", 4))
            {
                ps.p += 4;
                out = Json();
                ok = true;
            }
            else if (c == '-' || (c >= '0' && c <= '9'))
            {
                char* endp = nullptr;
                /* The buffer is NUL terminated, so strtod stops safely. */
                double d = std::strtod(ps.p, &endp);
                if (endp && endp != ps.p)
                {
                    ps.p = endp;
                    out = Json::number(d);
                    ok = true;
                }
            }

            ps.depth--;
            return ok;
        }
    } // namespace

    Result<Json> Json::parse(std::string_view text)
    {
        /* Copy so strtod and lookahead see a NUL-terminated buffer. */
        std::string copy(text);
        Parser ps{copy.data(), copy.data() + copy.size()};
        Json v;
        if (!parse_value(ps, v)) return std::unexpected(Error::Protocol);
        ps.skip_ws();
        if (ps.p != ps.end) return std::unexpected(Error::Protocol); /* trailing junk */
        return v;
    }

    /* ------------------------------------------------------------------ */
    /* Writer                                                              */
    /* ------------------------------------------------------------------ */

    void Json::escape_to(std::string_view s, std::string& out)
    {
        out += '"';
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(c);
                }
            }
        }
        out += '"';
    }

    std::string Json::escape(std::string_view s)
    {
        std::string out;
        escape_to(s, out);
        return out;
    }

    namespace
    {
        void write_number(double n, std::string& out)
        {
            if (std::isnan(n) || std::isinf(n))
            {
                out += "null";
                return;
            }
            char buf[32];
            double r = std::floor(n);
            if (r == n && n >= -9007199254740992.0 && n <= 9007199254740992.0)
                std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(n));
            else
                std::snprintf(buf, sizeof buf, "%.17g", n);
            out += buf;
        }
    } // namespace

    void Json::dump_to(std::string& out) const
    {
        switch (type_)
        {
        case Type::Null:
            out += "null";
            break;
        case Type::Bool:
            out += (bool_ ? "true" : "false");
            break;
        case Type::Number:
            write_number(num_, out);
            break;
        case Type::String:
            escape_to(str_, out);
            break;
        case Type::Array:
            out += '[';
            for (std::size_t i = 0; i < arr_.size(); i++)
            {
                if (i) out += ',';
                arr_[i].dump_to(out);
            }
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (std::size_t i = 0; i < obj_.size(); i++)
            {
                if (i) out += ',';
                escape_to(obj_[i].first, out);
                out += ':';
                obj_[i].second.dump_to(out);
            }
            out += '}';
            break;
        }
    }

    std::string Json::dump() const
    {
        std::string out;
        dump_to(out);
        return out;
    }
} // namespace vc

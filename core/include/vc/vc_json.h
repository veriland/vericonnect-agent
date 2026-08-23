/*
 * vc_json.h - JSON parser / writer.
 *
 * Supports the full JSON grammar (objects, arrays, strings with escapes
 * incl. \uXXXX surrogate pairs, numbers, true/false/null). Input and
 * output are UTF-8.
 */
#ifndef VC_JSON_H
#define VC_JSON_H

#include "vc_common.h"

#ifdef __cplusplus

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vc
{
    /* A JSON value with value semantics (copyable, movable). Containers own
     * their children. */
    class Json
    {
    public:
        enum class Type
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object
        };

        Json() noexcept : type_(Type::Null) {}

        static Json boolean(bool b);
        static Json number(double n);
        static Json string(std::string s);
        static Json array();
        static Json object();

        /* Parse a complete JSON document (trailing junk is rejected). */
        [[nodiscard]] static Result<Json> parse(std::string_view text);

        Type type() const noexcept
        {
            return type_;
        }
        bool is_null() const noexcept
        {
            return type_ == Type::Null;
        }
        bool is_bool() const noexcept
        {
            return type_ == Type::Bool;
        }
        bool is_number() const noexcept
        {
            return type_ == Type::Number;
        }
        bool is_string() const noexcept
        {
            return type_ == Type::String;
        }
        bool is_array() const noexcept
        {
            return type_ == Type::Array;
        }
        bool is_object() const noexcept
        {
            return type_ == Type::Object;
        }

        bool as_bool(bool def = false) const noexcept;
        double as_number(double def = 0) const noexcept;
        std::string_view as_string(std::string_view def = "") const noexcept;

        /* Object member lookup. Returns nullptr if absent or not an object. */
        const Json* find(std::string_view key) const noexcept;    /* case-sensitive   */
        const Json* find_ci(std::string_view key) const noexcept; /* case-insensitive */

        /* Convenience typed getters (case-insensitive, returning def on mismatch). */
        std::string_view get_str(std::string_view key, std::string_view def = "") const noexcept;
        double get_num(std::string_view key, double def = 0) const noexcept;
        bool get_bool(std::string_view key, bool def = false) const noexcept;

        /* Number of elements (array) or members (object). */
        std::size_t size() const noexcept;

        const std::vector<Json>& elements() const noexcept
        {
            return arr_;
        }
        const std::vector<std::pair<std::string, Json>>& members() const noexcept
        {
            return obj_;
        }

        /* Builders (chainable). set() appends an object member; add() an array
         * element. */
        Json& set(std::string key, Json value);
        Json& add(Json value);

        /* Serialise. */
        std::string dump() const;
        void dump_to(std::string& out) const;

        /* Append s as a quoted, escaped JSON string literal to out. */
        static void escape_to(std::string_view s, std::string& out);
        static std::string escape(std::string_view s);

    private:
        Type type_;
        bool bool_ = false;
        double num_ = 0;
        std::string str_;
        std::vector<Json> arr_;
        std::vector<std::pair<std::string, Json>> obj_;
    };
} // namespace vc

#endif /* __cplusplus */

#endif

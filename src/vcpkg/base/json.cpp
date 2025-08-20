#include <vcpkg/base/contractual-constants.h>
#include <vcpkg/base/files.h>
#include <vcpkg/base/json.h>
#include <vcpkg/base/jsonreader.h>
#include <vcpkg/base/messages.h>
#include <vcpkg/base/system.debug.h>
#include <vcpkg/base/system.h>
#include <vcpkg/base/unicode.h>
#include <vcpkg/base/util.h>

#include <vcpkg/documentation.h>

#include <math.h>

#include <atomic>
#include <type_traits>

namespace vcpkg::Json
{
    static std::atomic<uint64_t> g_json_parsing_stats(0);

    using VK = ValueKind;

    // struct Value {
    namespace impl
    {
        // TODO: add a value_kind value template once we get rid of VS2015 support
        template<ValueKind Vk>
        using ValueKindConstant = std::integral_constant<ValueKind, Vk>;

        struct ValueImpl
        {
            VK tag;
            union
            {
                std::nullptr_t null;
                bool boolean;
                int64_t integer;
                double number;
                std::string string;
                Array array;
                Object object;
            };

            ValueImpl(ValueKindConstant<VK::Null> vk, std::nullptr_t) : tag(vk), null() { }
            ValueImpl(ValueKindConstant<VK::Boolean> vk, bool b) : tag(vk), boolean(b) { }
            ValueImpl(ValueKindConstant<VK::Integer> vk, int64_t i) : tag(vk), integer(i) { }
            ValueImpl(ValueKindConstant<VK::Number> vk, double d) : tag(vk), number(d) { }
            ValueImpl(ValueKindConstant<VK::String> vk, std::string&& s) : tag(vk), string(std::move(s)) { }
            ValueImpl(ValueKindConstant<VK::String> vk, const std::string& s) : tag(vk), string(s) { }
            ValueImpl(ValueKindConstant<VK::Array> vk, Array&& arr) : tag(vk), array(std::move(arr)) { }
            ValueImpl(ValueKindConstant<VK::Array> vk, const Array& arr) : tag(vk), array(arr) { }
            ValueImpl(ValueKindConstant<VK::Object> vk, Object&& obj) : tag(vk), object(std::move(obj)) { }
            ValueImpl(ValueKindConstant<VK::Object> vk, const Object& obj) : tag(vk), object(obj) { }

            ValueImpl& operator=(ValueImpl&& other) noexcept
            {
                switch (other.tag)
                {
                    case VK::Null: return internal_assign(VK::Null, &ValueImpl::null, other);
                    case VK::Boolean: return internal_assign(VK::Boolean, &ValueImpl::boolean, other);
                    case VK::Integer: return internal_assign(VK::Integer, &ValueImpl::integer, other);
                    case VK::Number: return internal_assign(VK::Number, &ValueImpl::number, other);
                    case VK::String: return internal_assign(VK::String, &ValueImpl::string, other);
                    case VK::Array: return internal_assign(VK::Array, &ValueImpl::array, other);
                    case VK::Object: return internal_assign(VK::Object, &ValueImpl::object, other);
                    default: Checks::unreachable(VCPKG_LINE_INFO);
                }
            }

            ~ValueImpl() { destroy_underlying(); }

        private:
            template<class T>
            ValueImpl& internal_assign(ValueKind vk, T ValueImpl::* mp, ValueImpl& other) noexcept
            {
                if (tag == vk)
                {
                    this->*mp = std::move(other.*mp);
                }
                else
                {
                    destroy_underlying();
                    auto* address = &(this->*mp);
                    new (address) T(std::move(other.*mp));
                    tag = vk;
                }

                return *this;
            }

            void destroy_underlying() noexcept
            {
                switch (tag)
                {
                    case VK::String: string.~basic_string(); break;
                    case VK::Array: array.~Array(); break;
                    case VK::Object: object.~Object(); break;
                    default: break;
                }
                new (&null) std::nullptr_t();
                tag = VK::Null;
            }
        };
    }

    using impl::ValueImpl;
    using impl::ValueKindConstant;

    VK Value::kind() const noexcept
    {
        if (underlying_)
        {
            return underlying_->tag;
        }
        else
        {
            return VK::Null;
        }
    }

    bool Value::is_null() const noexcept { return kind() == VK::Null; }
    bool Value::is_boolean() const noexcept { return kind() == VK::Boolean; }
    bool Value::is_integer() const noexcept { return kind() == VK::Integer; }
    bool Value::is_number() const noexcept
    {
        auto k = kind();
        return k == VK::Integer || k == VK::Number;
    }
    bool Value::is_string() const noexcept { return kind() == VK::String; }
    bool Value::is_array() const noexcept { return kind() == VK::Array; }
    bool Value::is_object() const noexcept { return kind() == VK::Object; }

    bool Value::boolean(LineInfo li) const noexcept
    {
        vcpkg::Checks::check_exit(li, is_boolean());
        return underlying_->boolean;
    }
    int64_t Value::integer(LineInfo li) const noexcept
    {
        vcpkg::Checks::check_exit(li, is_integer());
        return underlying_->integer;
    }
    double Value::number(LineInfo li) const noexcept
    {
        auto k = kind();
        if (k == VK::Number)
        {
            return underlying_->number;
        }
        else
        {
            return static_cast<double>(integer(li));
        }
    }
    StringView Value::string(LineInfo li) const noexcept
    {
        vcpkg::Checks::msg_check_exit(li, is_string(), msgJsonValueNotString);
        return underlying_->string;
    }

    std::string* Value::maybe_string() noexcept
    {
        if (underlying_ && underlying_->tag == VK::String)
        {
            return &underlying_->string;
        }

        return nullptr;
    }

    const std::string* Value::maybe_string() const noexcept
    {
        if (underlying_ && underlying_->tag == VK::String)
        {
            return &underlying_->string;
        }

        return nullptr;
    }

    const Array& Value::array(LineInfo li) const& noexcept
    {
        vcpkg::Checks::msg_check_exit(li, is_array(), msgJsonValueNotArray);
        return underlying_->array;
    }
    Array& Value::array(LineInfo li) & noexcept
    {
        vcpkg::Checks::msg_check_exit(li, is_array(), msgJsonValueNotArray);
        return underlying_->array;
    }
    Array&& Value::array(LineInfo li) && noexcept { return std::move(this->array(li)); }

    Array* Value::maybe_array() noexcept
    {
        if (underlying_ && underlying_->tag == VK::Array)
        {
            return &underlying_->array;
        }

        return nullptr;
    }

    const Array* Value::maybe_array() const noexcept
    {
        if (underlying_ && underlying_->tag == VK::Array)
        {
            return &underlying_->array;
        }

        return nullptr;
    }

    const Object& Value::object(LineInfo li) const& noexcept
    {
        vcpkg::Checks::msg_check_exit(li, is_object(), msgJsonValueNotObject);
        return underlying_->object;
    }
    Object& Value::object(LineInfo li) & noexcept
    {
        vcpkg::Checks::msg_check_exit(li, is_object(), msgJsonValueNotObject);
        return underlying_->object;
    }
    Object&& Value::object(LineInfo li) && noexcept { return std::move(this->object(li)); }

    Object* Value::maybe_object() noexcept
    {
        if (underlying_ && underlying_->tag == VK::Object)
        {
            return &underlying_->object;
        }

        return nullptr;
    }

    const Object* Value::maybe_object() const noexcept
    {
        if (underlying_ && underlying_->tag == VK::Object)
        {
            return &underlying_->object;
        }

        return nullptr;
    }

    Value::Value() noexcept = default;
    Value::Value(Value&&) noexcept = default;
    Value& Value::operator=(Value&&) noexcept = default;

    Value::Value(const Value& other)
    {
        switch (other.kind())
        {
            case ValueKind::Null: return; // default construct underlying_
            case ValueKind::Boolean:
                underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Boolean>(), other.underlying_->boolean);
                break;
            case ValueKind::Integer:
                underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Integer>(), other.underlying_->integer);
                break;
            case ValueKind::Number:
                underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Number>(), other.underlying_->number);
                break;
            case ValueKind::String:
                underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::String>(), other.underlying_->string);
                break;
            case ValueKind::Array:
                underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Array>(), other.underlying_->array);
                break;
            case ValueKind::Object:
                underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Object>(), other.underlying_->object);
                break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }

    Value& Value::operator=(const Value& other)
    {
        switch (other.kind())
        {
            case ValueKind::Null: underlying_.reset(); break;
            case ValueKind::Boolean:
                underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Boolean>(), other.underlying_->boolean);
                break;
            case ValueKind::Integer:
                underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Integer>(), other.underlying_->integer);
                break;
            case ValueKind::Number:
                underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Number>(), other.underlying_->number);
                break;
            case ValueKind::String:
                underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::String>(), other.underlying_->string);
                break;
            case ValueKind::Array:
                underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Array>(), other.underlying_->array);
                break;
            case ValueKind::Object:
                underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Object>(), other.underlying_->object);
                break;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }

        return *this;
    }

    Value::~Value() = default;

    Value Value::null(std::nullptr_t) noexcept { return Value(); }
    Value Value::boolean(bool b) noexcept
    {
        Value val;
        val.underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Boolean>(), b);
        return val;
    }
    Value Value::integer(int64_t i) noexcept
    {
        Value val;
        val.underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Integer>(), i);
        return val;
    }
    Value Value::number(double d) noexcept
    {
        vcpkg::Checks::check_exit(VCPKG_LINE_INFO, isfinite(d));
        Value val;
        val.underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Number>(), d);
        return val;
    }
    Value Value::string(std::string&& s) noexcept
    {
        if (!Unicode::utf8_is_valid_string(s.data(), s.data() + s.size()))
        {
            Debug::print("Invalid string: ", s, '\n');
            vcpkg::Checks::msg_exit_with_message(VCPKG_LINE_INFO, msgInvalidString);
        }
        Value val;
        val.underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::String>(), std::move(s));
        return val;
    }
    Value Value::array(Array&& arr) noexcept
    {
        Value val;
        val.underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Array>(), std::move(arr));
        return val;
    }
    Value Value::array(const Array& arr) noexcept
    {
        Value val;
        val.underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Array>(), arr);
        return val;
    }
    Value Value::object(Object&& obj) noexcept
    {
        Value val;
        val.underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Object>(), std::move(obj));
        return val;
    }
    Value Value::object(const Object& obj) noexcept
    {
        Value val;
        val.underlying_ = std::make_unique<ValueImpl>(ValueKindConstant<VK::Object>(), obj);
        return val;
    }

    bool operator==(const Value& lhs, const Value& rhs)
    {
        if (lhs.kind() != rhs.kind()) return false;

        switch (lhs.kind())
        {
            case ValueKind::Null: return true;
            case ValueKind::Boolean: return lhs.underlying_->boolean == rhs.underlying_->boolean;
            case ValueKind::Integer: return lhs.underlying_->integer == rhs.underlying_->integer;
            case ValueKind::Number: return lhs.underlying_->number == rhs.underlying_->number;
            case ValueKind::String: return lhs.underlying_->string == rhs.underlying_->string;
            case ValueKind::Array: return lhs.underlying_->array == rhs.underlying_->array;
            case ValueKind::Object: return lhs.underlying_->object == rhs.underlying_->object;
            default: Checks::unreachable(VCPKG_LINE_INFO);
        }
    }
    // } struct Value
    // struct Array {
    Value& Array::push_back(std::string&& value) { return this->push_back(Json::Value::string(std::move(value))); }
    Value& Array::push_back(Value&& value) { return underlying_.emplace_back(std::move(value)); }
    Object& Array::push_back(Object&& obj) { return push_back(Value::object(std::move(obj))).object(VCPKG_LINE_INFO); }
    Array& Array::push_back(Array&& arr) { return push_back(Value::array(std::move(arr))).array(VCPKG_LINE_INFO); }
    Value& Array::insert_before(iterator it, Value&& value)
    {
        size_t index = it - underlying_.begin();
        underlying_.insert(it, std::move(value));
        return underlying_[index];
    }
    Object& Array::insert_before(iterator it, Object&& obj)
    {
        return insert_before(it, Value::object(std::move(obj))).object(VCPKG_LINE_INFO);
    }
    Array& Array::insert_before(iterator it, Array&& arr)
    {
        return insert_before(it, Value::array(std::move(arr))).array(VCPKG_LINE_INFO);
    }
    bool operator==(const Array& lhs, const Array& rhs) { return lhs.underlying_ == rhs.underlying_; }
    // } struct Array
    // struct Object {
    Value& Object::insert(StringView key, std::string&& value) { return insert(key, Value::string(std::move(value))); }
    Value& Object::insert(StringView key, Value&& value)
    {
        if (contains(key))
        {
            Checks::unreachable(VCPKG_LINE_INFO,
                                fmt::format("attempted to insert duplicate key {} into JSON object", key));
        }

        return underlying_.emplace_back(key.to_string(), std::move(value)).second;
    }
    Value& Object::insert(StringView key, const Value& value)
    {
        if (contains(key))
        {
            Checks::unreachable(VCPKG_LINE_INFO,
                                fmt::format("attempted to insert duplicate key {} into JSON object", key));
        }

        return underlying_.emplace_back(key.to_string(), value).second;
    }
    Array& Object::insert(StringView key, Array&& value)
    {
        return insert(key, Value::array(std::move(value))).array(VCPKG_LINE_INFO);
    }
    Array& Object::insert(StringView key, const Array& value)
    {
        return insert(key, Value::array(value)).array(VCPKG_LINE_INFO);
    }
    Object& Object::insert(StringView key, Object&& value)
    {
        return insert(key, Value::object(std::move(value))).object(VCPKG_LINE_INFO);
    }
    Object& Object::insert(StringView key, const Object& value)
    {
        return insert(key, Value::object(value)).object(VCPKG_LINE_INFO);
    }

    Value& Object::insert_or_replace(StringView key, std::string&& value)
    {
        return this->insert_or_replace(key, Json::Value::string(std::move(value)));
    }
    Value& Object::insert_or_replace(StringView key, Value&& value)
    {
        auto v = get(key);
        if (v)
        {
            *v = std::move(value);
            return *v;
        }
        else
        {
            return underlying_.emplace_back(key, std::move(value)).second;
        }
    }
    Value& Object::insert_or_replace(StringView key, const Value& value)
    {
        auto v = get(key);
        if (v)
        {
            *v = value;
            return *v;
        }
        else
        {
            return underlying_.emplace_back(key, value).second;
        }
    }
    Array& Object::insert_or_replace(StringView key, Array&& value)
    {
        return insert_or_replace(key, Value::array(std::move(value))).array(VCPKG_LINE_INFO);
    }
    Array& Object::insert_or_replace(StringView key, const Array& value)
    {
        return insert_or_replace(key, Value::array(value)).array(VCPKG_LINE_INFO);
    }
    Object& Object::insert_or_replace(StringView key, Object&& value)
    {
        return insert_or_replace(key, Value::object(std::move(value))).object(VCPKG_LINE_INFO);
    }
    Object& Object::insert_or_replace(StringView key, const Object& value)
    {
        return insert_or_replace(key, Value::object(value)).object(VCPKG_LINE_INFO);
    }

    auto Object::internal_find_key(StringView key) const noexcept -> underlying_t::const_iterator
    {
        return std::find_if(
            underlying_.begin(), underlying_.end(), [key](const auto& pair) { return pair.first == key; });
    }

    // returns whether the key existed
    bool Object::remove(StringView key) noexcept
    {
        auto it = internal_find_key(key);
        if (it == underlying_.end())
        {
            return false;
        }
        else
        {
            underlying_.erase(it);
            return true;
        }
    }

    Value* Object::get(StringView key) noexcept
    {
        auto it = internal_find_key(key);
        if (it == underlying_.end())
        {
            return nullptr;
        }
        else
        {
            return &underlying_[it - underlying_.begin()].second;
        }
    }
    const Value* Object::get(StringView key) const noexcept
    {
        auto it = internal_find_key(key);
        if (it == underlying_.end())
        {
            return nullptr;
        }
        else
        {
            return &it->second;
        }
    }

    void Object::sort_keys()
    {
        std::sort(underlying_.begin(), underlying_.end(), [](const value_type& lhs, const value_type& rhs) {
            return lhs.first < rhs.first;
        });
    }

    bool operator==(const Object& lhs, const Object& rhs) { return lhs.underlying_ == rhs.underlying_; }
    // } struct Object

    // auto parse() {
    namespace
    {
        struct Parser : private ParserBase
        {
            Parser(StringView text, StringView origin, TextRowCol init_rowcol)
                : ParserBase(text, origin, init_rowcol), style_()
            {
            }

            char32_t next() noexcept
            {
                auto ch = cur();
                if (ch == '\r') style_.newline_kind = JsonStyle::Newline::CrLf;
                if (ch == '\t') style_.set_tabs();
                return ParserBase::next();
            }

            static bool is_number_start(char32_t code_point) noexcept
            {
                return code_point == '-' || is_ascii_digit(code_point);
            }

            static unsigned char from_hex_digit(char32_t code_point) noexcept
            {
                if (is_ascii_digit(code_point))
                {
                    return static_cast<unsigned char>(code_point) - '0';
                }
                else if (code_point >= 'a' && code_point <= 'f')
                {
                    return static_cast<unsigned char>(code_point) - 'a' + 10;
                }
                else if (code_point >= 'A' && code_point <= 'F')
                {
                    return static_cast<unsigned char>(code_point) - 'A' + 10;
                }
                else
                {
                    vcpkg::Checks::unreachable(VCPKG_LINE_INFO);
                }
            }

            // parses a _single_ code point of a string -- either a literal code point, or an escape sequence
            // returns end_of_file if it reaches an unescaped '"'
            // _does not_ pair escaped surrogates -- returns the literal surrogate.
            char32_t parse_string_code_point() noexcept
            {
                char32_t current = cur();
                if (current == '"')
                {
                    next();
                    return Unicode::end_of_file;
                }
                else if (current <= 0x001F)
                {
                    add_error(msg::format(msgControlCharacterInString));
                    next();
                    return Unicode::end_of_file;
                }
                else if (current != '\\')
                {
                    next();
                    return current;
                }

                // cur == '\\'
                if (at_eof())
                {
                    add_error(msg::format(msgUnexpectedEOFAfterEscape));
                    return Unicode::end_of_file;
                }
                current = next();

                switch (current)
                {
                    case '"': next(); return '"';
                    case '\\': next(); return '\\';
                    case '/': next(); return '/';
                    case 'b': next(); return '\b';
                    case 'f': next(); return '\f';
                    case 'n': next(); return '\n';
                    case 'r': next(); return '\r';
                    case 't': next(); return '\t';
                    case 'u':
                    {
                        char16_t code_unit = 0;
                        for (unsigned int i = 0; i < 4; ++i)
                        {
                            current = next();

                            if (current == Unicode::end_of_file)
                            {
                                add_error(msg::format(msgUnexpectedEOFMidUnicodeEscape));
                                return Unicode::end_of_file;
                            }
                            if (is_hex_digit(current))
                            {
                                code_unit *= 16;
                                code_unit += from_hex_digit(current);
                            }
                            else
                            {
                                add_error(msg::format(msgInvalidHexDigit));
                                return Unicode::end_of_file;
                            }
                        }
                        next();

                        return code_unit;
                    }
                    default: add_error(msg::format(msgUnexpectedEscapeSequence)); return Unicode::end_of_file;
                }
            }

            std::string parse_string() noexcept
            {
                Checks::check_exit(VCPKG_LINE_INFO, cur() == '"');
                next();

                std::string res;
                char32_t previous_leading_surrogate = Unicode::end_of_file;
                while (!at_eof())
                {
                    auto code_point = parse_string_code_point();

                    if (previous_leading_surrogate != Unicode::end_of_file)
                    {
                        if (Unicode::utf16_is_trailing_surrogate_code_point(code_point))
                        {
                            const auto full_code_point =
                                Unicode::utf16_surrogates_to_code_point(previous_leading_surrogate, code_point);
                            Unicode::utf8_append_code_point(res, full_code_point);
                            previous_leading_surrogate = Unicode::end_of_file;
                            continue;
                        }
                        else
                        {
                            Unicode::utf8_append_code_point(res, previous_leading_surrogate);
                        }
                    }
                    previous_leading_surrogate = Unicode::end_of_file;

                    if (Unicode::utf16_is_leading_surrogate_code_point(code_point))
                    {
                        previous_leading_surrogate = code_point;
                    }
                    else if (code_point == Unicode::end_of_file)
                    {
                        return res;
                    }
                    else
                    {
                        Unicode::utf8_append_code_point(res, code_point);
                    }
                }

                add_error(msg::format(msgUnexpectedEOFMidString));
                return res;
            }

            Value parse_number() noexcept
            {
                Checks::check_exit(VCPKG_LINE_INFO, is_number_start(cur()));

                bool floating = false;
                bool negative = false; // negative & 0 -> floating, so keep track of it
                std::string number_to_parse;

                char32_t current = cur();
                if (cur() == '-')
                {
                    number_to_parse.push_back('-');
                    negative = true;
                    current = next();
                    if (current == Unicode::end_of_file)
                    {
                        add_error(msg::format(msgUnexpectedEOFAfterMinus));
                        return Value();
                    }
                }

                if (current == '0')
                {
                    current = next();
                    if (current == '.')
                    {
                        number_to_parse.append("0.");
                        floating = true;
                        current = next();
                    }
                    else if (is_ascii_digit(current))
                    {
                        add_error(msg::format(msgUnexpectedDigitsAfterLeadingZero));
                        return Value();
                    }
                    else
                    {
                        if (negative)
                        {
                            return Value::number(-0.0);
                        }
                        else
                        {
                            return Value::integer(0);
                        }
                    }
                }

                while (is_ascii_digit(current))
                {
                    number_to_parse.push_back(static_cast<char>(current));
                    current = next();
                }
                if (!floating && current == '.')
                {
                    floating = true;
                    number_to_parse.push_back('.');
                    current = next();
                    if (!is_ascii_digit(current))
                    {
                        add_error(msg::format(msgExpectedDigitsAfterDecimal));
                        return Value();
                    }
                    while (is_ascii_digit(current))
                    {
                        number_to_parse.push_back(static_cast<char>(current));
                        current = next();
                    }
                }

                if (floating)
                {
                    auto opt = Strings::strto<double>(number_to_parse);
                    if (auto res = opt.get())
                    {
                        if (std::abs(*res) < INFINITY)
                        {
                            return Value::number(*res);
                        }
                        else
                        {
                            add_error(msg::format(msgFloatingPointConstTooBig, msg::count = number_to_parse));
                        }
                    }
                    else
                    {
                        add_error(msg::format(msgInvalidFloatingPointConst, msg::count = number_to_parse));
                    }
                }
                else
                {
                    auto opt = Strings::strto<int64_t>(number_to_parse);
                    if (auto res = opt.get())
                    {
                        return Value::integer(*res);
                    }
                    else
                    {
                        add_error(msg::format(msgInvalidIntegerConst, msg::count = number_to_parse));
                    }
                }

                return Value();
            }

            Value parse_keyword() noexcept
            {
                char32_t current = cur();
                const char32_t* rest;
                Value val;
                switch (current)
                {
                    case 't': // parse true
                        rest = U"rue";
                        val = Value::boolean(true);
                        break;
                    case 'f': // parse false
                        rest = U"alse";
                        val = Value::boolean(false);
                        break;
                    case 'n': // parse null
                        rest = U"ull";
                        val = Value::null(nullptr);
                        break;
                    default: vcpkg::Checks::unreachable(VCPKG_LINE_INFO);
                }

                for (const char32_t* rest_it = rest; *rest_it != '\0'; ++rest_it)
                {
                    current = next();

                    if (current == Unicode::end_of_file)
                    {
                        add_error(msg::format(msgUnexpectedEOFMidKeyword));
                        return Value();
                    }
                    if (current != *rest_it)
                    {
                        add_error(msg::format(msgUnexpectedCharMidKeyword));
                    }
                }
                next();

                return val;
            }

            Value parse_array() noexcept
            {
                Checks::check_exit(VCPKG_LINE_INFO, cur() == '[');
                next();

                Array arr;
                bool first = true;
                for (;;)
                {
                    skip_whitespace();

                    char32_t current = cur();
                    if (current == Unicode::end_of_file)
                    {
                        add_error(msg::format(msgUnexpectedEOFMidArray));
                        return Value();
                    }
                    if (current == ']')
                    {
                        next();
                        return Value::array(std::move(arr));
                    }

                    if (first)
                    {
                        first = false;
                    }
                    else if (current == ',')
                    {
                        auto comma_loc = cur_loc();
                        next();
                        skip_whitespace();
                        current = cur();
                        if (current == Unicode::end_of_file)
                        {
                            add_error(msg::format(msgUnexpectedEOFMidArray));
                            return Value();
                        }
                        if (current == ']')
                        {
                            add_error(msg::format(msgTrailingCommaInArray), comma_loc);
                            return Value::array(std::move(arr));
                        }
                    }
                    else if (current == '/')
                    {
                        add_error(std::move(
                            msg::format(msgUnexpectedCharMidArray).append_raw('\n').append(msgInvalidCommentStyle)));
                    }
                    else
                    {
                        add_error(msg::format(msgUnexpectedCharMidArray));
                        return Value();
                    }

                    arr.push_back(parse_value());
                }
            }

            std::pair<std::string, Value> parse_kv_pair() noexcept
            {
                skip_whitespace();

                auto current = cur();

                std::pair<std::string, Value> res = {std::string(""), Value()};

                if (current == Unicode::end_of_file)
                {
                    add_error(msg::format(msgUnexpectedEOFExpectedName));
                    return res;
                }
                if (current != '"')
                {
                    add_error(msg::format(msgUnexpectedCharExpectedName));
                    return res;
                }
                res.first = parse_string();

                skip_whitespace();
                current = cur();
                if (current == ':')
                {
                    next();
                }
                else if (current == Unicode::end_of_file)
                {
                    add_error(msg::format(msgUnexpectedEOFExpectedColon));
                    return res;
                }
                else if (current == '/')
                {
                    add_error(std::move(
                        msg::format(msgUnexpectedCharExpectedColon).append_raw('\n').append(msgInvalidCommentStyle)));
                    return res;
                }
                else
                {
                    add_error(msg::format(msgUnexpectedCharExpectedColon));
                    return res;
                }

                res.second = parse_value();

                return res;
            }

            Value parse_object() noexcept
            {
                char32_t current = cur();

                Checks::check_exit(VCPKG_LINE_INFO, current == '{');
                next();

                Object obj;
                bool first = true;
                for (;;)
                {
                    skip_whitespace();
                    current = cur();
                    if (current == Unicode::end_of_file)
                    {
                        add_error(msg::format(msgUnexpectedEOFExpectedCloseBrace));
                        return Value();
                    }
                    else if (current == '}')
                    {
                        next();
                        return Value::object(std::move(obj));
                    }

                    if (first)
                    {
                        first = false;
                    }
                    else if (current == ',')
                    {
                        auto comma_loc = cur_loc();
                        next();
                        skip_whitespace();
                        current = cur();
                        if (current == Unicode::end_of_file)
                        {
                            add_error(msg::format(msgUnexpectedEOFExpectedProp));
                            return Value();
                        }
                        else if (current == '}')
                        {
                            add_error(msg::format(msgTrailingCommaInObj), comma_loc);
                            return Value();
                        }
                    }
                    else if (current == '/')
                    {
                        add_error(std::move(msg::format(msgUnexpectedCharExpectedColon)
                                                .append_raw('\n')
                                                .append(msgInvalidCommentStyle)));
                    }
                    else
                    {
                        add_error(msg::format(msgUnexpectedCharExpectedCloseBrace));
                    }

                    auto keyPairLoc = cur_loc();
                    auto val = parse_kv_pair();
                    if (obj.contains(val.first))
                    {
                        add_error(msg::format(msgDuplicatedKeyInObj, msg::value = val.first), keyPairLoc);
                        return Value();
                    }
                    obj.insert(val.first, std::move(val.second));
                }
            }

            Value parse_value() noexcept
            {
                skip_whitespace();
                char32_t current = cur();
                if (current == Unicode::end_of_file)
                {
                    add_error(msg::format(msgUnexpectedEOFExpectedValue));
                    return Value();
                }

                switch (current)
                {
                    case '{': return parse_object();
                    case '[': return parse_array();
                    case '"': return Value::string(parse_string());
                    case 'n':
                    case 't':
                    case 'f': return parse_keyword();
                    case '/':
                    {
                        add_error(std::move(msg::format(msgUnexpectedCharExpectedValue)
                                                .append_raw('\n')
                                                .append(msgInvalidCommentStyle)));
                        return Value();
                    }
                    default:
                        if (is_number_start(current))
                        {
                            return parse_number();
                        }
                        else
                        {
                            add_error(msg::format(msgUnexpectedCharExpectedValue));
                            return Value();
                        }
                }
            }

            static ExpectedL<ParsedJson> parse(StringView json, StringView origin)
            {
                StatsTimer t(g_json_parsing_stats);

                json.remove_bom();

                auto parser = Parser(json, origin, {1, 1});

                auto val = parser.parse_value();

                parser.skip_whitespace();
                if (!parser.at_eof())
                {
                    parser.add_error(msg::format(msgUnexpectedEOFExpectedChar));
                }

                if (parser.messages().any_errors())
                {
                    return parser.messages().join();
                }

                return ParsedJson{std::move(val), parser.style()};
            }

            JsonStyle style() const noexcept { return style_; }

        private:
            JsonStyle style_;
        };
    }

    Optional<std::string> StringDeserializer::visit_string(Reader&, StringView sv) const { return sv.to_string(); }

    LocalizedString UntypedStringDeserializer::type_name() const { return msg::format(msgAString); }

    const UntypedStringDeserializer UntypedStringDeserializer::instance;

    LocalizedString PathDeserializer::type_name() const { return msg::format(msgAPath); }
    Optional<Path> PathDeserializer::visit_string(Reader&, StringView sv) const { return sv; }

    const PathDeserializer PathDeserializer::instance;

    LocalizedString NaturalNumberDeserializer::type_name() const { return msg::format(msgANonNegativeInteger); }

    Optional<int> NaturalNumberDeserializer::visit_integer(Reader&, int64_t value) const
    {
        if (value > std::numeric_limits<int>::max() || value < 0)
        {
            return nullopt;
        }

        return static_cast<int>(value);
    }

    const NaturalNumberDeserializer NaturalNumberDeserializer::instance;

    LocalizedString BooleanDeserializer::type_name() const { return msg::format(msgABoolean); }

    Optional<bool> BooleanDeserializer::visit_boolean(Reader&, bool b) const { return b; }

    const BooleanDeserializer BooleanDeserializer::instance;

    bool IdentifierDeserializer::is_ident(StringView sv)
    {
        // back-compat
        if (sv == "all_modules")
        {
            return true;
        }

        // [a-z0-9]+(-[a-z0-9]+)*
        auto cur = sv.begin();
        const auto last = sv.end();
        for (;;)
        {
            if (cur == last || !ParserBase::is_lower_digit(*cur)) return false;
            ++cur;
            while (cur != last && ParserBase::is_lower_digit(*cur))
                ++cur;

            if (cur == last) break;
            if (*cur != '-') return false;
            ++cur;
        }

        if (sv.size() < 5)
        {
            // see https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file#naming-conventions
            if (sv == "prn" || sv == "aux" || sv == "nul" || sv == "con" || sv == FeatureNameCore)
            {
                return false; // we're a reserved identifier
            }
            if (sv.size() == 4 && (sv.starts_with("lpt") || sv.starts_with("com")) && sv[3] >= '0' && sv[3] <= '9')
            {
                return false; // we're a reserved identifier
            }
        }
        else
        {
            if (sv == FeatureNameDefault)
            {
                return false;
            }
        }

        return true;
    }

    ParsedJson parse_file(vcpkg::LineInfo li, const ReadOnlyFilesystem& fs, const Path& json_file)
    {
        std::error_code ec;
        auto disk_contents = fs.read_contents(json_file, ec);
        if (ec)
        {
            Checks::msg_exit_with_error(li, format_filesystem_call_error(ec, "read_contents", {json_file}));
        }

        return parse(disk_contents, json_file).value_or_exit(VCPKG_LINE_INFO);
    }

    ExpectedL<ParsedJson> parse(StringView json, StringView origin) { return Parser::parse(json, origin); }

    ExpectedL<Json::Object> parse_object(StringView text, StringView origin)
    {
        return parse(text, origin).then([&](ParsedJson&& mabeValueIsh) -> ExpectedL<Json::Object> {
            auto& asValue = mabeValueIsh.value;
            if (auto as_object = asValue.maybe_object())
            {
                return std::move(*as_object);
            }

            return msg::format(msgJsonErrorMustBeAnObject, msg::path = origin);
        });
    }
    // } auto parse()

    namespace
    {
        struct Stringifier
        {
            JsonStyle style;
            std::string& buffer;

            void append_indent(size_t indent) const
            {
                if (style.use_tabs())
                {
                    buffer.append(indent, '\t');
                }
                else
                {
                    buffer.append(indent * style.spaces(), ' ');
                }
            };

            void append_unicode_escape(char16_t code_unit) const
            {
                // AFAIK, there's no standard way of doing this?
                constexpr const char hex_digit[16] = {
                    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

                const char seq[6] = {
                    '\\',
                    'u',
                    hex_digit[(code_unit >> 12) & 0x0F],
                    hex_digit[(code_unit >> 8) & 0x0F],
                    hex_digit[(code_unit >> 4) & 0x0F],
                    hex_digit[(code_unit >> 0) & 0x0F],
                };

                buffer.append(seq, 6);
            }

            // taken from the ECMAScript 2020 standard, 24.5.2.2: Runtime Semantics: QuoteJSONString
            void append_quoted_json_string(StringView sv)
            {
                // Table 66: JSON Single Character Escape Sequences
                constexpr static std::pair<char32_t, const char*> escape_sequences[] = {
                    {0x0008, R"(\b)"}, // BACKSPACE
                    {0x0009, R"(\t)"}, // CHARACTER TABULATION
                    {0x000A, R"(\n)"}, // LINE FEED (LF)
                    {0x000C, R"(\f)"}, // FORM FEED (FF)
                    {0x000D, R"(\r)"}, // CARRIAGE RETURN (CR)
                    {0x0022, R"(\")"}, // QUOTATION MARK
                    {0x005C, R"(\\)"}  // REVERSE SOLIDUS
                };
                // 1. Let product be the String value consisting solely of the code unit 0x0022 (QUOTATION MARK).
                buffer.push_back('"');

                // 2. For each code point C in ! UTF16DecodeString(value), do
                // (note that we use utf8 instead of utf16)
                for (auto code_point : Unicode::Utf8Decoder(sv.begin(), sv.end()))
                {
                    // a. If C is listed in the "Code Point" column of Table 66, then
                    const auto match = std::find_if(begin(escape_sequences),
                                                    end(escape_sequences),
                                                    [code_point](const std::pair<char32_t, const char*>& attempt) {
                                                        return attempt.first == code_point;
                                                    });
                    // i. Set product to the string-concatenation of product and the escape sequence for C as
                    // specified in the "Escape Sequence" column of the corresponding row.
                    if (match != end(escape_sequences))
                    {
                        buffer.append(match->second);
                        continue;
                    }

                    // b. Else if C has a numeric value less than 0x0020 (SPACE), or if C has the same numeric value as
                    // a leading surrogate or trailing surrogate, then
                    if (code_point < 0x0020 || Unicode::utf16_is_surrogate_code_point(code_point))
                    {
                        // i. Let unit be the code unit whose numeric value is that of C.
                        // ii. Set product to the string-concatenation of product and UnicodeEscape(unit).
                        append_unicode_escape(static_cast<char16_t>(code_point));
                        break;
                    }

                    // c. Else,
                    // i. Set product to the string-concatenation of product and the UTF16Encoding of C.
                    // (again, we use utf-8 here instead)
                    Unicode::utf8_append_code_point(buffer, code_point);
                }

                // 3. Set product to the string-concatenation of product and the code unit 0x0022 (QUOTATION MARK).
                buffer.push_back('"');
            }

            void stringify_object(const Object& obj, size_t current_indent)
            {
                buffer.push_back('{');
                if (obj.size() != 0)
                {
                    bool first = true;

                    for (const auto& el : obj)
                    {
                        if (!first)
                        {
                            buffer.push_back(',');
                        }
                        first = false;

                        buffer.append(style.newline());
                        append_indent(current_indent + 1);

                        append_quoted_json_string(el.first);
                        buffer.append(": ");
                        stringify(el.second, current_indent + 1);
                    }
                    buffer.append(style.newline());
                    append_indent(current_indent);
                }
                buffer.push_back('}');
            }

            void stringify_object_member(StringView member_name, const Array& val, size_t current_indent)
            {
                append_quoted_json_string(member_name);
                buffer.append(": ");
                stringify_array(val, current_indent);
            }

            void stringify_array(const Array& arr, size_t current_indent)
            {
                buffer.push_back('[');
                if (arr.size() == 0)
                {
                    buffer.push_back(']');
                }
                else
                {
                    bool first = true;

                    for (const auto& el : arr)
                    {
                        if (!first)
                        {
                            buffer.push_back(',');
                        }
                        first = false;

                        buffer.append(style.newline());
                        append_indent(current_indent + 1);

                        stringify(el, current_indent + 1);
                    }
                    buffer.append(style.newline());
                    append_indent(current_indent);
                    buffer.push_back(']');
                }
            }

            void stringify(const Value& value, size_t current_indent)
            {
                switch (value.kind())
                {
                    case VK::Null: buffer.append("null"); break;
                    case VK::Boolean:
                    {
                        auto v = value.boolean(VCPKG_LINE_INFO);
                        buffer.append(v ? "true" : "false");
                        break;
                    }
                    // TODO: switch to `to_chars` once we are able to remove support for old compilers
                    case VK::Integer: buffer.append(std::to_string(value.integer(VCPKG_LINE_INFO))); break;
                    case VK::Number: buffer.append(std::to_string(value.number(VCPKG_LINE_INFO))); break;
                    case VK::String:
                    {
                        append_quoted_json_string(value.string(VCPKG_LINE_INFO));
                        break;
                    }
                    case VK::Array:
                    {
                        stringify_array(value.array(VCPKG_LINE_INFO), current_indent);
                        break;
                    }
                    case VK::Object:
                    {
                        stringify_object(value.object(VCPKG_LINE_INFO), current_indent);
                        break;
                    }
                }
            }
        };
    }

    std::string stringify(const Value& value) { return stringify(value, JsonStyle{}); }
    std::string stringify(const Value& value, JsonStyle style)
    {
        std::string res;
        Stringifier{style, res}.stringify(value, 0);
        res.push_back('\n');
        return res;
    }
    std::string stringify(const Object& obj) { return stringify(obj, JsonStyle{}); }
    std::string stringify(const Object& obj, JsonStyle style)
    {
        std::string res;
        Stringifier{style, res}.stringify_object(obj, 0);
        res.push_back('\n');
        return res;
    }
    std::string stringify(const Array& arr) { return stringify(arr, JsonStyle{}); }
    std::string stringify(const Array& arr, JsonStyle style)
    {
        std::string res;
        Stringifier{style, res}.stringify_array(arr, 0);
        res.push_back('\n');
        return res;
    }
    // } auto stringify()

    std::string stringify_object_member(StringLiteral member_name,
                                        const Array& arr,
                                        JsonStyle style,
                                        int initial_indent)
    {
        std::string res;
        Stringifier stringifier{style, res};
        stringifier.append_indent(initial_indent);
        stringifier.stringify_object_member(member_name, arr, initial_indent);
        res.push_back('\n');
        return res;
    }

    uint64_t get_json_parsing_stats() { return g_json_parsing_stats.load(); }

    static std::vector<std::string> invalid_json_fields(const Json::Object& obj,
                                                        View<StringLiteral> known_fields) noexcept
    {
        const auto field_is_unknown = [known_fields](StringView sv) {
            // allow directives
            if (sv.size() != 0 && *sv.begin() == '$')
            {
                return false;
            }
            return std::find(known_fields.begin(), known_fields.end(), sv) == known_fields.end();
        };

        std::vector<std::string> res;
        for (const auto& kv : obj)
        {
            if (field_is_unknown(kv.first))
            {
                res.push_back(kv.first.to_string());
            }
        }

        return res;
    }

    static std::atomic<uint64_t> g_json_reader_stats(0);

    Reader::Reader(StringView origin) : m_origin(origin.data(), origin.size()), m_stat_timer(g_json_reader_stats) { }

    uint64_t Reader::get_reader_stats() { return g_json_reader_stats.load(); }

    void Reader::add_missing_field_error(const LocalizedString& type, StringView key, const LocalizedString& key_type)
    {
        add_generic_error(type, msg::format(msgMissingRequiredField, msg::json_field = key, msg::json_type = key_type));
    }
    void Reader::add_expected_type_error(const LocalizedString& expected_type)
    {
        m_messages.add_line(
            DiagnosticLine{DiagKind::Error,
                           m_origin,
                           msg::format(msgMismatchedType, msg::json_field = path(), msg::json_type = expected_type)});
    }
    void Reader::add_extra_field_error(const LocalizedString& type, StringView field, StringView suggestion)
    {
        if (suggestion.size() > 0)
        {
            add_generic_error(type,
                              msg::format(msgUnexpectedFieldSuggest, msg::json_field = field, msg::value = suggestion));
        }
        else
        {
            add_generic_error(type, msg::format(msgUnexpectedField, msg::json_field = field));
        }
    }
    void Reader::add_generic_error(const LocalizedString& type, StringView message)
    {
        m_messages.add_line(DiagnosticLine{
            DiagKind::Error,
            m_origin,
            LocalizedString::from_raw(path()).append_raw(" (").append(type).append_raw("): ").append_raw(message)});
    }

    void Reader::add_field_name_error(const LocalizedString& type, StringView field, StringView message)
    {
        PathGuard guard{m_path, field};
        add_generic_error(type, message);
    }

    void Reader::check_for_unexpected_fields(const Object& obj,
                                             View<StringLiteral> valid_fields,
                                             const LocalizedString& type_name)
    {
        if (valid_fields.size() == 0)
        {
            return;
        }

        auto extra_fields = invalid_json_fields(obj, valid_fields);
        for (auto&& f : extra_fields)
        {
            auto best_it = valid_fields.begin();
            auto best_value = Strings::byte_edit_distance(f, *best_it);
            for (auto i = best_it + 1; i != valid_fields.end(); ++i)
            {
                auto v = Strings::byte_edit_distance(f, *i);
                if (v < best_value)
                {
                    best_value = v;
                    best_it = i;
                }
            }
            add_extra_field_error(type_name, f, *best_it);
        }
    }

    void Reader::add_warning(LocalizedString type, StringView message)
    {
        m_messages.add_line(DiagnosticLine{
            DiagKind::Warning,
            m_origin,
            LocalizedString::from_raw(path()).append_raw(" (").append(type).append_raw("): ").append_raw(message)});
    }

    std::string Reader::path() const noexcept
    {
        std::string p("$");
        for (auto&& s : m_path)
        {
            if (s.index < 0)
            {
                p.push_back('.');
                p.append(s.field.data(), s.field.size());
            }
            else
            {
                fmt::format_to(std::back_inserter(p), "[{}]", s.index);
            }
        }
        return p;
    }

    StringView Reader::origin() const noexcept { return m_origin; }

    LocalizedString ParagraphDeserializer::type_name() const { return msg::format(msgAStringOrArrayOfStrings); }

    Optional<std::vector<std::string>> ParagraphDeserializer::visit_string(Reader&, StringView sv) const
    {
        std::vector<std::string> out;
        out.push_back(sv.to_string());
        return out;
    }

    Optional<std::vector<std::string>> ParagraphDeserializer::visit_array(Reader& r, const Array& arr) const
    {
        return r.array_elements(arr, UntypedStringDeserializer::instance);
    }

    const ParagraphDeserializer ParagraphDeserializer::instance;

    LocalizedString IdentifierDeserializer::type_name() const { return msg::format(msgAnIdentifer); }

    Optional<std::string> IdentifierDeserializer::visit_string(Json::Reader& r, StringView sv) const
    {
        if (!is_ident(sv))
        {
            r.add_generic_error(type_name(),
                                msg::format(msgParseIdentifierError, msg::value = sv, msg::url = docs::manifests_url));
        }
        return sv.to_string();
    }

    const IdentifierDeserializer IdentifierDeserializer::instance;

    LocalizedString IdentifierArrayDeserializer::type_name() const { return msg::format(msgAnArrayOfIdentifers); }

    Optional<std::vector<std::string>> IdentifierArrayDeserializer::visit_array(Reader& r, const Array& arr) const
    {
        return r.array_elements(arr, IdentifierDeserializer::instance);
    }

    const IdentifierArrayDeserializer IdentifierArrayDeserializer::instance;

    LocalizedString PackageNameDeserializer::type_name() const { return msg::format(msgAPackageName); }

    Optional<std::string> PackageNameDeserializer::visit_string(Json::Reader& r, StringView sv) const
    {
        if (!IdentifierDeserializer::is_ident(sv))
        {
            r.add_generic_error(
                type_name(),
                msg::format(msgParsePackageNameError, msg::package_name = sv, msg::url = docs::manifests_url));
        }
        return sv.to_string();
    }

    const PackageNameDeserializer PackageNameDeserializer::instance;

    LocalizedString FeatureNameDeserializer::type_name() const { return msg::format(msgAFeatureName); }

    Optional<std::string> FeatureNameDeserializer::visit_string(Json::Reader& r, StringView sv) const
    {
        if (!IdentifierDeserializer::is_ident(sv))
        {
            r.add_generic_error(
                type_name(),
                msg::format(msgParseFeatureNameError, msg::package_name = sv, msg::url = docs::manifests_url));
        }
        return sv.to_string();
    }

    const FeatureNameDeserializer FeatureNameDeserializer::instance;

    LocalizedString ArchitectureDeserializer::type_name() const { return msg::format(msgACpuArchitecture); }

    Optional<Optional<CPUArchitecture>> ArchitectureDeserializer::visit_string(Json::Reader& r, StringView sv) const
    {
        auto maybe_cpu_architecture = to_cpu_architecture(sv);
        if (maybe_cpu_architecture.has_value())
        {
            return maybe_cpu_architecture;
        }

        r.add_generic_error(type_name(),
                            msg::format(msgInvalidArchitectureValue,
                                        msg::value = sv,
                                        msg::expected = all_comma_separated_cpu_architectures()));
        return Optional<CPUArchitecture>{nullopt};
    }

    const ArchitectureDeserializer ArchitectureDeserializer::instance;

    LocalizedString Sha512Deserializer::type_name() const { return msg::format(msgASha512); }

    Optional<std::string> Sha512Deserializer::visit_string(Json::Reader& r, StringView sv) const
    {
        if (sv.size() == 128 && std::all_of(sv.begin(), sv.end(), ParserBase::is_hex_digit))
        {
            return sv.to_string();
        }

        r.add_generic_error(type_name(), msg::format(msgInvalidSha512, msg::sha = sv));
        return std::string();
    }

    const Sha512Deserializer Sha512Deserializer::instance;
}

#pragma once

namespace vcpkg::Json
{
    struct JsonStyle;
    enum class ValueKind : int;
    struct Value;
    struct Object;
    struct ParsedJson;
    struct Array;
    struct Reader;
    template<class Type>
    struct IDeserializer;
}

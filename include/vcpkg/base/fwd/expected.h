#pragma once

#include <vcpkg/base/fwd/messages.h>

namespace vcpkg
{
    struct Unit;
    struct ExpectedLeftTag;
    struct ExpectedRightTag;

    template<class T>
    struct ExpectedHolder;
    template<class T>
    struct ExpectedHolder<T&>;

    template<class T, class Error>
    struct ExpectedT;

    template<class T>
    using ExpectedL = ExpectedT<T, LocalizedString>;
}

#include <vcpkg/base/expected.h>

namespace
{
    using namespace vcpkg;
    DECLARE_AND_REGISTER_MESSAGE(SystemApiNotAnErrorMessage, (), "", "not an error");
    DECLARE_AND_REGISTER_MESSAGE(SystemApiErrorMessage,
                                 (msg::system_api, msg::exit_code, msg::error_msg),
                                 "",
                                 "calling {system_api} failed with {exit_code} ({error_msg})");
}

namespace vcpkg
{
    const SystemApiError SystemApiError::empty{""};

    std::string SystemApiError::to_string() const
    {
        std::string tmp;
        to_string(tmp);
        return tmp;
    }

    void SystemApiError::to_string(std::string& target) const
    {
        if (api_name.empty())
        {
            target.append(msg::format(msgSystemApiNotAnErrorMessage).extract_data());
        }

        target.append(msg::format_error(msgSystemApiErrorMessage,
                                        msg::system_api = api_name,
                                        msg::exit_code = error_value,
                                        msg::error_msg = std::system_category().message(static_cast<int>(error_value)))
                          .extract_data());
    }
}

#include <catch2/catch.hpp>

#include <vcpkg/install.h>

using namespace vcpkg;
namespace Integrate = vcpkg::Commands::Integrate;

TEST_CASE ("find_targets_file_version", "[integrate]")
{
    constexpr static StringLiteral DEFAULT_TARGETS_FILE = R"xml(
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
    <!-- version 1 -->
    <PropertyGroup>
        <VCLibPackagePath Condition="'$(VCLibPackagePath)' == ''">$(LOCALAPPDATA)\vcpkg\vcpkg.user</VCLibPackagePath>
    </PropertyGroup>
    <Import Condition="'$(VCLibPackagePath)' != '' and Exists('$(VCLibPackagePath).props')" Project="$(VCLibPackagePath).props" />
    <Import Condition="'$(VCLibPackagePath)' != '' and Exists('$(VCLibPackagePath).targets')" Project="$(VCLibPackagePath).targets" />
</Project>
)xml";

    auto res = Integrate::find_targets_file_version(DEFAULT_TARGETS_FILE);
    REQUIRE(res.has_value());
    CHECK(*res.get() == 1);

    res = Integrate::find_targets_file_version("<!-- version 12345 -->");
    REQUIRE(res.has_value());
    CHECK(*res.get() == 12345);

    res = Integrate::find_targets_file_version("<!-- version <!-- version 1 -->");
    REQUIRE(res.has_value());
    CHECK(*res.get() == 1);

    res = Integrate::find_targets_file_version("<!-- version 32 <!-- version 1 -->");
    REQUIRE(res.has_value());
    CHECK(*res.get() == 1);

    res = Integrate::find_targets_file_version("<!-- version 32 --> <!-- version 1 -->");
    REQUIRE(res.has_value());
    CHECK(*res.get() == 32);

    res = Integrate::find_targets_file_version("<!-- version 12345  -->");
    CHECK_FALSE(res.has_value());

    res = Integrate::find_targets_file_version("<!--  version 12345 -->");
    CHECK_FALSE(res.has_value());

    res = Integrate::find_targets_file_version("<!-- version -12345 -->");
    CHECK_FALSE(res.has_value());

    res = Integrate::find_targets_file_version("<!-- version -12345 --> <!-- version 1 -->");
    REQUIRE(res.has_value());
    CHECK(*res.get() == 1);

    res = Integrate::find_targets_file_version("<!-- version unexpected --> <!-- version 1 -->");
    REQUIRE(res.has_value());
    CHECK(*res.get() == 1);

    res = Integrate::find_targets_file_version("<!-- ver 1 -->");
    CHECK_FALSE(res.has_value());
}

TEST_CASE ("get_bash_source_completion_lines", "[integrate]")
{
    constexpr static StringLiteral default_bashrc = R"sh(
# ~/.bashrc: executed by bash(1) for non-login shells.
# see /usr/share/doc/bash/examples/startup-files (in the package bash-doc)
# for examples

# If not running interactively, don't do anything
case $- in
    *i*) ;;
    *) return;;
esac

# enable programmable completion features (you don't need to enable
# this, if it's already enabled in /etc/bash.bashrc and /etc/profile
# sources /etc/bash.bashrc).
if ! shopt -oq posix; then
    if [ -f /usr/share/bash-completion/bash_completion ]; then
        . /usr/share/bash-completion/bash_completion
    elif [ -f /etc/bash_completion ]; then
        . /etc/bash_completion
    fi
fi

if [ -f "$HOME/.profile" ]; then
    source .profile
fi
)sh";

    CHECK(Integrate::get_bash_source_completion_lines(default_bashrc) == std::vector<std::string>{});

    constexpr static StringLiteral source_line_1 = "source /blah/bloop/scripts/vcpkg_completion.bash";
    constexpr static StringLiteral source_line_2 = "source /floop/scripts/vcpkg_completion.bash";

    std::string with_bash_completion = default_bashrc;
    with_bash_completion.append(source_line_1.begin(), source_line_1.end());
    with_bash_completion.push_back('\n');

    CHECK(Integrate::get_bash_source_completion_lines(with_bash_completion) == std::vector<std::string>{source_line_1});

    with_bash_completion.append(source_line_2.begin(), source_line_2.end());
    with_bash_completion.push_back('\n');

    CHECK(Integrate::get_bash_source_completion_lines(with_bash_completion) ==
          std::vector<std::string>{source_line_1, source_line_2});

    with_bash_completion.append("unrelated line\n");

    CHECK(Integrate::get_bash_source_completion_lines(with_bash_completion) ==
          std::vector<std::string>{source_line_1, source_line_2});

    CHECK(Integrate::get_bash_source_completion_lines("source nonrelated/vcpkg_completion.bash") ==
          std::vector<std::string>{});
    CHECK(Integrate::get_bash_source_completion_lines("  source /scripts/vcpkg_completion.bash") ==
          std::vector<std::string>{"source /scripts/vcpkg_completion.bash"});

    CHECK(Integrate::get_bash_source_completion_lines("#source /scripts/vcpkg_completion.bash") ==
          std::vector<std::string>{});

    CHECK(Integrate::get_bash_source_completion_lines("mysource /scripts/vcpkg_completion.bash") ==
          std::vector<std::string>{});
}

TEST_CASE ("get_zsh_autocomplete_data", "[integrate]")
{
    constexpr static StringLiteral zshrc = R"sh(
source ~/.profile

if [ -z "${HOMEBREW_PREFIX+x}" ]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
fi
eval "$(ssh-agent)"

alias -g kill-gpg='gpgconf --kill gpg-agent'
. "$HOME/.cargo/env"
)sh";

    auto res = Integrate::get_zsh_autocomplete_data(zshrc);
    CHECK(res.source_completion_lines == std::vector<std::string>{});
    CHECK(!res.has_bashcompinit);
    CHECK(!res.has_autoload_bashcompinit);

    constexpr static StringLiteral source_line_1 = "source /blah/bloop/scripts/vcpkg_completion.zsh";
    constexpr static StringLiteral source_line_2 = "source /floop/scripts/vcpkg_completion.zsh";
    constexpr static StringLiteral bash_source_line = "source /scripts/vcpkg_completion.bash";

    std::string my_zshrc = zshrc;
    my_zshrc.append(source_line_1.begin(), source_line_1.end());
    my_zshrc.push_back('\n');
    res = Integrate::get_zsh_autocomplete_data(my_zshrc);
    CHECK(res.source_completion_lines == std::vector<std::string>{source_line_1});
    CHECK(!res.has_bashcompinit);
    CHECK(!res.has_autoload_bashcompinit);

    my_zshrc.append(source_line_2.begin(), source_line_2.end());
    my_zshrc.push_back('\n');
    res = Integrate::get_zsh_autocomplete_data(my_zshrc);
    CHECK(res.source_completion_lines == std::vector<std::string>{source_line_1, source_line_2});
    CHECK(!res.has_bashcompinit);
    CHECK(!res.has_autoload_bashcompinit);

    my_zshrc.append(bash_source_line.begin(), bash_source_line.end());
    my_zshrc.push_back('\n');
    res = Integrate::get_zsh_autocomplete_data(my_zshrc);
    CHECK(res.source_completion_lines == std::vector<std::string>{source_line_1, source_line_2});
    CHECK(!res.has_bashcompinit);
    CHECK(!res.has_autoload_bashcompinit);

    my_zshrc.append("bashcompinit\n");
    res = Integrate::get_zsh_autocomplete_data(my_zshrc);
    CHECK(res.source_completion_lines == std::vector<std::string>{source_line_1, source_line_2});
    CHECK(res.has_bashcompinit);
    CHECK(!res.has_autoload_bashcompinit);

    my_zshrc.append("autoload bashcompinit\n");
    res = Integrate::get_zsh_autocomplete_data(my_zshrc);
    CHECK(res.source_completion_lines == std::vector<std::string>{source_line_1, source_line_2});
    CHECK(res.has_bashcompinit);
    CHECK(res.has_autoload_bashcompinit);

    res = Integrate::get_zsh_autocomplete_data("autoload bashcompinit");
    CHECK(res.source_completion_lines == std::vector<std::string>{});
    CHECK(!res.has_bashcompinit);
    CHECK(res.has_autoload_bashcompinit);

    res = Integrate::get_zsh_autocomplete_data("autoloadoasdoif--ha------oshgfiaqwenrlan hasdoifhaodfbashcompinit");
    CHECK(res.source_completion_lines == std::vector<std::string>{});
    CHECK(!res.has_bashcompinit);
    CHECK(res.has_autoload_bashcompinit);

    res = Integrate::get_zsh_autocomplete_data("autoloadoasdoi hasdoifhaodfbashcompinitasdfjadofin");
    CHECK(res.source_completion_lines == std::vector<std::string>{});
    CHECK(!res.has_bashcompinit);
    CHECK(res.has_autoload_bashcompinit);

    res = Integrate::get_zsh_autocomplete_data("myautoload bashcompinit");
    CHECK(res.source_completion_lines == std::vector<std::string>{});
    CHECK(!res.has_bashcompinit);
    CHECK(!res.has_autoload_bashcompinit);

    res = Integrate::get_zsh_autocomplete_data("bashcompinit");
    CHECK(res.source_completion_lines == std::vector<std::string>{});
    CHECK(res.has_bashcompinit);
    CHECK(!res.has_autoload_bashcompinit);

    res = Integrate::get_zsh_autocomplete_data("asdf && blah && bashcompinit");
    CHECK(res.source_completion_lines == std::vector<std::string>{});
    CHECK(res.has_bashcompinit);
    CHECK(!res.has_autoload_bashcompinit);

    res = Integrate::get_zsh_autocomplete_data("daslknfd bashcompinit");
    CHECK(res.source_completion_lines == std::vector<std::string>{});
    CHECK(!res.has_bashcompinit);
    CHECK(!res.has_autoload_bashcompinit);

    res = Integrate::get_zsh_autocomplete_data("# && bashcompinit");
    CHECK(res.source_completion_lines == std::vector<std::string>{});
    CHECK(!res.has_bashcompinit);
    CHECK(!res.has_autoload_bashcompinit);
}

# Vcpkg: 总览

[English Overview](README.md)

Vcpkg 可帮助您在 Windows、 Linux 和 MacOS 上管理 C 和 C++ 库。
这个工具和生态链正在不断发展，我们一直期待您的贡献！

你可以通过参阅存储库 https://github.com/microsoft/vcpkg 来进行功能讨论，问题跟踪和完善vcpkg的库。

# Vcpkg-tool: 总览

此存储库包含了以前位于 https://github.com/microsoft/vcpkg 下的"toolsrc"目录中的内容，并且支持构建。


# vcpkg-artifacts

vcpkg-artifacts 目前还处于 '预览' 阶段 -- 从现在到'发布'(在采纳用户反馈的基础上)期间,它的内容一定会发生变化。

你可以使用它, 但是请注意，我们可能会对格式、命令等做出更改。

把它作为你的 C\C++ 项目的主要配置清单时所需的配置状态。

它
 - 将自身集成到你的shell (Powershell, CMD, bash/zsh)中。
 - 可以根据自定义的清单文件来恢复二进制缓存的内容。
 - 提供可发现的接口

## 安装说明

虽然所有的平台上 `vcpkg-artifacts` 的用法是一致的，但是 安装/加载/删除 的用法会根据你使用的平台不同而略有改变。

`vcpkg-artifacts` 不会在您的环境中保留任何更改，也不会自动将它自己添加到您的初始环境变量当中。 如果您只想将其加载到一个窗口上，你只需执行脚本即可。 也可以通过手动将它添加到你的配置文件的方法来将它加载到每个新窗口上。

<hr>

## 安装/使用/删除

| 操作系统             | 安装                                             | 使用                   | 删除                             |
|---------------------|-------------------------------------------------|-----------------------|---------------------------------|
| **PowerShell/Pwsh** |`iex (iwr -useb https://aka.ms/vcpkg-init.ps1)`              |` . ~/.vcpkg/vcpkg-init.ps1`          | `rmdir -recurse -force ~/.vcpkg`          |
| **Linux/OSX**       |`. <(curl https://aka.ms/vcpkg-init.sh -L)`                  |` . ~/.vcpkg/vcpkg-init`          | `rm -rf ~/.vcpkg`                  |
| **CMD Shell**       |`curl -LO https://aka.ms/vcpkg-init.cmd && .\vcpkg-init.cmd` |`%USERPROFILE%\.vcpkg\vcpkg-init.cmd` | `rmdir /s /q %USERPROFILE%\.vcpkg` |

## 术语解释

| 术语       | 解释                                                 |
|------------|-----------------------------------------------------|
| `artifact` | 存储构建工具或其组件的存档 (.zip or .tar.gz-like), 包 (.nupkg, .vsix) 和二进制缓存. |
| `artifact metadata` | 对一个或者多个工件的位置描述的元数据，还包括对于每一个所部署的工件关于主机架构，目标架构或者其他属性规则的描述。|
| `artifact identity` | 一个短字符串，唯一可被引用来描述给定的工件(和它的元数据) . 它可以是以下几种形式：:<br> `full/identity/path` - 表示一个工件在源中的完整路径<br>`sourcename:full/identity/path` - 表示一个工件在指定前缀名称源中的完整路径<br>`shortname` - 表示一个 工件在源中的简写名称<br>`sourcename:shortname` - 表示 一个工件在指定前缀名称源中的简写名称<br>简写名称是基于所给源中最短的唯一标识路径生成的。 |
| `artifact source` | 它也被称为 “原料”. 它用来存放多个工件(注意：目前只能有一个工件源). |
| `activation` | 表示获取一组特定的工件并使其能够在调用命令程序中使用的过程.|
| `versions` | 版本号被指定使用Semver语义格式。 如果没有指定特定的版本, 则假定最新的版本范围(*)。 可以使用npm semver匹配语法来指定版本或者版本范围。 当存储版本时，可以使用指定的版本范围来存储，一个空格和所要找寻的版本即可。(即：第一个版本时所要求的版本，第二个时所安装的版本。不需要单独指定文件。) |


# 贡献

请参阅我们的 [贡献准则](https://github.com/microsoft/vcpkg-tool/blob/main/README_zh_CN.md#contributing) 了解更多详细信息。

该项目采用了 [Microsoft 开源行为准则][contributing:coc]。
获取更多信息请查看 [行为准则 FAQ][contributing:coc-faq] 或联系 [opencode@microsoft.com](mailto:opencode@microsoft.com) 提出其他问题或意见。

[贡献:提交问题]: https://github.com/microsoft/vcpkg/issues/new/choose
[贡献:提价-拉取请求]: https://github.com/microsoft/vcpkg/pulls
[贡献:行为准则]: https://opensource.microsoft.com/codeofconduct/
[贡献:行为准则-常见问题]: https://opensource.microsoft.com/codeofconduct/

## Windows 贡献前提条件

* Install Visual Studio and the C++ workload
* Install Node.JS by downloading a 16.x copy from https://nodejs.org/en/
* `npm install -g @microsoft/rush`

## Ubuntu 22.04 贡献前提条件
```
curl -fsSL https://deb.nodesource.com/setup_16.x | sudo -E bash -
sudo apt update
sudo apt install nodejs cmake ninja-build gcc build-essential git zip unzip
sudo npm install -g @microsoft/rush
```

# 开源协议

在此存储库中使用的代码均遵循 [MIT License](LICENSE.txt)。 测试包含的第三方库的代码则如 `NOTICE.txt` 所述。

# 商标

该工程可能会包含一些项目，产品或者服务的商标或徽标。 Microsoft 商标或徽标的使用必须遵守 [Microsoft 商标和品牌指南](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general). 在此项目的修改版本中使用 Microsoft 的商标和徽标时，不得造成混淆或暗示任何关于 Microsoft 的赞助。 对于第三方商标和徽标的使用必须遵守第三方的政策。

# 数据收集

vcpkg 会收集使用情况数据，以帮助我们改善您的体验。
Microsoft 收集的数据是匿名的。
您也可以通过以下步骤禁用数据收集：
- 将选项 `-disableMetrics` 传递给 bootstrap-vcpkg 脚本并重新运行此脚本
- 向 vcpkg 命令传递选项 `--disable-metrics`
- 设置环境变量 `VCPKG_DISABLE_METRICS`

请在 [https://learn.microsoft.com/vcpkg/about/privacy](https://learn.microsoft.com/vcpkg/about/privacy) 中了解有关 vcpkg 数据收集的更多信息。

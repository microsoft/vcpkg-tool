# Vcpkg: 개요

Vcpkg는 Windows와 Linux, MacOS에서 C와 C++ 라이브러리를 관리하는 것을 도와줍니다.
이 도구와 생태계는 끊임없이 진화하고 있으며, 우리는 항상 기여를 환영합니다!

모든 기능 토론, 이슈 트래킹과 사용 가능한 라이브러리에 대해서는 메인 레포지토리 https://github.com/microsoft/vcpkg 를 참고해주세요.

# Vcpkg-tool: 개요

이 레포지토리는 이전 https://github.com/microsoft/vcpkg "toolsrc" 트리의 콘텐츠와 빌드 지원이 포함되어 있습니다.

# vcpkg-artifacts

vcpkg-artifacts는 현재 '미리보기' 단계입니다. -- 지금부터 도구가 '릴리즈'될 때까지 피드백을 바탕으로 
확실한 변화가 있을 것입니다.

사용할 수 있지만 형식, 커맨드 등이 바뀔 수 있어 미리 경고합니다.

다음을 C/C++ 프로젝트를 위한 매니페스트 기반의 원하는 상태 구성으로써 생각해보세요.

다음
 - 당신의 쉘(PowerShell, CMD, bash/zsh)에 자체통합
 - 코드를 따르는 매니페스트에 따라 아티팩트를 복원할 수 있음
 - 쉽게 찾을 수 있는 인터페이스 제공

## 설치

`vcpkg-artifacts`의 사용법은 모든 플랫폼에서 동일하지만, 설치/로드/제거는 사용하는 플랫폼에 따라 약간 다릅니다.

`vcpkg-artifacts`는 환경에 대한 변경 사항을 유지하지 않으며, 시작 환경에 자동으로 추가되지도 않습니다. 
창에서 로드하려면 스크립트를 실행하기만 하면 됩니다. 프로필에 수동으로 추가하면 모든 새 창에서 로드됩니다.

<hr>

## 설치/사용/제거

| OS                  | 설치                                                        | 사용                                 | 제거                                |
|---------------------|-------------------------------------------------------------|--------------------------------------|------------------------------------|
| **PowerShell/Pwsh** |`iex (iwr -useb https://aka.ms/vcpkg-init.ps1)`              |` . ~/.vcpkg/vcpkg-init.ps1`          | `rmdir -recurse -force ~/.vcpkg`   |
| **Linux/OSX**       |`. <(curl https://aka.ms/vcpkg-init.sh -L)`                  |` . ~/.vcpkg/vcpkg-init`              | `rm -rf ~/.vcpkg`                  |
| **CMD Shell**       |`curl -LO https://aka.ms/vcpkg-init.cmd && .\vcpkg-init.cmd` |`%USERPROFILE%\.vcpkg\vcpkg-init.cmd` | `rmdir /s /q %USERPROFILE%\.vcpkg` |

## 용어집

| 용어       | 설명                                                |
|------------|-----------------------------------------------------|
| `artifact` | 빌드 도구나 구성 요소가 저장되는 아카이브(.zip 또는 .tar.gz), 패키지(.nupkg, .vsix) 바이너리입니다. |
| `artifact metadata` | 호스트 아키텍처, 타깃 아키텍처 또는 기타 속성을 선택하여 배포되는 규칙을 설명하는 하나 이상의 아티팩트의 위치에 대한 설명|
| `artifact identity` | 주어진 아티팩트(및 해당 메타데이터)가 참조될 수 있는 별칭을 고유하게 설명하는 짧은 문자열입니다. 다음 형식 중 하나를 가질 수 있습니다.<br> `full/identity/path` - 내장된 아티팩트 소스에 있는 아티팩트의 전체 ID<br>`sourcename:full/identity/path` - sourcename 접두사로 지정된 아티팩트 소스에 있는 아티팩트의 전체 ID<br>`shortname` - 내장된 아티팩트 소스에 있는 아티팩트의 단축된 고유 이름<br>`sourcename:shortname` - sourcename 접두사로 지정된 아티팩트 소스에 있는 아티팩트의 단축된 고유 이름<br>단축된 이름은 주어진 소스에서 가장 짧은 고유 ID 경로를 기반으로 생성됩니다. |
| `artifact source` | "feed(피드)"로 알려져 있기도 합니다. 아티팩트 소스는 아티팩트를 찾기 위한 메타데이터를 호스팅하는 위치입니다. (_현재 소스는 하나뿐입니다_) |
| `activation` | 호출 명령 프로그램에서 사용할 특정 아티팩트 세트를 획득하고 활성화하는 프로세스입니다.|
| `versions` | 버전 번호는 Semver 형식을 사용하여 지정합니다. 특정 작업에 대한 버전이 지정되지 않으면 최신 버전(`*`)에 대한 범위가 가정됩니다. 버전 또는 버전 범위는 npm semver 매칭 구문을 사용하여 지정할 수 있습니다. 버전이 저장되면 지정된 버전 범위, 공백, 그리고 찾은 버전을 사용하여 저장할 수 있습니다. (즉, 첫 번째 버전은 요청된 것이고 두 번째 버전은 설치된 것입니다. 별도의 잠금 파일이 필요 없습니다.) |


# 기여

[메인 `README.md`](https://github.com/microsoft/vcpkg/blob/master/README.md)의 "contributing" 섹션을 참조해주세요.

이 프로젝트는 [마이크로소프트 오픈소스 행동강령][contributing:coc]을 채택했습니다.
자세한 내용은 [행동강령 FAQ][contributing:coc-faq]을 참고하거나
추가 질문이나 의견이 있으시면 메일을 주세요 [opencode@microsoft.com](mailto:opencode@microsoft.com)

[contributing:submit-issue]: https://github.com/microsoft/vcpkg/issues/new/choose
[contributing:submit-pr]: https://github.com/microsoft/vcpkg/pulls
[contributing:coc]: https://opensource.microsoft.com/codeofconduct/
[contributing:coc-faq]: https://opensource.microsoft.com/codeofconduct/

## Windows 기여 전제 조건

* Visual Studio및 C++ 워크로드 설치
* https://nodejs.org/en/ 에서 사본 다운로드를 통한 Node.JS 16.x 설치
* `npm install -g @microsoft/rush`

## Ubuntu 22.04 기여 전제 조건

```
curl -fsSL https://deb.nodesource.com/setup_16.x | sudo -E bash -
sudo apt update
sudo apt install nodejs cmake ninja-build gcc build-essential git zip unzip
sudo npm install -g @microsoft/rush
```

# 라이센스

이 레포지토리의 제품 코드는 [MIT License](LICENSE.txt)에 따라 라이센스가 부여됩니다. 테스트에는 `NOTICE.txt`에 문서화된 대로 타사 코드가 포함되어 있습니다.

# 상표

이 프로젝트에는 프로젝트, 제품 또는 서비스에 대한 상표 또는 로고가 포함될 수 있습니다. Microsoft
상표 또는 로고의 승인된 사용은
[Microsoft의 상표 및 브랜드 가이드라인](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general)의 적용을 받으며 이를 따라야 합니다.
이 프로젝트의 수정된 버전에서 Microsoft 상표 또는 로고를 사용하더라도 혼란을 야기하거나 Microsoft 후원을 암시해서는 안 됩니다.
제3자 상표 또는 로고를 사용하는 경우 해당 제3자 정책의 적용을 받습니다.
# 원격 측정

vcpkg는 사용자 경험을 개선하는 데 도움이 되도록 사용 데이터를 수집합니다.
Microsoft에서 수집하는 데이터는 익명입니다.
bootstrap-vcpkg 스크립트를 -disableMetrics로 재실행하거나,
명령줄에서 --disable-metrics를 vcpkg에 전달하거나,
VCPKG_DISABLE_METRICS 환경 변수를 설정하여 원격 측정을 옵트아웃할 수 있습니다.
# 소스에서 빌드하기

## 펌웨어 r57

8BitDo 공식 Lite 2 v1.06을 기반으로 Cortex-M0 Thumb 코드를 추가합니다. S 모드에서 센서·버튼 입력을 BLE 및 USB HID 마우스·키보드 보고서로 변환합니다. D 무선 연결은 별도의 BLE 게임패드로 등록됩니다. 공식 DAT에서 응용 프로그램 한 섹션을 읽어 패치하고 같은 형식으로 다시 패키징합니다. Yichip 형식 해독·재패키징에는 AeroModes의 8cryptdo 고정 리비전을 사용합니다. [8cryptdo 소스와 라이선스](https://github.com/aeromodes/8cryptdo/tree/aaffcebafa481631de9c1d722a5693d02234e43a)

Windows에서 Python 3.13, xPack Arm GNU Toolchain 15.2.1-1.1을 설치하고 저장소 루트에서 실행합니다. ARM_GCC_BIN은 arm-none-eabi-gcc.exe가 있는 실제 폴더로 바꿉니다. 아래 절차는 장치 연결 없이 실행할 수 있습니다.

~~~powershell
python -m pip install pyelftools unicorn
$env:ARM_GCC_BIN = 'C:\Toolchains\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin'
python tools/prepare_sources.py
python tools/build.py --ble-base
python tools/package_patch.py --ble-base
python tools/build_ble_controls.py --home-power --public
python -m unittest discover -s tests -p test_public_r57.py
~~~

| 이미지 | 크기 | SHA-256 |
| --- | ---: | --- |
| [공식 Lite 2 v1.06 DAT](http://dl.8bitdo.com:8080/firmwareFile/upload/8807004b-61a7-49f4-877f-f42d8f9cba70.dat) | 81,436 B | `121c1df789577997572373d049bfa245eb1fdae231223b0bb37463b257d3e27f` |
| r57 DAT | 90,140 B | `cf5377bc9b2257df86cca2b2b9cc7389dac7ac92bc844076c07c03755d6800c4` |

앞의 두 빌드 단계는 BLE 코드가 사용하는 r50 메모리 배치를 재현하고 중간 이미지 해시를 검사합니다. 마지막 단계가 두 BLE 서비스와 HOME 2초 종료를 포함한 `build/Lite2AirMouse-r57-home-power-public.dat`를 만듭니다.

공개 펌웨어는 네 스틱 축의 중립값으로 128을 사용합니다. 각 사용자는 Android 앱에서 자기 슬롯의 영점과 최대 범위를 따로 저장할 수 있습니다.

기기 없이 배포본에서 같은 DAT를 만들려면 Windows 키트 폴더에서 다음을 실행합니다.

~~~powershell
.\Lite2Updater.exe --prepare-only .\Lite2AirMouse-r57.dat
Get-FileHash .\Lite2AirMouse-r57.dat
~~~

최초 실행 시 공식 v1.06 DAT와 제조사 Windows 도구 ZIP을 내려받습니다. 오프라인 재사용 시에는 검증된 두 파일을 `Lite2-v1.06-official.dat`, `8BitDo_Firmware_Updater_Win.zip` 이름으로 같은 폴더에 두고 `--cache 폴더경로`를 지정할 수 있습니다. 제조사 ZIP의 SHA-256은 `88fd6af7eb62a33546415db75a40907ef9120d14f10c6cd7a4337ea59994a26f`입니다.

## Android APK와 ARM64 도우미

앱은 Android API 26 이상을 대상으로 합니다. 검증 환경은 Windows, JDK 21, Android SDK Platform 35 및 Build Tools 35.0.0입니다. SDK 루트는 해당 버전을 포함하는 경로로 지정합니다. 빌드 스크립트는 개발 서명 키를 로컬에 생성합니다. 직접 빌드한 APK와 공개 APK는 서명이 다를 수 있습니다.

~~~powershell
New-Item -ItemType Directory -Force work/android-bridge
zig cc -target aarch64-linux-musl -Os -Wall -Wextra -Werror -static android/bridge/lite2-pointer-bridge.c -o work/android-bridge/lite2-pointer-bridge
.\android\build.ps1 -Jdk 'C:\Java\jdk-21' -Sdk 'C:\Android\sdk'
~~~

도우미는 Zig 0.14.1로 빌드합니다. APK는 `android/build/Lite2Control-debug.apk`에 생성됩니다. 앱과 도우미는 태블릿 안에서 세션별 토큰으로 연결합니다. RAW와 슬롯에서의 입력 경로는 [입력 도우미 설명](../android/bridge/README.md)을 참고하세요.

## Windows 업데이트 프로그램 및 릴리스 키트

Windows x64 Python 환경과 .NET Framework 4.x의 x86 C# 컴파일러가 필요합니다. 위의 펌웨어·앱·도우미를 먼저 빌드합니다.

~~~powershell
python -m pip install pyinstaller
python tools/make_release.py
~~~

`work/public-release-r57/assets`에 Windows 키트, Android 키트, APK와 SHA256SUMS.txt가 생성됩니다. Windows 키트에는 자체 실행 프로그램, x86 USB 기록 프로그램, r57 변경분이 들어 있습니다. 실행 시 제조사 입력 파일과 조립한 DAT의 해시를 대조합니다. 실제 기록은 D 모드 USB 기기를 확인하고 사용자가 R57을 입력한 뒤에 시작됩니다.

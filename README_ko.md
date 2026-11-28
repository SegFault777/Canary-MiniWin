<p align="center">
  <img src="docs/logo/MiniWin_LOGO_x6.png" alt="MiniWin 1.1" width="768" height="192">
</p>

# MiniWin

**Mini OS, Many Things.**

외부 의존성이 전혀 없는 32비트 x86 데스크톱 운영체제입니다. MBR 디스크 이미지 하나로 부팅해 곧바로 윈도우 95 느낌의 GUI가 뜹니다.
리얼 모드에서 보호 모드로 직접 넘어가고, VGA·마우스·키보드 스택과 자체 디스크 드라이버(IDE, SATA/AHCI, SCSI, NVMe)를 처음부터 만들었으며,
libc도 부트로더 프레임워크도 남의 커널 코드도 쓰지 않는 창 시스템을 갖췄습니다.

**현재 버전: 1.1-alpha-14** ([변경 내역](docs/en/project/CHANGELOG.md), 영어). QEMU에서 개발·시험했고,
VMware / VirtualBox는 [가상 머신 안내](docs/en/user/VIRTUAL-MACHINES.md)(영어)를 보세요.

**다른 언어:** [English](README.md) · 한국어 문서 목차: [docs/ko/README.md](docs/ko/README.md)

![바탕화면](tools/test/golden/s00_desktop.png)

> 이 문서는 영어 [README.md](README.md)의 한국어판입니다. 구현 세부는 [개발자 문서](docs/en/dev/ARCHITECTURE.md)(영어)에 있습니다.

## 무엇이 들어 있나

아래는 모두 **1.44 MB 디스크 이미지 하나**(`build/os-image.img`)로 가상 머신에서 돌아갑니다. 빌려 온 것은 없습니다: libc도, 부트로더 프레임워크도,
다른 시스템의 커널 코드도 없습니다. 전체 안내는 [ARCHITECTURE.md](docs/en/dev/ARCHITECTURE.md), 파일별 설명은 [SOURCE-MAP.md](docs/en/dev/SOURCE-MAP.md)(둘 다 영어)입니다.

**부트와 하드웨어**
- 512바이트 MBR과 2단계 로더(리얼 모드 → 보호 모드를 직접 전환, A20, GDT, BIOS로 찾는 진짜 VBE 트루컬러 모드). 모든 부트 오류에는 코드와 이름이 있습니다
  (`MW-B01` … `MW-K10`, 표는 [BOOT-PROCESS.md](docs/en/dev/BOOT-PROCESS.md)).
- 화면: 32비트 트루컬러 **640x480**(또는 **854x480**, **320x200**; 화면 속성에서 고르고 다시 시작해도 유지됩니다).
- 디스크: IDE/ATA와 ATAPI, **SATA(AHCI)**, **LSI SCSI**, **NVMe**, 부트 플로피. MiniWin이 올라온 디스크를 스스로 찾습니다. PCI 열거, ACPI 전원 끄기,
  이름 있는 오류 화면을 보여 주는 CPU 예외 처리기, COM1 디버그 로그.
- 입력: PS/2와 **USB**(xHCI, EHCI, OHCI: 키보드, 휠 마우스, 태블릿).
- 네트워크: 인텔 e1000 계열과 RTL8139 카드(폴링), ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP.
- **TLS 1.3/1.2**와 **QUIC 위의 HTTP/3**. 필요한 암호 기능(SHA-2, AES-GCM, X25519, P-256/P-384, RSA/ECDSA, X.509 체인 검증 — 모질라 묶음에서 뽑은 루트
  198개의 내장 신뢰 목록, 호스트 이름·날짜 검사)을 모두 직접 구현했습니다.

**파일 시스템**
- **MWFS**(MiniWin 자체): 깊이 제한 없는 디렉터리, 커지는 파일, `/` 아래 다른 디스크는 `/mnt/<id>`, 빈 공간 비트맵, 파일별 CRC-32, 그리고
  **구조적으로 보장되는 충돌 안전성**(새 파일을 통째로 쓴 뒤 디렉터리 한 번 쓰기로 이름을 바꿈; 모든 동작의 모든 섹터 쓰기 뒤 전원 차단을 흉내 내 시험).
  유닉스식 트리(`/boot /dev /etc /bin /lib /sys /proc /var /home /progs …`)에서 시스템 폴더는 `root`만 바꿀 수 있습니다. `part`로 파티션을
  만들고 마운트하고 점검하고 고치며, `os-inst`로 다른 디스크나 파티션에 MiniWin을 설치합니다.

**데스크톱**(윈도우 95 느낌의 GUI; 빌드할 때 `THEME=...`으로 모양을 바꿀 수 있습니다)
- 진짜로 겹치는 창: 끌기, 가장자리로 크기 조절(커서가 방향을 알려 줍니다), 최소화, 최대화, 창마다 단추가 있는 작업표시줄, 폴더가 있는 시작 메뉴(`/home/start`)와
  *추가…* 마법사, 달력이 있는 시계, **오른쪽 클릭 메뉴**, 끌 수 있는 스크롤바, 휠.
- **대화상자도 창입니다** — 메모장의 "저장할까요?"·다른 이름으로 저장·도움말, MiniWeb 정보, "이 화면 크기를 유지할까요?", 글꼴 질문, 글꼴 추가:
  제목 막대로 끌고, `_ □ X`, 작업표시줄 단추가 있으며, 답하기 전에는 소유 창이 비활성입니다([DIALOGS.md](docs/en/dev/DIALOGS.md), 영어).
- 처음 시작하면 설정 마법사(언어, 사용권, 계정, 마지막 설정)와 **로그온** 창이 뜨고, `root`와 사용자 암호를 씁니다.
- 진짜 **한글** 입력기(2벌식)와 현대 한글 11,172자 전체의 비트맵 글꼴(갈무리), 설치할 수 있는 **TrueType 글꼴**(`Fonts` 폴더, *Install New Font…*),
  글꼴 크기 7~14, 자간. 화면 전체가 영어와 한국어로 한 번에 바뀝니다.

**앱**(모두 `.MWP`)
- **NOTEPAD** — 여러 문서를 각자 창으로, 디스크에 저장.
- **SETTING**(제어판) — 화면 속성, 날짜/시간, 마우스(포인터 모양과 속도), 글꼴, 키보드/입력기, 국가별 설정, 소리, 시스템, 전원.
- **MINIWEB** — 주소창, 기록, HTTPS와 HTTP/3, 직접 만든 **HTML5 / CSS3 / JavaScript** 엔진, 그림(PNG, JPEG, GIF), 오른쪽 클릭 메뉴가 있는 작은 브라우저.
- **MINISHELL** — 유닉스·윈도우식 명령(`ls cd cp mv rm mkdir cat part os-inst fetch lsusb run …`)이 있는 터미널, `root`와 `user`.
- **MINIFILES** — 파일 관리자(복사, 잘라내기, 붙여넣기, 이름 바꾸기, 삭제, 드라이브, 디스크에 MiniWin 설치).
- 이미지에 들어 있는 예제: **TETRIS**, **MINES**(지뢰찾기), **BROWSE**, **PAINT**, **MINICALC**, **GREETER**, **HELLO**, **RING3**, **CRASHTST**.

**프로그램과 SDK**
- 프로그램은 `.MWP` 파일입니다. 커널과 따로 컴파일해 디스크에 두면 OS가 읽어 와 실행하고, 고정된 시스템 호출 표로 OS와 대화합니다.
  `--ring3`로 빌드한 프로그램은 **링 3**(보호된 사용자 모드)에서 돌아 커널을 건드릴 수 없습니다.
- **SDK 3.0**(`sdk/`, [sdk/README.md](sdk/README.md)): `miniwin.h`(API v4), `mw_libc.h`(문자열, `mw_snprintf`, `mw_malloc`/`mw_free`가 있는 힙, `mw_rand`, `mw_qsort` …),
  `mw_gfx.h`, `mw_ui.h` 위젯 도구, 빌드·설치 스크립트, `hello.c`부터 테트리스와 지뢰찾기까지 예제. 프로그램은 60 KB까지 가능합니다.

## 아직 없는 것

MiniWin은 **알파**이며 취미·교육용 시스템이지 일상 작업용이 아닙니다. QEMU에서만 돌려 보았습니다. 솔직하게: USB 저장 장치와 USB 허브 없음, 레거시 BIOS만(UEFI 없음),
IPv4만, CPU 하나에 스케줄러 없음(한 번에 프로그램 하나, 커널이 모든 것을 폴링), 소켓 API 없음, 문서는 4 KB까지(메모장 버퍼), MiniWeb은 작은 엔진이지 범용 브라우저가 아님
(쿠키 아직 없음, WebP/SVG 없음, CSS3와 JavaScript 일부 빠짐). 전체 목록과 앞으로의 계획 — 프로세스 보호, USB 플래시 드라이브, FAT/ext 읽기, 스케줄러, IPv6 … — 은
[ROADMAP.md](docs/en/project/ROADMAP.md)(영어)에 있습니다.

## 빌드

`nasm`, 32비트를 만들 수 있는 `gcc`(64비트 데비안/우분투라면 `gcc-multilib`), `ld`, Pillow가 있는 `python3`(`python3-pil`)가 필요합니다.

```bash
./build.sh && tools/install_all.sh                 # 두 번째 단계가 아이콘과 데모 프로그램을 이미지에 넣습니다
./build.sh && tools/install_all.sh --no-demos      # GREETER와 SDK 예제 없이(빈 /progs)
THEME=dark ./build.sh && tools/install_all.sh      # 다른 모양(kernel/ui/themes/<이름>.h)
```

`install_all.sh`는 배포용 이미지 둘을 만듭니다. **`build/os-image.iso`**(기본값: 라이브 디스크로 쓰는 전체 시스템, 최대 5 MB; 부팅해서 오른쪽 아래 INSTALL을 더블클릭하면
디스크에 설치됩니다)와 **`build/os-image-floppy.img`**(1.44 MB 플로피 이미지에 담은 최소 시스템; VMware / VirtualBox의 "플로피 이미지 파일"용). `build/os-image.img`
자체는 정확히 **1440 KB**(1,474,560바이트 = 2880섹터, 플로피 한 장)입니다. `build.sh`는 부트 예산과 저장소 배치를 `boot/boot.asm`·`kernel/fs.h`와 같은 상수에서 뽑아 쓰며,
커널 `.bss`가 부트 스택과 만나거나, 커널이 로더 읽기 예산을 넘거나, 커널·아이콘 카탈로그·루트 파일 시스템이 겹치거나, 이미지가 정확한 자리에서 끝나지 않으면 조용히 넘어가지 않고
큰 소리로 실패합니다. `build/` 디렉터리는 git에 올라가지 않고, 현재 버전의 미리 빌드한 내려받기는 없습니다.

## 실행

```bash
qemu-system-i386 -drive file=build/os-image.img,format=raw
```

PC 스피커 경고음(메모장의 저장 안 한 변경 경고)을 **듣고** 싶다면 QEMU에 오디오 백엔드를 붙이세요: `-audiodev pa,id=snd0 -machine pcspk-audiodev=snd0`
(다른 플랫폼에서는 `pa` 대신 `dsound`, `coreaudio`, `sdl`; `qemu-system-i386 -audio-help`로 목록이 나옵니다).
네트워크 카드와 직렬 로그: `-netdev user,id=n0 -device rtl8139,netdev=n0 -serial file:serial.log`(`-nic user,model=e1000`도 됩니다).
같은 이미지가 IDE, AHCI(`-device ich9-ahci`), 플로피(`build/os-image-floppy.img`, `-drive if=floppy -boot a`), SCSI(`-device mptsas1068`), NVMe(`-device nvme`)에서 부팅합니다.
VMware / VirtualBox는 [VIRTUAL-MACHINES.md](docs/en/user/VIRTUAL-MACHINES.md)(영어)를 보세요.

## 조작

- 마우스: 클릭, 제목 막대나 가장자리 끌기, `_` / `□` / `X`, 오른쪽 클릭 메뉴, 휠로 스크롤.
- **오른쪽 Alt**: 입력 언어 전환(SETTING > 키보드 > 입력기에서 켠 언어만 돌아가며 선택됩니다).
- 메모장에서 **Ctrl+S / Ctrl+N / Ctrl+W**: 저장 / 새 문서 / 닫기.
- 테트리스: 방향키 이동·회전, 스페이스 하드 드롭, P 일시정지, R 다시 시작. 지뢰찾기: 왼쪽 클릭 열기, 오른쪽 클릭 깃발, 얼굴 클릭 새 게임.

## 프로젝트 구조

```
boot/            512바이트 MBR과 2단계 로더
kernel/          OS 본체: kernel.c(층 지도와 메인 루프)와 하위 시스템별 헤더
  ui/            셸: 테마, 위젯, 창, 작업표시줄, 시작 메뉴, 대화상자(dialog.h), 입력
  apps/          메모장, 설정, MiniWeb(web/), MiniShell, MiniFiles, 프로그램 창
  sys/           util, 언어·입력기, 전원, 부트
  usb/           xHCI / EHCI / OHCI와 HID 키보드·마우스·태블릿
programs/        커널과 따로 빌드하는 .mwp 프로그램(greeter 등)
sdk/             프로그램을 짜는 사람을 위한 SDK 3.0: include/, examples/, build.sh, install.sh
tools/           빌드·설치·생성 스크립트; tools/test/에 시험(호스트 시험, QEMU 시험, 화면 골든)
docs/            en/(전체 문서), ko/(한국어), logo/
third_party/     글꼴·아이콘 원본과 각각의 라이선스
```

시험은 [TESTING.md](docs/en/dev/TESTING.md)(영어)에 있고, `tools/test/check_docs.py`와 `check_version.sh`가 문서를 정직하게 지킵니다.

## 이름: MWP

여기 있는 모든 앱은 `.mwp`("**M**ini**W**in **P**rogram") 파일입니다. 메모장·설정·MiniWeb·MiniShell·MiniFiles는 로더보다 먼저 만들어져 커널 안에 있으면서 이름만 같이 씁니다.
GREETER, 테트리스처럼 `programs/`와 `sdk/examples/`에 있는 프로그램이 진짜 적재형입니다(`kernel/mwp.h`, [MWP-PROGRAMS.md](docs/en/dev/MWP-PROGRAMS.md)).

## 라이선스

MiniWin은 자유 소프트웨어이며 **GNU 일반 공중 사용 허가서 3판**(`GPL-3.0-only`)으로 배포됩니다. 전문은 `LICENSE`(한국어 안내: `LICENSE.ko.txt`)에 있고,
프로젝트의 모든 소스 파일에 `SPDX-License-Identifier: GPL-3.0-only` 줄이 들어 있습니다.

**아이콘 묶음**(`third_party/icon-bundle/`, 모든 디스크 이미지의 아이콘 카탈로그에도 들어갑니다)과 **로고**(`docs/logo/`)는 GPL이 아니라
저작자 SegFault777의 **CC BY-SA 4.0**입니다 — 저작자를 밝히고 같은 조건으로 공유하세요
(<https://creativecommons.org/licenses/by-sa/4.0/deed.ko>). 코드와 그림은 서로 다른 라이선스의 별개 저작물이며 디스크 이미지에 함께 담겨 배포됩니다.

함께 들어 있는 제3자 자료(갈무리·달무리 글꼴, CA 루트 인증서와 그로부터 만든 데이터 파일)는 각 저작자의 라이선스를 따릅니다.
`third_party/*/NOTICE.md`와 [LICENSING.md](docs/en/project/LICENSING.md)를 보세요.

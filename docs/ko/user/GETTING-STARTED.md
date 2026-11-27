# MiniWin 시작하기

MiniWin은 PC용 작은 32비트 운영체제입니다. 디스크 이미지로 시작해 창, 시작 메뉴, 텍스트 편집기, 파일 관리자, 터미널, 아주 작은 웹 브라우저가 있는
640x480 바탕화면을 보여 주고, 문서는 자기 디스크에 보관합니다. 리눅스나 윈도우 코드 없이 처음부터 만든 취미·교육용 OS이므로
일상 작업용이 아니라 가상 머신과 실험용입니다.

## 필요한 것

* **레거시 BIOS**를 쓰는 컴퓨터나 **가상 머신**("BIOS" 또는 "Legacy" 부팅, **UEFI 아님**).
* 약 **64 MB 메모리**(대략 9.2 MB 미만이면 시작을 거부합니다).
* 디스크 이미지를 **직접 빌드**하려면(미리 만든 내려받기는 없습니다): `nasm`, 32비트 코드를 만들 수 있는 `gcc`(데비안/우분투: `gcc-multilib`),
  Pillow가 설치된 `python3`(`python3-pil`)가 있는 리눅스 셸, 그리고 시험해 보려면 `qemu-system-x86`.

## 빌드하고 실행하기 (빠른 방법)

```sh
git clone https://github.com/SegFault777/MiniWin.git
cd MiniWin
./build.sh                  # 커널과 1440 KB 디스크 이미지를 만듭니다: build/os-image.img
tools/install_all.sh        # 아이콘 묶음과 데모 프로그램(GREETER와 SDK 예제)을 이미지에 넣습니다 (build.sh 뒤에는 항상 실행)
                            # --no-demos (또는 MW_NO_DEMOS=1)를 붙이면 데모를 빼고 아이콘만 넣습니다: /progs가 빕니다
qemu-system-i386 -drive format=raw,file=build/os-image.img
```

`install_all.sh`는 `build/os-image-floppy.img`도 만듭니다. 같은 시스템을 1.44 MB **플로피** 이미지로 만든 것으로, 플로피를 원하는 가상 머신용입니다.
게스트에 네트워크를 주려면 `-nic user,model=e1000`을 덧붙이세요.

## 다른 가상 머신 (VMware, VirtualBox)

이미지는 원시 디스크입니다. 변환해서 하드 디스크로 붙이고(또는 플로피 이미지를 플로피로 쓰고) 레거시 BIOS를 골라 부팅하세요.
정확한 명령, 지원하는 디스크 컨트롤러와 네트워크 카드는 [VIRTUAL-MACHINES.md](../../en/user/VIRTUAL-MACHINES.md)(영어)에 있습니다.
화면에 `MiniWin ERROR MW-...`로 시작하는 메시지가 나오면 [TROUBLESHOOTING.md](../../en/user/TROUBLESHOOTING.md)(영어)가 각각을 쉬운 말로 설명합니다.

## 처음 5분

1. 몇 초 뒤 바탕화면이 나타납니다. 왼쪽에 아이콘 한 줄, 아래에 작업표시줄이 있습니다.
2. **NOTEPAD를 더블클릭**하고 아무거나 입력한 뒤 **Ctrl+S**를 누르세요. 문서가 디스크 이미지에 저장되고 바탕화면에 그 문서의 새 아이콘이 생깁니다.
   (File > Save As에서 이름을 정할 수 있습니다.)
3. **MINISHELL을 더블클릭**하고 `fetch`를 입력한 뒤 Enter: MiniWin이 알아낸 컴퓨터 정보(CPU, 그래픽, 메모리, 디스크, 네트워크)가 나옵니다.
   `?`를 입력하면 모든 명령이 나옵니다.
4. 앱과 끄기/다시 시작은 **시작 메뉴**(왼쪽 아래)에 있습니다. 끄기는 가상 머신의 전원을 실제로 끕니다.
5. **MINIFILES를 더블클릭**하면 문서, 프로그램, 디스크가 보입니다.
6. 바탕화면의 **TETRIS**와 **MINES**(지뢰찾기)를 더블클릭해 보세요. SDK로 만든 예제 프로그램입니다.

저장한 것은 모두 디스크 이미지 파일 안에 있습니다. 문서를 지키려면 `build/os-image.img`를 복사해 두세요. `tools/install_all.sh`나 `./build.sh`를
다시 실행하면 새 이미지로 처음부터 시작합니다.

## 다음으로

* [DESKTOP-AND-APPS.md](../../en/user/DESKTOP-AND-APPS.md) -- 바탕화면, 창, 모든 앱 (영어)
* [TERMINAL.md](../../en/user/TERMINAL.md) -- MiniShell의 모든 명령 (영어)
* [FILES-AND-DISKS.md](../../en/user/FILES-AND-DISKS.md) -- 문서, 프로그램, 디스크, 다른 디스크에 설치하기 (영어)
* [FAQ.md](FAQ.md) -- 짧은 답변 (실제 PC? USB? 64비트?)

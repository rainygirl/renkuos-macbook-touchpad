# RenkuOS용 MacBook Touchpad

[English](README.md) · [한국어](README.ko.md) · [日本語](README.ja.md)

2010년형 MacBook7,1(USB 05ac:0236)의 원시 터치 좌표를 이용하는 터치패드
드라이버입니다. 한 손가락 이동, 탭 투 클릭, 물리 클릭, 버튼을 누른 채 하는
드래그, 두 손가락 세로·가로 스크롤을 지원합니다.

## 설치

```sh
pkgman add-repo https://pkgman.rainygirl.com/x86_64
pkgman refresh
pkgman install macbook_touchpad
```

설치 후 재부팅하세요. 저장소가 이미 등록돼 있다면 `add-repo`는 생략합니다.
Haiku 패키지 이름은 하이픈을 허용하지 않아 설치 이름은 `macbook_touchpad`입니다.

x86_64 RenkuOS `r1~beta6_hrev60072_269_dirty` 전용입니다. `macbook_fnkey`가
의존 패키지로 함께 설치되며 기존 기능 키·밝기 수정을 유지합니다. 다른 Mac
모델과 USB ID에는 적용하지 않습니다.

## 제거

```sh
pkgman uninstall macbook_touchpad
```

제거 후 재부팅하세요. `macbook_fnkey`의 USB HID 모듈로 돌아갑니다. 설치와
제거 모두 자동으로 재부팅하지는 않습니다.

## 라이선스

MIT, [LICENSE](LICENSE) 참고.

## AI 사용 고지

MacBook Touchpad의 일부는 AI 코딩 도구(OpenAI Codex, Anthropic Claude)의
도움을 받아 개발했습니다. 모든 코드는 작성자가 검토하고 테스트했습니다.

# GameServer — C++17 trên ServerEngine

Đây là phần đầu của việc chuyển HUNR2026 Java sang một game server C++ có thể mở
rộng. Đã có mã thực thi cho thế giới, nhân vật, di chuyển, chiến đấu, kỹ năng,
hiệu ứng và host dùng ServerEngine DLL. **Chưa chuyển toàn bộ 502 file Java và
chưa tương thích client Java cũ.** Không build hoặc chạy C++ trong lần chuyển này.

## Phần hiện có

- `GameDomain`: định nghĩa bất biến, trạng thái từng nhân vật, simulation clock,
  chọn mục tiêu, mana/cooldown, damage, heal, poison, stun, shield và stat modifier.
- `GameProtocol`: chuyển lệnh thành hành động game, ràng buộc nhân vật với session,
  trả trạng thái và sự kiện. Độc lập với DLL và socket.
- `GameServer`: console hoặc TCP localhost qua C ABI của ServerEngine; cập nhật
  theo nhịp 50 ms, quản lý lifetime và giới hạn hàng đợi.
- `game::legacy`: 24 ID kỹ năng và một số công thức/thời gian được đối chiếu Java.
  Module này giữ luật gốc để chuyển tiếp; demo chưa dùng toàn bộ luật đó.
- `content/demo.game`: ba mẫu Earth/Namek/Saiyan, một mob, bảy kỹ năng, bảy hiệu ứng.
  Chỉ số demo được chọn mới, không phải số cân bằng của game Java.

Không cần thêm subclass cho mỗi nhân vật. Nhân vật mới là một định nghĩa; kỹ năng
mới ghép các effect ID; cơ chế hiệu ứng mới đăng ký một handler. ID dạng chuỗi và
bộ thuộc tính động giúp mở rộng nội dung mà không phải kéo dài enum/lớp Player.
Vẫn có giới hạn CPU, RAM, số entity và lưu lượng; không có cam kết mở rộng vô hạn.

## Đọc và mở rộng

1. `content/demo.game` — nội dung game và ví dụ sửa dữ liệu.
2. `include/game/Content.h` — định nghĩa kỹ năng, hiệu ứng, nhân vật, map.
3. `include/game/World.h`, `src/game/World.cpp` — owner, cast flow, thời gian, effect lifecycle.
4. `src/net/DebugProtocol.cpp` — lệnh từ session đi vào world.
5. `src/net/WorldConnection.cpp` — vòng poll/tick nối với ServerEngine DLL.

Hướng dẫn chi tiết: [kiến trúc](docs/architecture.md),
[định dạng content](docs/content-format.md), [protocol và ví dụ thao tác](docs/protocol.md),
[luật kỹ năng Java đã đối chiếu](docs/legacy-skill-rules.md),
[phạm vi chuyển đổi còn lại](docs/migration-status.md).

## Bạn tự build

Mở thư mục này bằng Visual Studio hỗ trợ CMake, hoặc dùng x64 Developer PowerShell
có CMake/Ninja/MSVC. Cần `external/ServerEngine` đã checkout, `VCPKG_ROOT` trỏ tới
vcpkg; preset đọc manifest Boost/OpenSSL/SQLite của submodule. Không có dependency
binary, dữ liệu tài khoản hoặc tài nguyên game được thêm vào Git.

```powershell
cmake --preset ninja-debug
cmake --build --preset ninja-debug
ctest --preset ninja-debug

# Kiểm tra định nghĩa, console, hoặc server sau khi bạn đã build:
.\out\build\ninja-debug\GameServer.exe --validate-content
.\out\build\ninja-debug\GameServer.exe --console
.\out\build\ninja-debug\GameServer.exe --serve content/demo.game 7777
python .\scripts\debug_client.py --port 7777
```

Chạy từ thư mục repo. Có thể chạy từ thư mục binary vì file content được copy
trong bước build. DLL ServerEngine được copy cạnh executable trên Windows.
Các lệnh trên chỉ là hướng dẫn, chưa được chạy ở đây.

Chỉ build test domain/content/protocol, không cần ServerEngine hoặc vcpkg:

```powershell
cmake --preset domain-debug
cmake --build --preset domain-debug
ctest --preset domain-debug
```

## Source Java và dọn dữ liệu

Source tham chiếu nằm ở `Sample game old/HUNR_Server_Java/Hunr2026/src/main/java`.
Đã khôi phục và đối chiếu hash 502 file từ backup mới nhất có trong thư mục.
Schema 102 bảng và 9.302 INSERT của 52 bảng định nghĩa tĩnh được giữ dưới `sql/`;
dữ liệu tài khoản, nhân vật và lịch sử không nằm trong SQL được giữ cho Git.

**Việc xóa vật lý vẫn chờ bạn chạy:** công cụ tự động chặn lệnh xóa bằng
`blocked by policy`, kể cả khi chỉ xóa riêng `resources/`. Khoảng 498 MB cache,
resource, log và backup vẫn còn trên đĩa và đã được `.gitignore` loại khỏi Git.
Script đã chuẩn bị kiểm tra hash source và SQL trước/sau khi xóa:

```powershell
.\scripts\Clean-LegacyArtifacts.ps1 -WhatIf
.\scripts\Clean-LegacyArtifacts.ps1
.\scripts\Prepare-LegacySource.ps1 -VerifyOnly
```

Chi tiết nằm ở
[`SOURCE_RECOVERY.md`](Sample%20game%20old/HUNR_Server_Java/Hunr2026/docs/SOURCE_RECOVERY.md).
`pom.xml` và `application.properties` của Java vẫn chưa có; các script Java giữ
làm tham chiếu không có nghĩa Java build/runtime đã tái tạo được. Không commit
hoặc push trong lần làm việc này.

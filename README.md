# GameServer — C++17 trên ServerEngine

Source C++ chuyển từng phần HUNR2026 Java thành module để có thể thay luật, nội
dung, protocol và persistence khi làm game mới. **Theo lựa chọn hiện tại, server
dùng TCP framing 4 byte có sẵn của ServerEngine; client cần sửa phần mạng.
Không cần sửa ServerEngine hoặc dùng TCP_STREAM.**

Đã có code nạp dữ liệu gốc, cache, xác thực, tạo/lưu nhân vật và runtime chiến đấu.
**Chưa phải bản thay thế đầy đủ server Java:** host `--legacy` hiện nối module
phiên/tài khoản/cache, chưa cài full gameplay adapter cho `Player.enter`, quest,
trade, clan và boss. Xem [phạm vi thực tế](docs/migration-status.md).
Chưa build hoặc chạy C++ theo yêu cầu người dùng.

## Các phần hiện có

- `GameDomain`: ID chuỗi, thuộc tính động, kỹ năng ghép hiệu ứng; damage/heal/DoT/
  control/modifier/shield, thời gian và trạng thái riêng từng entity.
- `GameLegacy`: definition bất biến từ 52 bảng/9.302 dòng; 29 skill template theo
  class với 191 cấp; luật chiến đấu số nguyên và các ngoại lệ đối chiếu Java.
- `LegacyWorld`: actor/mob, kỹ năng đã học, cooldown, effect timeline, summon
  và giao diện bổ sung stat/movement/rule. Có đường chạy lại bằng JSON.
- `LegacyApplication`: hello, phiên bản, matrix/ECC, login, cache, tạo nhân vật
  và bàn giao quyền sở hữu cho `LegacyGameplay` có thể thay thế.
- `IdentityStore`: giao diện storage; SQLite cục bộ có password hash, uniqueness
  và revision để tránh ghi đè snapshot cũ. Chưa nối database MySQL Java.
- `GameModule` và `TcpHost`: xử lý game tách khỏi socket; engine tự chia gói.
- `MapGeometry`: code đọc địa hình từ đường dẫn triển khai ngoài repo.
  Không đưa ảnh, map binary hoặc dữ liệu người chơi vào Git.

## Đọc và mở rộng

1. [Kiến trúc](docs/architecture.md): ownership, module và effect extension.
2. [Protocol TCP/client](docs/client-wire-protocol.md): phần mạng client cần sửa.
3. [Application và storage](docs/legacy-application.md): xác thực và điểm nối game.
4. [Content](docs/legacy-content.md), [combat](docs/legacy-combat.md),
   [runtime](docs/legacy-world.md): dữ liệu và các ranh giới đã chuyển.
5. [Content cho game mới](docs/content-format.md): không bị khóa vào enum HUNR.

Không cần subclass riêng cho mỗi nhân vật. Kỹ năng mới có thể ghép handler
hiện có; cơ chế mới cần handler/system với validation và lifecycle rõ ràng.
Vẫn có giới hạn RAM, CPU và lưu lượng; không có cam kết mở rộng vô hạn.

## Bạn tự build và kiểm tra

Cần checkout submodule, x64 Developer PowerShell với CMake/Ninja/MSVC và
`VCPKG_ROOT`. Manifest dependency ở **root** gồm Boost/OpenSSL/SQLite/nlohmann-json.

```powershell
git submodule update --init --recursive
cmake --preset ninja-debug
cmake --build --preset ninja-debug
ctest --preset ninja-debug

Copy-Item config/hunr.example.json config/hunr.local.json
# Sửa cấu hình local trước khi dùng với client.
.\out\build\ninja-debug\GameServer.exe --validate-legacy config/hunr.local.json
.\out\build\ninja-debug\GameServer.exe --create-local-account config/hunr.local.json tester
.\out\build\ninja-debug\GameServer.exe --legacy config/hunr.local.json
.\out\build\ninja-debug\GameServer.exe --replay-legacy content/hunr/content.json content/hunr/replay.example.json
```

Đây là hướng dẫn; agent chưa chạy các lệnh build/runtime trên. Chạy từ root repo.
Đường dẫn content/database trong config được tính từ thư mục chứa config.
Cache/checksum/resource trong example là giá trị mẫu, không phải cấu hình Java
triển khai đã khôi phục. DLL được copy cạnh executable; content HUNR vẫn đọc
từ đường dẫn cấu hình.

Demo game mới vẫn có `--console`, `--serve content/demo.game 7777` và
`scripts/debug_client.py` dùng GAME/1, balance riêng. Preset `domain-debug`
chỉ build test demo/skill arithmetic, không tải dependency; `ninja-debug`
mới bao gồm test HUNR.

## Source tham chiếu và Git

Giữ 502 file Java dưới `Sample game old/HUNR_Server_Java/Hunr2026/src/main/java`,
schema SQL và định nghĩa tĩnh để đối chiếu. `content/hunr/content.json` được tạo
deterministic từ SQL nguồn, không chứa tài khoản hoặc lịch sử người chơi:

```powershell
python scripts/import_legacy_content.py --check
```

Resource, cache, backup, runtime database và config local bị loại khỏi Git.
Không khôi phục resource đã xóa. [Verification](docs/verification.md) ghi rõ
các kiểm tra source; không coi source review là bằng chứng runtime.

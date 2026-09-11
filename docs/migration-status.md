# Phạm vi chuyển đổi

Nguồn tham chiếu: 502 file Java, 96.465 dòng từ snapshot ngày 22/04/2026,
đã kiểm tra SHA-256 từng file. Bản C++ là phần đầu có hành vi thực, không phải bản
chuyển tự động đầy đủ của mọi package. Bảng này phân biệt code đã viết với hành
vi còn phải chuyển; không mục nào có bằng chứng build/runtime trong lần này.

| Phần Java | C++ hiện có | Công việc còn lại để đạt parity |
| --- | --- | --- |
| `Skill`, `SkillTemplate`, `SkillName` | Definition/runtime riêng; module legacy giữ 24 ID và công thức đã đối chiếu | Nạp mọi level từ `nr_skill`, mapping vào runtime, power/learn/upgrade rules |
| `Player`, `Info` | Entity, base/derived attributes, HP/MP, team, cooldown, effect | Equipment/options, growth, class/passive/set/charm/fusion và mọi rounding Java |
| `Zone.attackNpc/attackPlayer`, `Player.useSkill` | Cost, cooldown, target/range/AoE, damage/heal/CC/shield/DoT | Crit/random, chống one-shot, boss cap, MP attack, summon, revive, transform và skill đặc biệt |
| `Zone`, `TMap` | Một world owner, map bounds, movement speed, fixed tick | Map collision/resources, zone transfer, NPC, AI, mob respawn, pathfinding và spatial index |
| `Session`, `MessageHandler`, `Service` | Session ownership + GAME/1 debug codec + ServerEngine TCP | Legacy command/24-bit frame/XOR/Base64/batch/download handshake và client fixtures |
| `User`, repositories, DB entities | Schema + static content được bảo toàn, không nhập dữ liệu người chơi | Authentication, character load/create/save, async persistence, transactions, save compatibility |
| Item/shop/trade/drop/task/clan/event/boss/bot | Source Java và SQL định nghĩa giữ để đối chiếu | Port theo từng subsystem với owner, command, event và state riêng |
| Effect paint/image/resource metadata | Visual ID tách khỏi gameplay effect; metadata nguồn nằm trong SQL | Client renderer/assets và exact legacy effect messages |

Demo giữ tên ba nhóm nhân vật quen thuộc nhưng không nhận là bản cân bằng game cũ.
Shield demo hấp thụ một pool damage; protection trong Java có luật damage=1 và
break điều kiện riêng. Các helper legacy mới chỉ mô tả một phần luật đó, chưa
được gắn thành toàn bộ pipeline combat. Không có account database hoặc persistence
được kết nối; nhân vật debug chỉ tồn tại trong session.

Các bước chuyển tiếp có thể review riêng: content loader đọc schema đã giữ →
codec + handshake fixtures → account/character load chỉ đọc → một map có mob và
combat đúng Java → inventory/stat/options → save round-trip/transaction →
shop/trade/economy → quest/clan/events/bosses. Mỗi bước cần đối chiếu Java bằng dữ
liệu kiểm thử và chạy bằng toolchain do người dùng build.

`Sample game old/.../docs/architecture/` là tài liệu cũ đi cùng nguồn; một số
trang nhắc dự án khác `AH_BE_NRO_SERVER_C++` hoặc file không có trong bản chép.
Tài liệu hiện hành của checkout này là README và các trang dưới `docs/` ở root.

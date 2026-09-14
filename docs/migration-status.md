# Phạm vi chuyển đổi — 2026-09-14

Nguồn đối chiếu: 502 file Java/96.465 dòng và SQL định nghĩa trong repo.
Mục tiêu là bảo toàn luật game và cho phép thay từng module khi làm game mới.
**Chưa có kết luận parity 100%, build thành công hoặc chạy toàn bộ client game.**
Người dùng tự build, tự quản lý resource và đã chọn sửa client để dùng framing
TCP tiêu chuẩn của ServerEngine.

| Phần | Source C++ hiện có | Chưa hoàn tất |
|---|---|---|
| Content | 52 bảng/9.302 dòng, 29 template/191 cấp, item/map/mob/NPC; hash/reference validation | Chính sách hot reload của host |
| Protocol | Engine xử lý BE32 length; app dùng command byte + raw binary | Client cũ phải sửa; chưa thử client thực |
| Cache | Version, skill, map, item0/1/2/100, sáu animation/paint cache | Download resource và renderer ngoài host |
| Session/auth | Hello, version, matrix, ECC diagnostic, device, login/role/lock/duplicate/cooldown | Guest registration, activation, admin service, multi-layer extensions |
| Character/storage | Name/gender/hair; stats/items/tree/task/currency ban đầu; SQLite uniqueness/CAS | MySQL adapter, migration toàn bộ trạng thái cũ, async persistence |
| Combat | Luật mob/PvP số nguyên theo thứ tự, RNG/crit/miss, protection/cap, mana/stamina, lifesteal, nhiều skill đặc biệt | Full Info/equipment/set/fusion, mọi boss subclass, XP/drop/quest side effects |
| World | Actor/mob ownership, learned skills, commit kết quả, effect timing/expiry, summon lifecycle/follow-up | Full AI/respawn/zone transfer, event scheduler, spatial index |
| Geometry | External mapData/block loader, tile classification, floor/collision helpers, template packets | Full Player.move/waypoint/flight policy trong host |
| Extension | GameModule, LegacyGameplay, IdentityStore, RuleProfile, effects, string IDs/dynamic attributes | Cơ chế mới vẫn cần implementation/validation cụ thể |
| Gameplay integration | LegacyWorld/replay riêng; session host bàn giao qua LegacyGameplay | **Main chưa cài full LegacyGameplay: chưa vào map/đánh quái bằng client** |
| Economy/social | Java/SQL giữ để đối chiếu | Inventory operations, shop/trade/drop, quest/clan/events/bosses/bots chưa port đầy đủ |

`--legacy` thực thi tầng account/content và tạo nhân vật có lưu trữ. Khi chưa có
adapter gameplay, enter trả unsupported rõ ràng, không đặt trạng thái đã vào game
giả. `--replay-legacy` và public API dùng runtime chiến đấu mà không cần asset/client.

Khác biệt có chủ ý: framing mới, bỏ XOR/Base64 ở mạng live, payload validation
chặt hơn, bounded queues, identity/ownership kiểm tra rõ, storage SQLite có
revision. Đây không phải parity wire/schema với Java cũ. `LegacyCodec` chỉ dùng
đối chiếu ngoại tuyến; submodule ServerEngine không bị sửa.

Subsystem còn thiếu cần được nối theo luồng hoàn chỉnh, rồi người dùng build và
đối chiếu kết quả. Resource ngoài repo chỉ cần khi triển khai, không ngăn phát
triển source. Tài liệu hiện hành nằm dưới root `docs/`; tài liệu trong thư mục
Java cũ có thể nhắc tới các dự án khác không có trong checkout.

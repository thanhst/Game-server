#pragma once

#include <cstdint>

namespace game {

struct ObjectId {
    std::uint64_t value = 0;

    explicit operator bool() const noexcept { return value != 0; }

    friend bool operator==(ObjectId left, ObjectId right) noexcept {
        return left.value == right.value;
    }
    friend bool operator!=(ObjectId left, ObjectId right) noexcept {
        return !(left == right);
    }
    friend bool operator<(ObjectId left, ObjectId right) noexcept {
        return left.value < right.value;
    }
};

enum class ObjectKind {
    Player,
    Creature
};

class Object {
public:
    virtual ~Object() noexcept = default;

    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;

    ObjectId id() const noexcept { return id_; }
    ObjectKind kind() const noexcept { return kind_; }

protected:
    Object(ObjectId id, ObjectKind kind);

private:
    ObjectId id_;
    ObjectKind kind_;
};

} // namespace game

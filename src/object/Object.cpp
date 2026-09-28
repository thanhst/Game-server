#include "game/object/Object.h"

#include <stdexcept>

namespace game {

Object::Object(ObjectId id, ObjectKind kind) : id_(id), kind_(kind) {
    if (!id) throw std::invalid_argument("object id must be nonzero");
}

} // namespace game

#pragma once

#include "game/legacy/LegacyContent.h"
#include <memory>

namespace game::legacy {
// Executes a local, data-only scenario through LegacyWorld and returns its trace.
// Throws for malformed input; valid operations report applied/rejected/unsupported.
// No files, network, user-code evaluation, or implicit movement/stat adapters.
nlohmann::json runReplay(std::shared_ptr<const ContentSnapshot> content,
                         const nlohmann::json& scenario);
} // namespace game::legacy

#pragma once
#include "../../IIMA.Core/Persistence/UserData.h"

namespace iima {
// Blocking local IO: invoke on a background thread and await completion before exit.
// Internally serialized. No timers, UI objects, or shared mutable player state.
class UserDataStore final {
public:
    static UserData Load();
    static void Save(UserData const& data);
    // Three-way merge under the same process-wide IO lock. The two documents
    // remain individually atomic, not an all-or-nothing pair. Retrying after a
    // partial write is idempotent for the same base/edited pair.
    // Advance the caller's baseline to its submitted edited snapshot only after
    // success; keeping that baseline avoids treating unseen peer values as edits.
    static UserData SaveMerged(UserData const& base, UserData const& edited);
};
}

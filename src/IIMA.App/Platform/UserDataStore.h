#pragma once
#include "../../IIMA.Core/Persistence/UserData.h"

namespace iima {
// Blocking local IO: invoke on a background thread and await completion before exit.
// Internally serialized. No timers, UI objects, or shared mutable player state.
class UserDataStore final {
public:
    static UserData Load();
    static void Save(UserData const& data);
};
}

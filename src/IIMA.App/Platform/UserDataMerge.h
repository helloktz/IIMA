#pragma once
// Multi-window adaptation of IINA's shared HistoryController and Preference store.
// Copyright (c) 2017 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "../../IIMA.Core/Persistence/UserData.h"

namespace iima {
// Apply only differences authored between base and edited to latest. No IO.
// Conflicting settings changes: the last serialized writer wins for that key.
// History removal targets the observed record, not newer playback of its path.
UserData MergeUserData(UserData const& base, UserData const& edited, UserData latest);
}

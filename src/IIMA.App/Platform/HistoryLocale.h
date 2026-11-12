#pragma once
#include "../../IIMA.Core/History/HistoryBrowser.h"
namespace iima {
// Locale-aware projection hooks; no filesystem access and no time-zone guesses.
HistoryPresentation WindowsHistoryPresentation();
}

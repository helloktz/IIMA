#pragma once
#include "MpvClient.h"
#include "include/mpv/client.h"
namespace iima {
// Copies only the typed fields the app consumes, before the next wait_event.
void CopyCollectionProperty(MpvEvent& event, mpv_node const& value);
}

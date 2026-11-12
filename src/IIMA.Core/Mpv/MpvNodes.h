#pragma once
#include "MpvClient.h"
#include "include/mpv/client.h"
namespace iima {
// Copies only the typed fields the app consumes, before the next wait_event.
// Strict complete transaction input: no partial/truncated subtitle identities.
bool CopyCompleteTrackIdentities(MpvEvent& event, mpv_node const& value);
bool CopySubtitleStateTracks(MpvEvent& event, mpv_node const& value);
void CopyCollectionProperty(MpvEvent& event, mpv_node const& value);
}

#pragma once
#include <windows.h>
namespace iima {
// Explicit user-triggered HKCU registration only. Never writes UserChoice or
// changes an extension's default association; Windows owns that decision.
class FileAssociations final {
public:
    static void Register();
    static void Unregister();
    static void OpenDefaultApps(HWND owner);
};
}

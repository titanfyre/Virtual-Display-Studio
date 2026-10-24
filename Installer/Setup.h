#pragma once
#include <windows.h>
#include <string>
namespace Setup {
struct Result { DWORD error=0;bool reboot=false;std::wstring detail; };
unsigned Installed(DWORD* error=nullptr);
Result Execute(bool install) noexcept;
bool Restart() noexcept;
bool PackageMatches(const std::wstring& path) noexcept;
bool CatalogTrusted(const std::wstring& path) noexcept;
}

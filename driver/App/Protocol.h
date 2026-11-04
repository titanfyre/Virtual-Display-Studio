#pragma once
#include <windows.h>
#include <winioctl.h>
#include <cstdint>
// A single, fixed protocol shared by the application and the private UMDF driver.
namespace DisplayProtocol {
inline constexpr GUID Interface = {0x961270df,0xba5e,0x4597,{0x86,0x19,0x9a,0x3a,0xe8,0x71,0xdc,0x92}};
inline constexpr DWORD Control = CTL_CODE(FILE_DEVICE_UNKNOWN,0x801,METHOD_BUFFERED,FILE_READ_DATA|FILE_WRITE_DATA);
enum class Action : std::uint32_t { Query=0, Connect=1, Disconnect=2 };
struct Request { std::uint32_t size=sizeof(Request), version=1; Action action=Action::Query; std::uint32_t reserved=0; };
struct State { std::uint32_t size=sizeof(State), version=1, ready=0, connected=0, owned=0, width=1920, height=1080, hz=60, adapter_status=0; };
inline bool Valid(const Request& r) noexcept { return r.size==sizeof(Request)&&r.version==1&&r.reserved==0&&static_cast<std::uint32_t>(r.action)<=2; }
inline bool Valid(const State& s) noexcept { return s.size==sizeof(State)&&s.version==1&&s.ready<=1&&s.connected<=1&&s.owned<=1&&s.width==1920&&s.height==1080&&s.hz==60; }
static_assert(sizeof(Request)==16&&sizeof(State)==36,"Stable wire protocol");
}

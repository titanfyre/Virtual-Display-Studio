#pragma once
#include "Protocol.h"
#include <string>
class VirtualDisplay {
    HANDLE handle_=INVALID_HANDLE_VALUE;
public:
    ~VirtualDisplay();
    VirtualDisplay()=default;
    VirtualDisplay(const VirtualDisplay&)=delete;
    VirtualDisplay& operator=(const VirtualDisplay&)=delete;
    DWORD Execute(DisplayProtocol::Action action,DisplayProtocol::State& state) noexcept;
    static std::wstring Error(DWORD error);
};

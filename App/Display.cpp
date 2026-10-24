#include "Display.h"
#include <setupapi.h>
#include <vector>
#include <memory>
VirtualDisplay::~VirtualDisplay(){if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
DWORD VirtualDisplay::Execute(DisplayProtocol::Action action,DisplayProtocol::State& state) noexcept {
    try {
        if(handle_==INVALID_HANDLE_VALUE){
            HDEVINFO devices=SetupDiGetClassDevsW(&DisplayProtocol::Interface,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
            if(devices==INVALID_HANDLE_VALUE)return GetLastError();
            struct Cleanup{HDEVINFO h;~Cleanup(){SetupDiDestroyDeviceInfoList(h);}} cleanup{devices};
            SP_DEVICE_INTERFACE_DATA item{};item.cbSize=sizeof(item);
            if(!SetupDiEnumDeviceInterfaces(devices,nullptr,&DisplayProtocol::Interface,0,&item))return GetLastError()==ERROR_NO_MORE_ITEMS?ERROR_FILE_NOT_FOUND:GetLastError();
            DWORD bytes=0;SetupDiGetDeviceInterfaceDetailW(devices,&item,nullptr,0,&bytes,nullptr);
            if(bytes<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||bytes>65536)return ERROR_INVALID_DATA;
            std::vector<unsigned char> data(bytes);
            auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(data.data());detail->cbSize=sizeof(*detail);
            if(!SetupDiGetDeviceInterfaceDetailW(devices,&item,detail,bytes,nullptr,nullptr))return GetLastError();
            handle_=CreateFileW(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
            if(handle_==INVALID_HANDLE_VALUE)return GetLastError();
        }
        DisplayProtocol::Request input{};input.action=action;
        DisplayProtocol::State result{};
        OVERLAPPED overlap{};overlap.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!overlap.hEvent)return GetLastError();
        struct Event{HANDLE h;~Event(){CloseHandle(h);}} event{overlap.hEvent};
        DWORD bytes=0,error=ERROR_SUCCESS;
        if(!DeviceIoControl(handle_,DisplayProtocol::Control,&input,sizeof(input),&result,sizeof(result),&bytes,&overlap)){
            error=GetLastError();
            if(error==ERROR_IO_PENDING){
                DWORD wait=WaitForSingleObject(overlap.hEvent,5000);
                if(wait!=WAIT_OBJECT_0){
                    CancelIoEx(handle_,&overlap);
                    GetOverlappedResult(handle_,&overlap,&bytes,TRUE);
                    CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;
                    return wait==WAIT_TIMEOUT?ERROR_TIMEOUT:ERROR_GEN_FAILURE;
                }
                error=GetOverlappedResult(handle_,&overlap,&bytes,FALSE)?ERROR_SUCCESS:GetLastError();
            }
        }
        if(error){CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;return error;}
        if(bytes!=sizeof(result)||!DisplayProtocol::Valid(result)){CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;return ERROR_REVISION_MISMATCH;}
        state=result;return ERROR_SUCCESS;
    }catch(...){return ERROR_NOT_ENOUGH_MEMORY;}
}
std::wstring VirtualDisplay::Error(DWORD error){
    if(error==ERROR_FILE_NOT_FOUND)return L"The signed display driver is not installed. See the installation instructions.";
    if(error==ERROR_ACCESS_DENIED)return L"Administrator access is required to control the virtual display.";
    if(error==ERROR_NOT_READY)return L"The display adapter is still starting. Wait briefly and refresh.";
    if(error==ERROR_BUSY)return L"Another application owns the virtual display. Close it first.";
    wchar_t* text=nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,error,0,reinterpret_cast<LPWSTR>(&text),0,nullptr);
    std::wstring message=text?text:L"Windows could not complete the operation.";if(text)LocalFree(text);
    while(!message.empty()&&(message.back()==L'\r'||message.back()==L'\n'))message.pop_back();
    return message+L" (Windows error "+std::to_wstring(error)+L")";
}

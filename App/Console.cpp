#include "Display.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){
    bool connect=argc==2&&std::wstring(argv[1])==L"connect";
    if(argc>2||(argc==2&&!connect&&std::wstring(argv[1])!=L"status")){std::wcerr<<L"Usage: DisplayControl.exe [status|connect]\n";return 2;}
    VirtualDisplay display;DisplayProtocol::State state;
    DWORD error=display.Execute(connect?DisplayProtocol::Action::Connect:DisplayProtocol::Action::Query,state);
    if(error){std::wcerr<<VirtualDisplay::Error(error)<<L'\n';return 1;}
    std::wcout<<L"Adapter: "<<(state.ready?L"ready":L"preparing")<<L". Monitor: "<<(state.connected?L"connected":L"disconnected")<<L". Mode: 1920 x 1080 at 60 Hz.\n";
    if(state.adapter_status)std::wcout<<L"Adapter initialization NTSTATUS: 0x"<<std::hex<<state.adapter_status<<std::dec<<L'\n';
    if(connect){std::wcout<<L"Press Enter to disconnect and close.\n";std::wstring line;std::getline(std::wcin,line);}
    // Closing this handle also disconnects its monitor after process termination.
    return 0;
}

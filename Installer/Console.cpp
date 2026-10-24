#include "Setup.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){
    if(argc!=2||(std::wstring(argv[1])!=L"/install"&&std::wstring(argv[1])!=L"/uninstall")){
        std::wcout<<L"Virtual Display Studio Setup\nUsage: VirtualDisplaySetupConsole.exe /install | /uninstall\nAdministrator rights required. No automatic restart.\n";return 2;
    }
    auto result=Setup::Execute(std::wstring(argv[1])==L"/install");std::wcout<<result.detail<<L'\n';
    if(result.reboot)std::wcout<<L"Restart Windows to complete this operation.\n";
    return result.error?static_cast<int>(result.error):(result.reboot?3010:0);
}

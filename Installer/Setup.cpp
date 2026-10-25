#include "Setup.h"
#include <setupapi.h>
#include <newdev.h>
#include <devguid.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <mscat.h>
#include <sddl.h>
#include <vector>
#include <filesystem>
#include <stdexcept>
#include <set>
namespace fs=std::filesystem;
namespace {
constexpr wchar_t Hardware[]=L"ROOT\\TitanVirtualDisplay";
struct Devices {HDEVINFO h=INVALID_HANDLE_VALUE;~Devices(){if(h!=INVALID_HANDLE_VALUE)SetupDiDestroyDeviceInfoList(h);}};
bool Ours(HDEVINFO h,SP_DEVINFO_DATA& d){
    DWORD type=0,bytes=0;SetupDiGetDeviceRegistryPropertyW(h,&d,SPDRP_HARDWAREID,&type,nullptr,0,&bytes);
    if(type!=REG_MULTI_SZ||bytes<4||bytes>65536)return false;
    std::vector<wchar_t> ids(bytes/2+2,0);
    if(!SetupDiGetDeviceRegistryPropertyW(h,&d,SPDRP_HARDWAREID,&type,reinterpret_cast<BYTE*>(ids.data()),bytes,nullptr))return false;
    for(const wchar_t* p=ids.data();*p;p+=wcslen(p)+1)if(_wcsicmp(p,Hardware)==0)return true;
    return false;
}
std::wstring Message(DWORD e){wchar_t* s=nullptr;FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,e,0,reinterpret_cast<LPWSTR>(&s),0,nullptr);std::wstring r=s?s:L"Operation failed";if(s)LocalFree(s);return r+L" (Windows error "+std::to_wstring(e)+L")";}
void Require(bool ok){if(!ok)throw std::system_error(static_cast<int>(GetLastError()?GetLastError():ERROR_GEN_FAILURE),std::system_category());}
bool MicrosoftCatalog(const fs::path& path){
    WINTRUST_FILE_INFO file{};file.cbStruct=sizeof(file);file.pcwszFilePath=path.c_str();
    WINTRUST_DATA data{};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;LONG status=WinVerifyTrust(nullptr,&action,&data);bool accepted=false;
    if(status==ERROR_SUCCESS){auto provider=WTHelperProvDataFromStateData(data.hWVTStateData);auto signer=provider?WTHelperGetProvSignerFromChain(provider,0,FALSE,0):nullptr;if(signer&&signer->csCertChain){wchar_t name[512]{};CertGetNameStringW(signer->pasCertChain[0].pCert,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,nullptr,name,512);accepted=_wcsicmp(name,L"Microsoft Windows Hardware Compatibility Publisher")==0;}}
    data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);return accepted;
}
bool CatalogMember(const fs::path& directory,const wchar_t* name){
    const auto path=directory/name;const auto catalog=directory/L"TitanVirtualDisplay.cat";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    struct File{HANDLE h;~File(){CloseHandle(h);}} close{file};
    HCATADMIN admin=nullptr;if(!CryptCATAdminAcquireContext2(&admin,nullptr,L"SHA256",nullptr,0))return false;
    struct Admin{HCATADMIN h;~Admin(){CryptCATAdminReleaseContext(h,0);}} release{admin};
    BYTE hash[64]{};DWORD bytes=sizeof(hash);if(!CryptCATAdminCalcHashFromFileHandle2(admin,file,&bytes,hash,0)||bytes>64)return false;
    wchar_t tag[129]{};const wchar_t* hex=L"0123456789ABCDEF";for(DWORD i=0;i<bytes;++i){tag[i*2]=hex[hash[i]>>4];tag[i*2+1]=hex[hash[i]&15];}
    WINTRUST_CATALOG_INFO member{};member.cbStruct=sizeof(member);member.pcwszCatalogFilePath=catalog.c_str();member.pcwszMemberTag=tag;member.pcwszMemberFilePath=path.c_str();member.hMemberFile=file;member.pbCalculatedFileHash=hash;member.cbCalculatedFileHash=bytes;member.hCatAdmin=admin;
    WINTRUST_DATA data{};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.dwUnionChoice=WTD_CHOICE_CATALOG;data.pCatalog=&member;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL|WTD_REVOCATION_CHECK_NONE;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;LONG result=WinVerifyTrust(nullptr,&action,&data);data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);return result==ERROR_SUCCESS;
}
bool OwnPackage(const fs::path& path){
    GUID classId{};wchar_t className[128]{};
    if(!SetupDiGetINFClassW(path.c_str(),&classId,className,128,nullptr)||!IsEqualGUID(classId,GUID_DEVCLASS_DISPLAY))return false;
    HINF inf=SetupOpenInfFileW(path.c_str(),nullptr,INF_STYLE_WIN4,nullptr);if(inf==INVALID_HANDLE_VALUE)return false;
    struct Inf{HINF h;~Inf(){SetupCloseInfFile(h);}} file{inf};INFCONTEXT line{};wchar_t value[512]{};
    if(!SetupFindFirstLineW(inf,L"Version",L"CatalogFile",&line)||!SetupGetStringFieldW(&line,1,value,512,nullptr)||_wcsicmp(value,L"TitanVirtualDisplay.cat"))return false;
    if(!SetupFindFirstLineW(inf,L"Version",L"Provider",&line)||!SetupGetStringFieldW(&line,1,value,512,nullptr)||_wcsicmp(value,L"Titanfyre"))return false;
    if(!SetupFindFirstLineW(inf,L"Models.NTamd64.10.0...19041",nullptr,&line))return false;
    unsigned rows=0;
    do{if(++rows>32)return false;if(SetupGetStringFieldW(&line,2,value,512,nullptr)&&_wcsicmp(value,Hardware)==0)return true;}while(SetupFindNextLine(&line,&line));
    return false;
}
struct Temporary {
    fs::path directory;
    bool owned=false;
    ~Temporary(){if(!owned)return;for(auto name:{L"TitanVirtualDisplay.inf",L"TitanVirtualDisplay.cat",L"TitanVirtualDisplay.dll"})DeleteFileW((directory/name).c_str());RemoveDirectoryW(directory.c_str());}
    void Create(){
        wchar_t windows[MAX_PATH]{};DWORD length=GetWindowsDirectoryW(windows,MAX_PATH);Require(length>0&&length<MAX_PATH);GUID guid{};Require(SUCCEEDED(CoCreateGuid(&guid)));wchar_t id[40]{};StringFromGUID2(guid,id,40);
        // Use a system-owned parent, not the initiating user's writable Temp tree.
        directory=fs::path(windows)/L"Temp"/(std::wstring(L"TitanDisplay-")+id);
        PSECURITY_DESCRIPTOR descriptor=nullptr;Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&descriptor,nullptr)!=FALSE);
        SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};bool made=CreateDirectoryW(directory.c_str(),&security)!=FALSE;DWORD error=GetLastError();LocalFree(descriptor);if(!made){SetLastError(error);Require(false);}
        owned=true;
        const wchar_t* names[]{L"TitanVirtualDisplay.inf",L"TitanVirtualDisplay.cat",L"TitanVirtualDisplay.dll"};
        for(int i=0;i<3;++i){
            HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(101+i),RT_RCDATA);Require(resource!=nullptr);HGLOBAL loaded=LoadResource(nullptr,resource);Require(loaded!=nullptr);DWORD size=SizeofResource(nullptr,resource);void* bytes=LockResource(loaded);Require(bytes&&size&&size<16*1024*1024);
            HANDLE f=CreateFileW((directory/names[i]).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);Require(f!=INVALID_HANDLE_VALUE);DWORD written=0;bool ok=WriteFile(f,bytes,size,&written,nullptr)&&written==size;error=GetLastError();CloseHandle(f);if(!ok){SetLastError(error);Require(false);}
        }
    }
};
Setup::Result Install(){
    Temporary temp;temp.Create();
    if(!OwnPackage(temp.directory/L"TitanVirtualDisplay.inf"))return {ERROR_INVALID_DATA,false,L"Installation refused: the embedded package does not match the exact Titan Virtual Display identity."};
    if(!CatalogMember(temp.directory,L"TitanVirtualDisplay.inf")||!CatalogMember(temp.directory,L"TitanVirtualDisplay.dll"))return {ERROR_INVALID_IMAGE_HASH,false,L"Installation refused: embedded INF or DLL is not a trusted member of its catalog."};
    if(!MicrosoftCatalog(temp.directory/L"TitanVirtualDisplay.cat"))return {ERROR_INVALID_IMAGE_HASH,false,L"Installation refused: the embedded driver catalog must have a trusted Microsoft Windows Hardware Compatibility Publisher signature."};
    Devices devices;devices.h=SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY,nullptr,nullptr,0);Require(devices.h!=INVALID_HANDLE_VALUE);
    SP_DEVINFO_DATA selected{};selected.cbSize=sizeof(selected);bool found=false;
    for(DWORD i=0;;++i){SP_DEVINFO_DATA d{};d.cbSize=sizeof(d);if(!SetupDiEnumDeviceInfo(devices.h,i,&d)){if(GetLastError()!=ERROR_NO_MORE_ITEMS)Require(false);break;}if(Ours(devices.h,d)){selected=d;found=true;break;}}
    bool created=false;
    if(!found){
        Require(SetupDiCreateDeviceInfoW(devices.h,L"Display",&GUID_DEVCLASS_DISPLAY,L"Titan Virtual Display Studio",nullptr,DICD_GENERATE_ID,&selected)!=FALSE);
        constexpr wchar_t ids[]=L"ROOT\\TitanVirtualDisplay\0";
        Require(SetupDiSetDeviceRegistryPropertyW(devices.h,&selected,SPDRP_HARDWAREID,reinterpret_cast<const BYTE*>(ids),sizeof(ids))!=FALSE);
        Require(SetupDiCallClassInstaller(DIF_REGISTERDEVICE,devices.h,&selected)!=FALSE);created=true;
    }
    BOOL reboot=FALSE;
    if(!UpdateDriverForPlugAndPlayDevicesW(nullptr,Hardware,(temp.directory/L"TitanVirtualDisplay.inf").c_str(),INSTALLFLAG_NONINTERACTIVE,&reboot)){
        DWORD error=GetLastError();bool removed=!created||SetupDiCallClassInstaller(DIF_REMOVE,devices.h,&selected)!=FALSE;
        return {error,!removed,L"Driver installation failed. "+std::wstring(created?(removed?L"The new device node was removed. ":L"The new device node could not be removed; restart and run Uninstall. "):L"The existing device was retained. ")+Message(error)};
    }
    return {0,reboot!=FALSE,L"Display driver installed. No monitor was connected. Open Virtual Display Studio and choose Connect."};
}
Setup::Result Uninstall(){
    Devices devices;devices.h=SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY,nullptr,nullptr,0);Require(devices.h!=INVALID_HANDLE_VALUE);
    std::vector<SP_DEVINFO_DATA> targets;std::set<std::wstring> packages;
    for(DWORD i=0;;++i){SP_DEVINFO_DATA d{};d.cbSize=sizeof(d);if(!SetupDiEnumDeviceInfo(devices.h,i,&d)){if(GetLastError()!=ERROR_NO_MORE_ITEMS)Require(false);break;}if(Ours(devices.h,d)){
        targets.push_back(d);HKEY key=SetupDiOpenDevRegKey(devices.h,&d,DICS_FLAG_GLOBAL,0,DIREG_DRV,KEY_QUERY_VALUE);
        if(key!=INVALID_HANDLE_VALUE){wchar_t inf[256]{};DWORD bytes=sizeof(inf),type=0;if(RegQueryValueExW(key,L"InfPath",nullptr,&type,reinterpret_cast<BYTE*>(inf),&bytes)==ERROR_SUCCESS&&type==REG_SZ&&bytes<sizeof(inf)){CharLowerBuffW(inf,static_cast<DWORD>(wcslen(inf)));packages.emplace(inf);}RegCloseKey(key);}
    }}
    // Find only this exact display package, including an orphan left by an earlier
    // package-in-use failure after its device node had already been removed.
    wchar_t windows[MAX_PATH]{};DWORD length=GetWindowsDirectoryW(windows,MAX_PATH);Require(length>0&&length<MAX_PATH);
    fs::path infDirectory=fs::path(windows)/L"INF";
    for(auto it=packages.begin();it!=packages.end();){if(!OwnPackage(infDirectory/(*it)))it=packages.erase(it);else ++it;}
    WIN32_FIND_DATAW entry{};HANDLE search=FindFirstFileW((infDirectory/L"oem*.inf").c_str(),&entry);
    if(search==INVALID_HANDLE_VALUE){if(GetLastError()!=ERROR_FILE_NOT_FOUND)Require(false);}
    else {
        struct Search{HANDLE h;~Search(){FindClose(h);}} cleanup{search};unsigned scanned=0;
        do {
            if(++scanned>16384){SetLastError(ERROR_BUFFER_OVERFLOW);Require(false);}
            auto path=infDirectory/entry.cFileName;
            if(OwnPackage(path)){std::wstring name=entry.cFileName;CharLowerBuffW(name.data(),static_cast<DWORD>(name.size()));packages.insert(name);}
        }while(FindNextFileW(search,&entry));
        if(GetLastError()!=ERROR_NO_MORE_FILES)Require(false);
    }
    bool restart=false;
    for(auto& d:targets){BOOL reboot=FALSE;if(!DiUninstallDevice(nullptr,devices.h,&d,0,&reboot)){auto error=GetLastError();return {error,restart,L"Could not remove the virtual display device. "+Message(error)};}restart=restart||reboot!=FALSE;}
    for(auto& inf:packages){
        // Published names only; never remove an arbitrary path or force removal of an in-use package.
        bool valid=inf.size()>7&&_wcsnicmp(inf.c_str(),L"oem",3)==0&&_wcsicmp(inf.c_str()+inf.size()-4,L".inf")==0;
        for(size_t i=3;valid&&i<inf.size()-4;++i)valid=inf[i]>=L'0'&&inf[i]<=L'9';
        if(valid&&!SetupUninstallOEMInfW(inf.c_str(),0,nullptr)){DWORD error=GetLastError();return {error,restart,L"The device was removed, but Windows retained the driver package. Restart if requested and run Uninstall again. "+Message(error)};}
    }
    return {0,restart,targets.empty()&&packages.empty()?L"The Titan virtual display device and package are not installed.":L"Titan virtual display devices and unused driver packages removed."};
}
}
unsigned Setup::Installed(DWORD* error){if(error)*error=0;try{Devices devices;devices.h=SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY,nullptr,nullptr,DIGCF_PRESENT);if(devices.h==INVALID_HANDLE_VALUE){if(error)*error=GetLastError();return 0;}unsigned n=0;for(DWORD i=0;;++i){SP_DEVINFO_DATA d{};d.cbSize=sizeof(d);if(!SetupDiEnumDeviceInfo(devices.h,i,&d)){DWORD e=GetLastError();if(e!=ERROR_NO_MORE_ITEMS&&error)*error=e;break;}if(Ours(devices.h,d))++n;}return n;}catch(...){if(error)*error=ERROR_NOT_ENOUGH_MEMORY;return 0;}}
bool Setup::PackageMatches(const std::wstring& path) noexcept{try{return OwnPackage(fs::path(path));}catch(...){return false;}}
bool Setup::CatalogTrusted(const std::wstring& path) noexcept{try{return MicrosoftCatalog(fs::path(path));}catch(...){return false;}}
bool Setup::SignedPackageTrusted(const std::wstring& directory) noexcept{try{fs::path p=directory;return OwnPackage(p/L"TitanVirtualDisplay.inf")&&MicrosoftCatalog(p/L"TitanVirtualDisplay.cat")&&CatalogMember(p,L"TitanVirtualDisplay.inf")&&CatalogMember(p,L"TitanVirtualDisplay.dll");}catch(...){return false;}}
Setup::Result Setup::Execute(bool install) noexcept {
    HANDLE mutex=nullptr;
    try {
        mutex=CreateMutexW(nullptr,FALSE,L"Global\\TitanVirtualDisplaySetup");if(!mutex)return {GetLastError(),false,L"Could not open the installer lock."};
        DWORD wait=WaitForSingleObject(mutex,0);if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){CloseHandle(mutex);return {ERROR_BUSY,false,L"Another virtual display installation is already running."};}
        Result result=install?Install():Uninstall();ReleaseMutex(mutex);CloseHandle(mutex);return result;
    }catch(const std::system_error& e){if(mutex){ReleaseMutex(mutex);CloseHandle(mutex);}return {static_cast<DWORD>(e.code().value()),false,Message(static_cast<DWORD>(e.code().value()))};}
    catch(...){if(mutex){ReleaseMutex(mutex);CloseHandle(mutex);}return {ERROR_GEN_FAILURE,false,L"The operation failed before completion. No system restart was requested."};}
}
bool Setup::Restart() noexcept {
    HANDLE token{};if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token))return false;TOKEN_PRIVILEGES p{};p.PrivilegeCount=1;bool ok=LookupPrivilegeValueW(nullptr,SE_SHUTDOWN_NAME,&p.Privileges[0].Luid)!=FALSE;p.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
    if(ok){SetLastError(0);ok=AdjustTokenPrivileges(token,FALSE,&p,0,nullptr,nullptr)&&GetLastError()==ERROR_SUCCESS;}CloseHandle(token);
    return ok&&ExitWindowsEx(EWX_REBOOT,SHTDN_REASON_MAJOR_APPLICATION|SHTDN_REASON_MINOR_INSTALLATION|SHTDN_REASON_FLAG_PLANNED);
}

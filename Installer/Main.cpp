#include "Setup.h"
#include <thread>
#include <atomic>
#include <memory>
namespace {HWND window{},status{},logbox{},install{},uninstall{},refresh{},restart{};HFONT font{};std::thread worker;std::atomic_bool busy=false;
void Refresh(){DWORD error=0;unsigned count=Setup::Installed(&error);SetWindowTextW(status,error?L"Windows could not read the display device inventory.":(count?L"Titan Virtual Display Studio is installed.":L"Titan Virtual Display Studio is not installed."));}
void Run(bool adding){if(busy.exchange(true))return;if(worker.joinable())worker.join();EnableWindow(install,FALSE);EnableWindow(uninstall,FALSE);EnableWindow(refresh,FALSE);SetWindowTextW(logbox,L"Working... No automatic restart will be performed.");try{worker=std::thread([adding]{auto r=new(std::nothrow)Setup::Result(Setup::Execute(adding));if(!r){PostMessageW(window,WM_APP+2,0,0);return;}if(!PostMessageW(window,WM_APP+1,0,reinterpret_cast<LPARAM>(r)))delete r;});}catch(...){PostMessageW(window,WM_APP+2,0,0);}}
LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){switch(m){case WM_CREATE:{
font=CreateFontW(18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
status=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,24,24,600,30,h,nullptr,nullptr,nullptr);
install=CreateWindowW(L"BUTTON",L"Install / reinstall",WS_CHILD|WS_VISIBLE|WS_TABSTOP,24,74,180,42,h,reinterpret_cast<HMENU>(1),nullptr,nullptr);
uninstall=CreateWindowW(L"BUTTON",L"Uninstall",WS_CHILD|WS_VISIBLE|WS_TABSTOP,216,74,130,42,h,reinterpret_cast<HMENU>(2),nullptr,nullptr);
refresh=CreateWindowW(L"BUTTON",L"Refresh",WS_CHILD|WS_VISIBLE|WS_TABSTOP,358,74,110,42,h,reinterpret_cast<HMENU>(3),nullptr,nullptr);
restart=CreateWindowW(L"BUTTON",L"Restart Windows",WS_CHILD|WS_TABSTOP,480,74,160,42,h,reinterpret_cast<HMENU>(4),nullptr,nullptr);
logbox=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"Microsoft signing is required before installation. The installer never enables test signing or connects a monitor automatically.",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,24,140,616,190,h,nullptr,nullptr,nullptr);
for(auto child:{status,install,uninstall,refresh,restart,logbox})SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);Refresh();return 0;}
case WM_COMMAND:if(HIWORD(w)==BN_CLICKED){switch(LOWORD(w)){case 1:Run(true);break;case 2:Run(false);break;case 3:Refresh();break;case 4:if(MessageBoxW(h,L"Restart Windows now? Save your work first.",L"Restart Windows",MB_YESNO|MB_ICONQUESTION)==IDYES&&!Setup::Restart())MessageBoxW(h,L"Windows did not accept the restart request. Restart manually.",L"Restart",MB_OK|MB_ICONERROR);break;}}return 0;
case WM_APP+1:{std::unique_ptr<Setup::Result> r(reinterpret_cast<Setup::Result*>(l));if(worker.joinable())worker.join();busy=false;SetWindowTextW(logbox,r->detail.c_str());EnableWindow(install,TRUE);EnableWindow(uninstall,TRUE);EnableWindow(refresh,TRUE);ShowWindow(restart,r->reboot?SW_SHOW:SW_HIDE);Refresh();return 0;}
case WM_APP+2:if(worker.joinable())worker.join();busy=false;SetWindowTextW(logbox,L"The operation could not be started because there are not enough resources.");EnableWindow(install,TRUE);EnableWindow(uninstall,TRUE);EnableWindow(refresh,TRUE);return 0;
case WM_SETCURSOR:if(reinterpret_cast<HWND>(w)!=h&&reinterpret_cast<HWND>(w)!=logbox){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}break;
case WM_CLOSE:if(busy){MessageBoxW(h,L"Wait for the installation operation to finish.",L"Working",MB_OK);return 0;}DestroyWindow(h);return 0;
case WM_DESTROY:if(worker.joinable())worker.join();DeleteObject(font);PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){SetProcessDPIAware();WNDCLASSW c{};c.lpfnWndProc=Proc;c.hInstance=instance;c.lpszClassName=L"TitanVirtualDisplaySetup";c.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);c.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&c);window=CreateWindowW(c.lpszClassName,L"Virtual Display Studio Setup",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,684,400,nullptr,nullptr,instance,nullptr);if(!window)return 1;ShowWindow(window,show);MSG m;while(GetMessageW(&m,nullptr,0,0)>0){if(!IsDialogMessageW(window,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}return 0;}

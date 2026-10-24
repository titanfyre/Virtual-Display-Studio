#include "Display.h"
#include <shellapi.h>
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <gdiplus.h>
namespace {
constexpr COLORREF Background=RGB(13,19,32),Panel=RGB(23,33,51),Ink=RGB(239,244,255),Muted=RGB(163,179,204),Blue=RGB(39,104,241);
HWND window{},connectButton{},disconnectButton{},refreshButton{};HFONT titleFont{},bodyFont{},smallFont{};
VirtualDisplay display;DisplayProtocol::State state{};
std::wstring status=L"Ready to connect",detail=L"One virtual monitor. No display is created until you choose Connect.";
std::thread worker;std::atomic_bool busy=false;
struct Result{DWORD error;DisplayProtocol::State state;};
void Text(HDC dc,int x,int y,int right,int bottom,const std::wstring& text,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_WORDBREAK){
    auto old=SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);RECT r{x,y,right,bottom};DrawTextW(dc,text.c_str(),-1,&r,flags);SelectObject(dc,old);
}
void DrawScene(HDC dc,const RECT& r){
    HBRUSH b=CreateSolidBrush(Background);FillRect(dc,&r,b);DeleteObject(b);
    Text(dc,32,24,r.right-24,70,L"Virtual Display Studio",titleFont,Ink);
    Text(dc,32,78,r.right-28,106,L"Explicit control for headless, OEM and display test workflows",smallFont,Muted);
    RECT card{32,126,r.right-32,284};b=CreateSolidBrush(Panel);FillRect(dc,&card,b);DeleteObject(b);
    Text(dc,52,142,r.right-48,172,status,bodyFont,Ink);
    Text(dc,52,185,r.right-52,236,detail,smallFont,Muted);
    Text(dc,52,246,r.right-48,274,L"1920 x 1080     /     60 Hz     /     One monitor",smallFont,Ink);
    Text(dc,32,391,r.right-32,448,L"Signing pending: install only the Microsoft-signed package after it is published.\nNo desktop images are recorded or transmitted.",smallFont,Muted);
}
bool RenderPreview(){
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token{};
    if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return false;
    bool saved=false;
    {RECT r;GetClientRect(window,&r);Gdiplus::Bitmap bitmap(r.right,r.bottom,PixelFormat32bppARGB);Gdiplus::Graphics graphics(&bitmap);HDC dc=graphics.GetHDC();
    DrawScene(dc,r);
    // Render the existing button controls in this process without showing a window.
    for(auto child:{connectButton,disconnectButton,refreshButton}){RECT b;GetWindowRect(child,&b);POINT point{b.left,b.top};ScreenToClient(window,&point);int old=SaveDC(dc);SetViewportOrgEx(dc,point.x,point.y,nullptr);SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);RestoreDC(dc,old);}
    graphics.ReleaseHDC(dc);CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};saved=bitmap.Save(L"preview.png",&png,nullptr)==Gdiplus::Ok;}
    Gdiplus::GdiplusShutdown(token);return saved;
}
bool Elevated(){HANDLE token{};if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;TOKEN_ELEVATION e{};DWORD n=0;bool ok=GetTokenInformation(token,TokenElevation,&e,sizeof(e),&n)&&e.TokenIsElevated;CloseHandle(token);return ok;}
void Operation(DisplayProtocol::Action action){
    if(busy.exchange(true))return;
    if(action==DisplayProtocol::Action::Connect&&!Elevated()){
        wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
        SHELLEXECUTEINFOW e{sizeof(e)};e.fMask=SEE_MASK_NOCLOSEPROCESS;e.hwnd=window;e.lpVerb=L"runas";e.lpFile=path;e.lpParameters=L"--connect";e.nShow=SW_SHOWNORMAL;
        bool launched=ShellExecuteExW(&e)!=FALSE;if(e.hProcess)CloseHandle(e.hProcess);busy=false;
        if(launched)PostMessageW(window,WM_CLOSE,0,0);
        else {detail=L"Administrator approval was cancelled. No monitor was created.";InvalidateRect(window,nullptr,FALSE);}return;
    }
    if(worker.joinable())worker.join();status=L"Working...";detail=L"Waiting for Windows to complete the display operation.";
    EnableWindow(connectButton,FALSE);EnableWindow(disconnectButton,FALSE);EnableWindow(refreshButton,FALSE);InvalidateRect(window,nullptr,FALSE);
    try {worker=std::thread([action]{auto r=new(std::nothrow)Result{};if(!r){PostMessageW(window,WM_APP+2,0,0);return;}r->error=display.Execute(action,r->state);if(!PostMessageW(window,WM_APP+1,0,reinterpret_cast<LPARAM>(r)))delete r;});}
    catch(...){PostMessageW(window,WM_APP+2,0,0);}
}
LRESULT CALLBACK Proc(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_CREATE:{
        titleFont=CreateFontW(31,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        bodyFont=CreateFontW(20,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        smallFont=CreateFontW(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        connectButton=CreateWindowW(L"BUTTON",L"Connect display",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,32,310,205,48,hwnd,reinterpret_cast<HMENU>(1),nullptr,nullptr);
        disconnectButton=CreateWindowW(L"BUTTON",L"Disconnect",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,250,310,160,48,hwnd,reinterpret_cast<HMENU>(2),nullptr,nullptr);
        refreshButton=CreateWindowW(L"BUTTON",L"Refresh status",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,423,310,170,48,hwnd,reinterpret_cast<HMENU>(3),nullptr,nullptr);
        EnableWindow(disconnectButton,FALSE);return 0;}
    case WM_COMMAND:if(HIWORD(w)==BN_CLICKED){switch(LOWORD(w)){case 1:Operation(DisplayProtocol::Action::Connect);break;case 2:Operation(DisplayProtocol::Action::Disconnect);break;case 3:Operation(DisplayProtocol::Action::Query);break;case 4:ShellExecuteW(hwnd,L"open",L"ms-settings:display",nullptr,nullptr,SW_SHOWNORMAL);break;}}return 0;
    case WM_APP+1:{std::unique_ptr<Result> r(reinterpret_cast<Result*>(l));if(worker.joinable())worker.join();busy=false;
        if(r->error){status=L"Operation not completed";detail=VirtualDisplay::Error(r->error);}
        else {state=r->state;status=state.connected?L"Virtual monitor connected":L"Virtual monitor disconnected";detail=state.connected?(state.owned?L"This window owns the display. Closing the application disconnects it.":L"A different application owns this display."):(state.ready?L"Adapter ready. Choose Connect to add a 1920 x 1080 display.":L"The adapter is still preparing. Refresh to check its status.");}
        if(!r->error&&!state.ready&&state.adapter_status){wchar_t value[64]{};swprintf_s(value,L"0x%08X",state.adapter_status);detail=L"Windows could not initialize the display adapter. NTSTATUS "+std::wstring(value)+L". See Device Manager for the device status.";}
        EnableWindow(connectButton,!state.connected);EnableWindow(disconnectButton,state.owned!=0);EnableWindow(refreshButton,TRUE);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
    case WM_APP+2:if(worker.joinable())worker.join();busy=false;status=L"Not enough memory";detail=L"The operation was not started.";EnableWindow(connectButton,TRUE);EnableWindow(refreshButton,TRUE);InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_DRAWITEM:{auto d=reinterpret_cast<DRAWITEMSTRUCT*>(l);bool enabled=(d->itemState&ODS_DISABLED)==0;COLORREF color=enabled&&d->CtlID==1?Blue:Panel;HBRUSH b=CreateSolidBrush(color);FillRect(d->hDC,&d->rcItem,b);DeleteObject(b);wchar_t label[64]{};GetWindowTextW(d->hwndItem,label,64);auto f=SelectObject(d->hDC,smallFont);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,enabled?Ink:Muted);DrawTextW(d->hDC,label,-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(d->itemState&ODS_FOCUS)DrawFocusRect(d->hDC,&d->rcItem);SelectObject(d->hDC,f);return TRUE;}
    case WM_SETCURSOR:if(reinterpret_cast<HWND>(w)!=hwnd&&IsWindowEnabled(reinterpret_cast<HWND>(w))){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}break;
    case WM_ERASEBKGND:return 1;
    case WM_PRINTCLIENT:{RECT r;GetClientRect(hwnd,&r);DrawScene(reinterpret_cast<HDC>(w),r);return 0;}
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(hwnd,&p);RECT r;GetClientRect(hwnd,&r);HDC buffer=CreateCompatibleDC(dc);auto bmp=CreateCompatibleBitmap(dc,r.right,r.bottom);auto previous=SelectObject(buffer,bmp);DrawScene(buffer,r);
        BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,previous);DeleteObject(bmp);DeleteDC(buffer);EndPaint(hwnd,&p);return 0;}
    case WM_CLOSE:if(busy){status=L"Finishing the current operation";detail=L"Wait for Windows to complete the request, then close this window.";InvalidateRect(hwnd,nullptr,FALSE);return 0;}DestroyWindow(hwnd);return 0;
    case WM_DESTROY:if(worker.joinable())worker.join();DeleteObject(titleFont);DeleteObject(bodyFont);DeleteObject(smallFont);PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return 1;
    bool render=argc==2&&std::wstring(argv[1])==L"--render-preview";
    bool connect=argc==2&&std::wstring(argv[1])==L"--connect";LocalFree(argv);
    SetProcessDPIAware();WNDCLASSW c{};c.lpfnWndProc=Proc;c.hInstance=instance;c.lpszClassName=L"TitanVirtualDisplayStudio";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&c);
    window=CreateWindowExW(0,c.lpszClassName,L"Virtual Display Studio | Titanfyre",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,672,510,nullptr,nullptr,instance,nullptr);
    if(!window)return 1;
    if(render){bool saved=RenderPreview();DestroyWindow(window);return saved?0:1;}
    ShowWindow(window,show);UpdateWindow(window);
    if(connect)Operation(DisplayProtocol::Action::Connect);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}return 0;
}

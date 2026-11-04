/* Derived from Microsoft's MS-PL-licensed Windows-driver-samples/IndirectDisplay.
   Copyright (c) Microsoft Corporation. Copyright (c) 2026 Titanfyre.
   See ../Reference/LICENSE. Private driver implementation; no desktop capture. */
#include <windows.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <iddcx.h>
#include <dxgi1_5.h>
#include <d3d11.h>
#include <avrt.h>
#include <wrl.h>
#include <memory>
#include <new>
#include <evntprov.h>
#include "Protocol.h"
#include "Recovery.h"
using Microsoft::WRL::ComPtr;
using namespace DisplayProtocol;
// No content or frame events. Initialization failures are emitted only to an
// explicitly enabled support ETW session; ordinary rendering does not log.
static REGHANDLE DiagnosticProvider=0;
static const GUID DiagnosticGuid={0x970da3d4,0x5f49,0x4f87,{0x99,0xd6,0x77,0xe8,0x7b,0x43,0xa9,0x64}};
static NTSTATUS Report(const wchar_t* stage,NTSTATUS status) noexcept {
    if(!NT_SUCCESS(status)&&DiagnosticProvider){wchar_t message[192]{};swprintf_s(message,L"%ls: NTSTATUS 0x%08X",stage,static_cast<unsigned>(status));EventWriteString(DiagnosticProvider,2,0,message);}
    return status;
}
EVT_WDF_DRIVER_UNLOAD DriverUnload;
_Use_decl_annotations_ void DriverUnload(WDFDRIVER driver){UNREFERENCED_PARAMETER(driver);if(DiagnosticProvider){EventUnregister(DiagnosticProvider);DiagnosticProvider=0;}}
// Adapter-init completion can run on another thread before its caller resumes.
// Gate its first context read until the returned object has been initialized.
static SRWLOCK AdapterInitializationGate=SRWLOCK_INIT;
struct FaultSink {WDFWORKITEM work;volatile LONG64* pending;std::uint64_t generation;};
static void FrameFailure(DisplayFailureBudget& budget,const FaultSink& sink) noexcept {
    if(budget.Fail(GetTickCount64())){InterlockedExchange64(sink.pending,static_cast<LONG64>(sink.generation));WdfWorkItemEnqueue(sink.work);}
}

class FrameProcessor {
    IDDCX_SWAPCHAIN chain_{};
    HANDLE available_{}, stop_{}, thread_{};
    ComPtr<ID3D11Device> device_;
    DisplayFailureBudget& budget_;FaultSink fault_;
    static DWORD WINAPI Entry(void* p) noexcept { static_cast<FrameProcessor*>(p)->Run(); return 0; }
    void Run() noexcept {
        DWORD task=0; HANDLE av=AvSetMmThreadCharacteristicsW(L"Distribution",&task);
        ComPtr<IDXGIDevice> dxgi;
        HRESULT hr=device_.As(&dxgi);
        IDARG_IN_SWAPCHAINSETDEVICE set{}; set.pDevice=dxgi.Get();
        if(SUCCEEDED(hr)) hr=IddCxSwapChainSetDevice(chain_,&set);
        while(SUCCEEDED(hr)&&WaitForSingleObject(stop_,0)!=WAIT_OBJECT_0) {
            IDARG_OUT_RELEASEANDACQUIREBUFFER buffer{};
            hr=IddCxSwapChainReleaseAndAcquireBuffer(chain_,&buffer);
            if(hr==E_PENDING) {
                HANDLE waits[]{stop_,available_};
                DWORD result=WaitForMultipleObjects(2,waits,FALSE,INFINITE);
                if(result!=WAIT_OBJECT_0+1){if(result!=WAIT_OBJECT_0)hr=E_FAIL;break;}
                hr=S_OK; continue;
            }
            if(FAILED(hr)) break;
            // The desktop stays on the GPU. Nothing is mapped, copied, saved or sent.
            ComPtr<IDXGIResource> surface; surface.Attach(buffer.MetaData.pSurface);
            surface.Reset();
            hr=IddCxSwapChainFinishedProcessingFrame(chain_);
        }
        WdfObjectDelete(chain_);
        if(av) AvRevertMmThreadCharacteristics(av);
        if(FAILED(hr))FrameFailure(budget_,fault_);
    }
public:
    FrameProcessor(IDDCX_SWAPCHAIN chain,HANDLE available,DisplayFailureBudget& budget,FaultSink fault):chain_(chain),available_(available),budget_(budget),fault_(fault){}
    NTSTATUS Start(LUID luid) noexcept {
        ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter> adapter;
        HRESULT hr=CreateDXGIFactory2(0,IID_PPV_ARGS(&factory));
        if(SUCCEEDED(hr)) hr=factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter));
        if(SUCCEEDED(hr)) hr=D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device_,nullptr,nullptr);
        if(FAILED(hr)) return STATUS_UNSUCCESSFUL;
        stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!stop_) return STATUS_INSUFFICIENT_RESOURCES;
        thread_=CreateThread(nullptr,0,Entry,this,0,nullptr);
        return thread_?STATUS_SUCCESS:STATUS_INSUFFICIENT_RESOURCES;
    }
    ~FrameProcessor() {
        if(stop_) SetEvent(stop_);
        if(thread_){ WaitForSingleObject(thread_,INFINITE); CloseHandle(thread_); }
        if(stop_) CloseHandle(stop_);
    }
};
struct MonitorContext { std::unique_ptr<FrameProcessor> processor;DisplayFailureBudget budget;FaultSink fault{}; };
struct MonitorWrapper { MonitorContext* value; };
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(MonitorWrapper,MonitorFrom);
struct DeviceContext {
    WDFDEVICE device{}; WDFWAITLOCK lock{}; IDDCX_ADAPTER adapter{};
    IDDCX_MONITOR monitor{}; WDFFILEOBJECT owner{};
    bool powered=false,ready=false,initializing=false,adapterReady=false;
    NTSTATUS adapterStatus=STATUS_SUCCESS;
    WDFWORKITEM faultWork{};volatile LONG64 pendingFault=0;std::uint64_t monitorGeneration=0;
    NTSTATUS Connect(WDFFILEOBJECT file) noexcept;
    NTSTATUS Disconnect(WDFFILEOBJECT file,bool force=false) noexcept {
        if(monitor&&file!=owner&&!force) return STATUS_DEVICE_BUSY;
        if(monitor) {
            auto old=monitor;
            NTSTATUS s=IddCxMonitorDeparture(old);
            if(!NT_SUCCESS(s)) return s;
            auto previousOwner=owner;monitor=nullptr; owner=nullptr;
            if(previousOwner)WdfObjectDereference(previousOwner);
        }
        return STATUS_SUCCESS;
    }
};
struct DeviceWrapper { DeviceContext* value; };
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DeviceWrapper,DeviceFrom);
struct Locked { WDFWAITLOCK lock; explicit Locked(WDFWAITLOCK p):lock(p){ WdfWaitLockAcquire(lock,nullptr); } ~Locked(){ WdfWaitLockRelease(lock); } };

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD DeviceAdd;
EVT_WDF_DEVICE_D0_ENTRY PowerOn;
EVT_WDF_DEVICE_D0_EXIT PowerOff;
EVT_WDF_DEVICE_RELEASE_HARDWARE ReleaseHardware;
EVT_WDF_FILE_CLEANUP FileCleanup;
EVT_WDF_WORKITEM StopFailedMonitor;
EVT_IDD_CX_DEVICE_IO_CONTROL ControlRequest;
EVT_IDD_CX_ADAPTER_INIT_FINISHED AdapterReady;
EVT_IDD_CX_PARSE_MONITOR_DESCRIPTION ParseDescription;
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES DefaultModes;
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES TargetModes;
EVT_IDD_CX_ADAPTER_COMMIT_MODES CommitModes;
EVT_IDD_CX_MONITOR_ASSIGN_SWAPCHAIN Assign;
EVT_IDD_CX_MONITOR_UNASSIGN_SWAPCHAIN Unassign;

extern "C" BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){return TRUE;}
_Use_decl_annotations_ extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT d,PUNICODE_STRING p){
    WDF_DRIVER_CONFIG c; WDF_DRIVER_CONFIG_INIT(&c,DeviceAdd);
    EventRegister(&DiagnosticGuid,nullptr,nullptr,&DiagnosticProvider);c.EvtDriverUnload=DriverUnload;
    if(DiagnosticProvider)EventWriteString(DiagnosticProvider,4,0,L"DriverEntry started");
    auto status=Report(L"WdfDriverCreate",WdfDriverCreate(d,p,WDF_NO_OBJECT_ATTRIBUTES,&c,WDF_NO_HANDLE));
    if(!NT_SUCCESS(status)&&DiagnosticProvider){EventUnregister(DiagnosticProvider);DiagnosticProvider=0;}return status;
}
_Use_decl_annotations_ NTSTATUS DeviceAdd(WDFDRIVER driver,PWDFDEVICE_INIT init){
    UNREFERENCED_PARAMETER(driver);
    WDF_PNPPOWER_EVENT_CALLBACKS power; WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&power);
    power.EvtDeviceD0Entry=PowerOn; power.EvtDeviceD0Exit=PowerOff;power.EvtDeviceReleaseHardware=ReleaseHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(init,&power);
    WDF_FILEOBJECT_CONFIG files; WDF_FILEOBJECT_CONFIG_INIT(&files,WDF_NO_EVENT_CALLBACK,WDF_NO_EVENT_CALLBACK,FileCleanup);
    files.FileObjectClass=WdfFileObjectWdfCanUseFsContext;
    WdfDeviceInitSetFileObjectConfig(init,&files,WDF_NO_OBJECT_ATTRIBUTES);
    IDD_CX_CLIENT_CONFIG idd; IDD_CX_CLIENT_CONFIG_INIT(&idd);
    idd.EvtIddCxDeviceIoControl=ControlRequest;
    idd.EvtIddCxAdapterInitFinished=AdapterReady;
    idd.EvtIddCxParseMonitorDescription=ParseDescription;
    idd.EvtIddCxMonitorGetDefaultDescriptionModes=DefaultModes;
    idd.EvtIddCxMonitorQueryTargetModes=TargetModes;
    idd.EvtIddCxAdapterCommitModes=CommitModes;
    idd.EvtIddCxMonitorAssignSwapChain=Assign;
    idd.EvtIddCxMonitorUnassignSwapChain=Unassign;
    NTSTATUS s=Report(L"IddCxDeviceInitConfig",IddCxDeviceInitConfig(init,&idd)); if(!NT_SUCCESS(s)) return s;
    WDF_OBJECT_ATTRIBUTES attr; WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr,DeviceWrapper);
    attr.ExecutionLevel=WdfExecutionLevelPassive;
    attr.EvtDestroyCallback=[](WDFOBJECT o){ auto w=DeviceFrom(o); delete w->value; w->value=nullptr; };
    WDFDEVICE device{}; s=Report(L"WdfDeviceCreate",WdfDeviceCreate(&init,&attr,&device)); if(!NT_SUCCESS(s)) return s;
    auto c=new(std::nothrow)DeviceContext; if(!c)return Report(L"AllocateDeviceContext",STATUS_INSUFFICIENT_RESOURCES);
    DeviceFrom(device)->value=c; c->device=device;
    WDF_OBJECT_ATTRIBUTES_INIT(&attr); attr.ParentObject=device;
    s=Report(L"WdfWaitLockCreate",WdfWaitLockCreate(&attr,&c->lock)); if(!NT_SUCCESS(s))return s;
    WDF_WORKITEM_CONFIG work;WDF_WORKITEM_CONFIG_INIT(&work,StopFailedMonitor);
    work.AutomaticSerialization=FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr,DeviceWrapper);attr.ParentObject=device;
    s=Report(L"WdfWorkItemCreate",WdfWorkItemCreate(&work,&attr,&c->faultWork));if(!NT_SUCCESS(s))return s;
    DeviceFrom(c->faultWork)->value=c;
    s=Report(L"IddCxDeviceInitialize",IddCxDeviceInitialize(device)); if(!NT_SUCCESS(s))return s;
    return Report(L"WdfDeviceCreateDeviceInterface",WdfDeviceCreateDeviceInterface(device,&Interface,nullptr));
}
_Use_decl_annotations_ NTSTATUS PowerOn(WDFDEVICE device,WDF_POWER_DEVICE_STATE previous){
    UNREFERENCED_PARAMETER(previous); auto c=DeviceFrom(device)->value; Locked guard(c->lock);
    c->powered=true;
    if(c->adapter){c->ready=c->adapterReady;return STATUS_SUCCESS;}
    if(c->initializing)return STATUS_SUCCESS;
    IDDCX_ADAPTER_CAPS caps{}; caps.Size=sizeof(caps); caps.MaxMonitorsSupported=1;
    caps.EndPointDiagnostics.Size=sizeof(caps.EndPointDiagnostics);
    caps.EndPointDiagnostics.GammaSupport=IDDCX_FEATURE_IMPLEMENTATION_NONE;
    caps.EndPointDiagnostics.TransmissionType=IDDCX_TRANSMISSION_TYPE_WIRED_OTHER;
    caps.EndPointDiagnostics.pEndPointFriendlyName=L"Titan Virtual Display Studio";
    caps.EndPointDiagnostics.pEndPointManufacturerName=L"Titanfyre";
    caps.EndPointDiagnostics.pEndPointModelName=L"Virtual Display 1080p";
    IDDCX_ENDPOINT_VERSION version{}; version.Size=sizeof(version);version.MajorVer=1;
    caps.EndPointDiagnostics.pFirmwareVersion=&version;caps.EndPointDiagnostics.pHardwareVersion=&version;
    WDF_OBJECT_ATTRIBUTES attr; WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr,DeviceWrapper);
    IDARG_IN_ADAPTER_INIT in{};in.WdfDevice=device;in.pCaps=&caps;in.ObjectAttributes=&attr;
    IDARG_OUT_ADAPTER_INIT out{};
    c->initializing=true;
    AcquireSRWLockExclusive(&AdapterInitializationGate);
    NTSTATUS s=Report(L"IddCxAdapterInitAsync",IddCxAdapterInitAsync(&in,&out));
    if(NT_SUCCESS(s)){c->adapter=out.AdapterObject;DeviceFrom(c->adapter)->value=c;}
    else {c->initializing=false;c->adapterStatus=s;}
    ReleaseSRWLockExclusive(&AdapterInitializationGate);
    return s;
}
_Use_decl_annotations_ NTSTATUS PowerOff(WDFDEVICE device,WDF_POWER_DEVICE_STATE target){
    UNREFERENCED_PARAMETER(target);auto c=DeviceFrom(device)->value;Locked guard(c->lock);
    c->powered=false;c->ready=false;return c->Disconnect(nullptr,true);
}
_Use_decl_annotations_ NTSTATUS AdapterReady(IDDCX_ADAPTER adapter,const IDARG_IN_ADAPTER_INIT_FINISHED* in){
    AcquireSRWLockShared(&AdapterInitializationGate);
    auto c=DeviceFrom(adapter)->value;
    if(c)WdfObjectReference(c->device);
    ReleaseSRWLockShared(&AdapterInitializationGate);
    if(!c)return STATUS_UNSUCCESSFUL;
    struct DeviceReference{WDFDEVICE device;~DeviceReference(){WdfObjectDereference(device);}} reference{c->device};
    Locked guard(c->lock);c->initializing=false;
    if(c->adapter!=adapter)return STATUS_SUCCESS;
    c->adapterStatus=Report(L"AdapterInitFinished",in->AdapterInitStatus);c->adapterReady=NT_SUCCESS(in->AdapterInitStatus);
    c->ready=c->adapterReady&&c->powered;
    return STATUS_SUCCESS; // No monitor is created at startup.
}
_Use_decl_annotations_ NTSTATUS ReleaseHardware(WDFDEVICE device,WDFCMRESLIST resources){
    UNREFERENCED_PARAMETER(resources);auto c=DeviceFrom(device)->value;Locked guard(c->lock);
    // IddCx destroys its adapter/monitor objects when this PnP device stops.
    auto previous=c->owner;c->owner=nullptr;c->monitor=nullptr;c->adapter=nullptr;
    c->ready=false;c->adapterReady=false;c->initializing=false;c->powered=false;
    if(previous)WdfObjectDereference(previous);return STATUS_SUCCESS;
}
NTSTATUS DeviceContext::Connect(WDFFILEOBJECT file) noexcept {
    if(!ready||!powered)return STATUS_DEVICE_NOT_READY;
    if(!file)return STATUS_INVALID_PARAMETER;
    if(monitor)return owner==file?STATUS_SUCCESS:STATUS_DEVICE_BUSY;
    WDF_OBJECT_ATTRIBUTES attr;WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr,MonitorWrapper);
    attr.EvtCleanupCallback=[](WDFOBJECT o){auto w=MonitorFrom(o);delete w->value;w->value=nullptr;};
    IDDCX_MONITOR_INFO info{};info.Size=sizeof(info);info.MonitorType=DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
    info.ConnectorIndex=0;info.MonitorDescription.Size=sizeof(info.MonitorDescription);
    info.MonitorDescription.Type=IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
    info.MonitorContainerId={0xcb81b71e,0x2ad0,0x49d2,{0x88,0x22,0x73,0xad,0x85,0x1c,0x18,0x36}};
    IDARG_IN_MONITORCREATE in{};in.ObjectAttributes=&attr;in.pMonitorInfo=&info;
    IDARG_OUT_MONITORCREATE out{};auto s=IddCxMonitorCreate(adapter,&in,&out);
    if(!NT_SUCCESS(s))return s;
    auto context=new(std::nothrow)MonitorContext;
    if(!context){WdfObjectDelete(out.MonitorObject);return STATUS_INSUFFICIENT_RESOURCES;}
    MonitorFrom(out.MonitorObject)->value=context;
    context->fault={faultWork,&pendingFault,++monitorGeneration};
    IDARG_OUT_MONITORARRIVAL arrival{};s=IddCxMonitorArrival(out.MonitorObject,&arrival);
    if(!NT_SUCCESS(s)){WdfObjectDelete(out.MonitorObject);return s;}
    WdfObjectReference(file);monitor=out.MonitorObject;owner=file;return STATUS_SUCCESS;
}
_Use_decl_annotations_ void FileCleanup(WDFFILEOBJECT file){
    auto c=DeviceFrom(WdfFileObjectGetDevice(file))->value;
    if(c&&c->lock){Locked guard(c->lock);if(c->owner==file)c->Disconnect(file);}
}
_Use_decl_annotations_ void StopFailedMonitor(WDFWORKITEM work){
    auto c=DeviceFrom(work)->value;Locked guard(c->lock);
    auto generation=static_cast<std::uint64_t>(InterlockedCompareExchange64(&c->pendingFault,0,0));
    if(!c->monitor||generation!=c->monitorGeneration)return;
    c->adapterStatus=STATUS_DEVICE_HARDWARE_ERROR;c->ready=false;c->adapterReady=false;
    c->Disconnect(nullptr,true);
    // Remain stopped until an explicit PnP restart; do not spin or crash/restart UMDF.
}
_Use_decl_annotations_ void ControlRequest(WDFDEVICE device,WDFREQUEST request,size_t output,size_t input,ULONG code){
    if(code!=Control){WdfRequestComplete(request,STATUS_INVALID_DEVICE_REQUEST);return;}
    if(input!=sizeof(Request)||output<sizeof(State)){WdfRequestComplete(request,STATUS_INFO_LENGTH_MISMATCH);return;}
    Request* in=nullptr;State* out=nullptr;
    auto s=WdfRequestRetrieveInputBuffer(request,sizeof(Request),reinterpret_cast<void**>(&in),nullptr);
    if(!NT_SUCCESS(s)){WdfRequestComplete(request,s);return;}
    Request copy=*in;
    if(!Valid(copy)){WdfRequestComplete(request,STATUS_INVALID_PARAMETER);return;}
    s=WdfRequestRetrieveOutputBuffer(request,sizeof(State),reinterpret_cast<void**>(&out),nullptr);
    if(!NT_SUCCESS(s)){WdfRequestComplete(request,s);return;}
    auto c=DeviceFrom(device)->value;auto file=WdfRequestGetFileObject(request);
    {
        Locked guard(c->lock);
        if(copy.action==Action::Connect)s=c->Connect(file);
        else if(copy.action==Action::Disconnect)s=c->Disconnect(file);
        if(NT_SUCCESS(s)){
            *out=State{};out->ready=c->ready?1:0;out->connected=c->monitor?1:0;out->owned=c->owner==file&&c->monitor?1:0;out->adapter_status=static_cast<std::uint32_t>(c->adapterStatus);
        }
    }
    if(NT_SUCCESS(s))WdfRequestCompleteWithInformation(request,STATUS_SUCCESS,sizeof(State));
    else WdfRequestComplete(request,s);
}
static void Signal(DISPLAYCONFIG_VIDEO_SIGNAL_INFO& s,bool monitor){
    s.activeSize.cx=1920;s.activeSize.cy=1080;s.totalSize.cx=2200;s.totalSize.cy=1125;
    s.AdditionalSignalInfo.vSyncFreqDivider=monitor?0:1;s.AdditionalSignalInfo.videoStandard=255;
    s.vSyncFreq.Numerator=60;s.vSyncFreq.Denominator=1;s.hSyncFreq.Numerator=67500;s.hSyncFreq.Denominator=1;
    s.scanLineOrdering=DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;s.pixelRate=148500000;
}
_Use_decl_annotations_ NTSTATUS ParseDescription(const IDARG_IN_PARSEMONITORDESCRIPTION* in,IDARG_OUT_PARSEMONITORDESCRIPTION* out){
    *out={};
    // This device advertises an EDID-less virtual monitor. Register the required
    // parser callback, but never pretend to decode arbitrary external EDID data.
    if(in->MonitorDescription.DataSize)return STATUS_NOT_SUPPORTED;
    out->MonitorModeBufferOutputCount=1;out->PreferredMonitorModeIdx=0;
    if(!in->MonitorModeBufferInputCount)return STATUS_SUCCESS;
    if(!in->pMonitorModes)return STATUS_INVALID_PARAMETER;
    auto& mode=in->pMonitorModes[0];mode={};mode.Size=sizeof(mode);mode.Origin=IDDCX_MONITOR_MODE_ORIGIN_DRIVER;Signal(mode.MonitorVideoSignalInfo,true);return STATUS_SUCCESS;
}
_Use_decl_annotations_ NTSTATUS DefaultModes(IDDCX_MONITOR monitor,const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* in,IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* out){
    UNREFERENCED_PARAMETER(monitor);out->DefaultMonitorModeBufferOutputCount=1;out->PreferredMonitorModeIdx=0;
    if(in->DefaultMonitorModeBufferInputCount==0)return STATUS_SUCCESS;
    if(!in->pDefaultMonitorModes)return STATUS_INVALID_PARAMETER;
    auto& m=in->pDefaultMonitorModes[0];m={};m.Size=sizeof(m);m.Origin=IDDCX_MONITOR_MODE_ORIGIN_DRIVER;Signal(m.MonitorVideoSignalInfo,true);return STATUS_SUCCESS;
}
_Use_decl_annotations_ NTSTATUS TargetModes(IDDCX_MONITOR monitor,const IDARG_IN_QUERYTARGETMODES* in,IDARG_OUT_QUERYTARGETMODES* out){
    UNREFERENCED_PARAMETER(monitor);out->TargetModeBufferOutputCount=1;
    if(in->TargetModeBufferInputCount==0)return STATUS_SUCCESS;
    if(!in->pTargetModes)return STATUS_INVALID_PARAMETER;
    auto& m=in->pTargetModes[0];m={};m.Size=sizeof(m);Signal(m.TargetVideoSignalInfo.targetVideoSignalInfo,false);return STATUS_SUCCESS;
}
_Use_decl_annotations_ NTSTATUS CommitModes(IDDCX_ADAPTER adapter,const IDARG_IN_COMMITMODES* in){UNREFERENCED_PARAMETER(adapter);UNREFERENCED_PARAMETER(in);return STATUS_SUCCESS;}
_Use_decl_annotations_ NTSTATUS Assign(IDDCX_MONITOR monitor,const IDARG_IN_SETSWAPCHAIN* in){
    auto c=MonitorFrom(monitor)->value;
    c->processor.reset();
    if(c->budget.blocked)return STATUS_GRAPHICS_INDIRECT_DISPLAY_ABANDON_SWAPCHAIN;
    auto p=std::unique_ptr<FrameProcessor>(new(std::nothrow)FrameProcessor(in->hSwapChain,in->hNextSurfaceAvailable,c->budget,c->fault));
    if(!p){FrameFailure(c->budget,c->fault);return STATUS_GRAPHICS_INDIRECT_DISPLAY_ABANDON_SWAPCHAIN;}
    auto s=p->Start(in->RenderAdapterLuid);if(!NT_SUCCESS(s)){FrameFailure(c->budget,c->fault);return STATUS_GRAPHICS_INDIRECT_DISPLAY_ABANDON_SWAPCHAIN;}
    c->processor=std::move(p);return STATUS_SUCCESS;
}
_Use_decl_annotations_ NTSTATUS Unassign(IDDCX_MONITOR monitor){MonitorFrom(monitor)->value->processor.reset();return STATUS_SUCCESS;}

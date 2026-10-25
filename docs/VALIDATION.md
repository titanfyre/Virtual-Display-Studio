# Validation

Application/SDK preview 0.1.1; matching driver candidate 0.1.1.0, Windows x64.

| Check | Result |
| --- | --- |
| C++ app, console and both installer sources | Pass, MSVC /W4 /WX and CFG |
| Offline protocol and failure-budget regression | Pass, 100,018 assertions |
| Offline package identity and unsigned-file rejection | Pass, 11 assertions |
| Returned Microsoft catalog trust and INF/DLL membership | Pass for returned 0.1.0.0 package |
| Positive signed-package check plus altered INF/DLL rejection | Pass, 22 assertions |
| Protocol under AddressSanitizer | Pass |
| Private UMDF 2.25 / IddCx 1.4 compilation and PREfast | Pass, zero reported defects |
| InfVerif /w, /u; Inf2Cat Windows 10/11 x64 | Pass for 0.1.1.0 candidate |
| INF platform selection regression | Pass for builds 19041, 19045, 22000, 22621 and 26100; offline structure checks |
| Corrected candidate in Windows 10 build 19045 VM | Pass: 91 checks, 32 connect/disconnect cycles |
| Same candidate with dedicated UMDF Verifier enabled before load | Pass: 91 checks, 32 connect/disconnect cycles |
| Admin-only access, invalid requests and ownership | Pass in VM |
| Handle-close and abrupt client-exit monitor cleanup | Pass in VM |
| Candidate PnP state and uninstallation | Pass; problem code 0 and exact installed DLL verified |
| New 0.1.1.0 Microsoft-signed installation | Pending returned signature |
| Windows 11 live installation and operation | Pending |
| Physical Intel/AMD/NVIDIA rendering, HDR, suspend/resume and GPU loss | Not qualified; HDR is not implemented |

The VM tests used a disposable test certificate and test mode only inside the guest. The original VM was restored with all nine historical checkpoints retained; no host installation or host security change occurred. These tests establish the exercised control/lifecycle behavior, not physical GPU rendering quality, latency, universal compatibility or production certification.

## Why a fresh signature is required

The original 0.1.0.0 return had a valid Microsoft signature and the submitted image content was preserved. Actual Windows 10 installation nevertheless failed with `0xE0000219`: its INF referenced `WUDFRD.inf`, an inbox file available only on Windows 11 and later. The candidate now selects an explicit reflector service on Windows 10 and the inbox Include/Needs sections on Windows 11.

A second startup failure was traced to `IddCxDeviceInitConfig`, returning `STATUS_INVALID_PARAMETER` because the client configuration omitted the monitor-description parser callback. The candidate registers that callback and handles its advertised EDID-less mode without claiming to parse arbitrary external EDID content. The device then started successfully in the VM. INF/catalog and driver code changes invalidate the applicability of the earlier signature to the new package; the old signed package is not shipped as a substitute.

Initialization-only ETW diagnostics expose the failing stage and NTSTATUS when explicitly enabled by support tooling. They contain no frames, inputs, application names or desktop contents, and there is no per-frame event logging.

## Deployment boundaries

Opening the app normally does not install a driver or create a monitor. The upcoming setup requires a matching Microsoft-signed catalog and verifies the package identity and INF/DLL membership before installation. The preview ZIP intentionally omits driver and setup binaries while the corrected signature is pending.

Qualification still needs Windows 11, physical GPUs, hardware and remote sessions, device removal during active use, power transitions, graphics resets, update/rollback and longer workloads. No guarantee of zero bugs, overhead or bypass is made. Driver binaries have separate terms and retained Microsoft MS-PL notices.

References: [UMDF reflector installation by Windows version](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/adding-the-reflector), [IddCx client configuration](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/ns-iddcx-idd_cx_client_config), [UMDF Verifier](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-umdf-verifier).

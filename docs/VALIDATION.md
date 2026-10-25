# Validation

Application 0.1.2; Microsoft-signed UMDF driver 0.1.1.0, Windows x64.

| Check | Result |
| --- | --- |
| C++ app, console and both installers | Pass, MSVC /W4 /WX, static runtime and CFG |
| Offline protocol and failure-budget regression | Pass, 100,018 assertions |
| Package identity and unsigned-file refusal | Pass, 11 assertions |
| Corrected Microsoft catalog, INF/DLL membership and altered-file refusal | Pass, 22 assertions |
| Exact submitted INF and Authenticode-protected returned DLL content | Match |
| Embedded UI/console installer INF/CAT/DLL resources | Exact returned package |
| Corrected private driver PREfast, INF and catalog checks | Pass, zero reported defects |
| INF platform-selection structure | Pass for 19041, 19045, 22000, 22621 and 26100; offline only |
| Returned signed package, Windows 10 build 19045 x64 | Pass: actual console install/uninstall, 91 checks, 32 connect/disconnect cycles |
| Same signed package, dedicated UMDF Verifier before load | Pass: 91 checks, 32 cycles |
| Administrator-only control, invalid requests and exclusive monitor ownership | Pass in guest |
| Handle-close and abrupt client-exit monitor departure | Pass in guest |
| PnP state / exact installed DLL | Problem code 0 / SHA-256 matches return |
| Windows 11 live operation and physical GPU rendering | Not yet qualified |
| HDR, variable refresh, multiple monitors, streaming or licensing | Not implemented |

Both signed-package campaigns disabled guest test-signing and used the production console installer. The original VM and all nine historical checkpoints were restored. No driver was installed on the host and no host security setting was changed. The UI installer's payload was byte-verified; automated operations exercised the console installer.

These bounded tests establish control and monitor lifecycle behavior in the Windows 10 VM. They do not establish physical Intel/AMD/NVIDIA rendering, image quality, application/game behavior, remote-session compatibility, physical latency, power/GPU-reset behavior or universal support. The device starts disconnected; no desktop images are stored or transmitted by the product.

## Package provenance

The original 0.1.0.0 return was correctly signed but failed Windows 10 installation because its INF depended on the Windows 11 WUDFRD.inf. The corrected 0.1.1.0 package uses explicit Windows 10 reflector installation and Windows 11 Include/Needs sections, and supplies the required IddCx monitor-description callback. The returned Microsoft-signed files match that corrected submission, excluding expected PE signature/checksum differences. The obsolete return is preserved privately and is not substituted in this release.

Initialization-only ETW diagnostics expose the failure stage and NTSTATUS when explicitly enabled. They contain no frames, inputs, application names or desktop contents and add no per-frame event logging.

## Deployment boundaries

Both setup executables verify the exact package identity, trusted Microsoft hardware-publisher catalog and INF/DLL catalog membership. They never enable test-signing or create a monitor automatically. Restart is requested only when Windows requires it. Driver source is private; upstream Microsoft MS-PL notices and the full license accompany the binary distribution.

Further qualification includes Windows 11, representative physical GPUs and display applications, device removal during active rendering, power transitions, graphics resets, update/rollback and extended workloads. A Microsoft signature is not WHQL certification, product endorsement or a guarantee of zero defects.

References: [UMDF reflector installation](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/adding-the-reflector), [IddCx configuration](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/ns-iddcx-idd_cx_client_config), [UMDF Verifier](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-umdf-verifier).

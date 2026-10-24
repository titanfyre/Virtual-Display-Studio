# Validation

Version 0.1.0 preview, Windows x64.

| Check | Result |
| --- | --- |
| C++ GUI, console, UI setup and console setup compilation | Pass, MSVC /W4 /WX and CFG |
| Fixed protocol/recovery invalid action/version/size/reserved checks | Pass, 100,018 assertions |
| Exact INF hardware/publisher/catalog policy and rejection of unsigned files | Pass, 11 assertions |
| Protocol model under AddressSanitizer | Pass |
| Private UMDF 2.25 / IddCx 1.4 driver compilation | Pass, WDK 26100, PREfast, /W4 /WX and CFG |
| InfVerif Windows Driver and universal checks | Pass |
| Inf2Cat Windows 10/11 x64 signability | Pass |
| Microsoft-signed catalog | Pending |
| Windows 10/11 driver installation and PnP state | Pending |
| Physical GPU rendering, sleep/resume, process-crash cleanup | Pending |
| Repeated connect/disconnect and device stop/start | Pending |

Offline tests do not open the driver or alter display settings. Opening the GUI normally does not open a driver handle, install a device or connect a monitor. No host driver installation or virtual-display runtime test has been performed.

The production validation matrix must include Intel/AMD/NVIDIA GPUs, hardware/remote sessions, suspend/resume, disable/enable, owner process termination, a second administrator observer, malformed IOCTL requests, abrupt device removal, driver update/uninstall and a long-running connect/disconnect loop after Microsoft signing. A compile or an INF check is not evidence that these runtime cases pass.

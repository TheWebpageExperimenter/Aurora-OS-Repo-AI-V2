# Aurora OS Alpha 1.0.0

Aurora is moving from milestone releases to the **Alpha Developer Stage**. This is an experimental kernel-only build.

## Included
- Darker, clearer translucent taskbar styling with reduced white highlight/opacity.
- Five selectable procedural wallpapers: Aurora, Blue Horizon, Neon Bloom, Emerald, and Solar Flare.
- Browser preview wording updated to report the actual networking limitation instead of implying a certificate-only issue.
- Existing kernel network-adapter discovery and TLS status APIs retained.

## Important limitations
- The current `net.c` only discovers supported PCI network adapters. It does not start a NIC driver, send/receive packets, or implement TCP/IP/DNS/HTTP.
- `tls.c` deliberately fails closed. There is no TLS handshake, X.509 chain/hostname validation implementation, or trusted CA store.
- Installing root certificates alone cannot make HTTPS work without the transport and TLS implementations.
- Firefox is not bundled and cannot run inside this kernel-only environment; the browser window remains a diagnostic preview, not a working browser. Google, Bing, and arbitrary websites are not reachable in this alpha.
- Build validation is not a runtime boot test.

# P2-S1 host checks

Run from any directory:

```text
python <nearlink-repository>/test/mytest/host/p2_host_checks.py --cc gcc --cxx g++
```

The runner compiles the production codec, client/server state machines, channel, service and status parcel implementation. SSAP, QoSM, TUN, security, task scheduling and parcel storage have explicit host stubs. The CP send test compiles the production send functions extracted without modification, with allocator/scheduler boundaries mocked. The C syntax checks use the real stack headers except platform logging and CP-worker declarations. The probe syntax check stubs Linux and AccessToken headers on Windows.

Coverage includes dual configuration, capability intersection, one explicit fallback, no security/SSAP fallback, reservation failure and rollback, duplicate responses, 1500-byte interleaved traffic, PI/version/length/source/generation rejection, IPv4 DHCP admission, cancellation/late completion, stop/start generation isolation, private transaction IDs 0–9, truncated parcel reads and bounded mode lists.

These tests do not prove a product build, actual Binder/AccessToken/CFI behavior, a real TUN, radio interoperability, address configuration or Internet access. They add no GN executable, shared library or device CLI. The device executable remains `sleip_nearlink_stage1`.

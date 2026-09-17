# Engineering package: not yet an end-user release

This unsigned candidate targets the **original HP LaserJet 1005 Series** over USB
on Windows x64. It is not for the P1005 or M1005. Do not use this candidate to
replace a working installation: publicly trusted signing and the end-user
installation flow are still release gates. There is no Setup program or supported
uninstaller yet. Package installation and offline GDI captures passed on a fresh
Windows 11 Pro 25H2 x64 VM using a certificate trusted only inside that guest;
this does not establish public trust or physical compatibility of this package.

## Contents and requirements

- `driver/`: package-aware Type 3 INF, GPD, INI, renderer, firmware monitor and
  **unsigned** catalog. The catalog is an integrity inventory, not a signature.
- `tools/lj1005.exe`: optional command-line diagnostic utility.
- `BUILD-INFO.json`: build provenance and matching source archive hash.
- `SHA256-MANIFEST.json`: file hashes, excluding the manifest itself.
- `LICENSE` and `THIRD_PARTY.md`: GPL terms and codec attribution.

The DLLs and utility statically link the MSVC runtime. A separately installed
Visual C++ redistributable was not needed for the clean-VM installation and GDI
capture test. Windows supplies Unidrv and the USB printer port driver.
Neither Windows binaries nor HP firmware are included.

Automatic firmware upload requires the original `sihp1005.img`, obtained by the
user from their HP driver package. The accepted 116892-byte image has SHA-256:

```
638b9ad4a2cf6888208926ef7c7de073da8ba4cded25d31000a585c4526a9ca1
```

The firmware-import UI is not implemented. Development scripts exist in the
matching source archive, but they are not the clean-machine deployment path.

Windows Protected Print Mode must be off because it excludes third-party
drivers. Setup must report this condition and respect administrator policy;
it must not silently turn the setting off. Normal driver-signature enforcement
and Secure Boot must remain enabled for release acceptance.

## Clean-machine acceptance procedure

The lab-signing, package-registration and file-output steps below passed in the
25H2 VM on 2026-09-17. Production trust, firmware/USB, installer lifecycle and
additional OS builds remain pending. See the source repository's
`docs/TEST_STATUS.md` and `docs/VM_TESTING.md` for scope and evidence.

Use a disposable VM snapshot or spare PC. Record the exact Windows edition,
build, architecture and security settings. No Visual Studio, WDK, previous Open
ZJS installation or manually copied Unidrv files should be present. A VM is
sufficient for package installation and file-output checks; physical printing
also needs usable USB passthrough or a real PC.

1. Verify both archive hashes and every file in the binary manifest. Preserve
   the matching source archive with the results.
2. Establish the signing route in the disposable environment. Any lab certificate
   is only for lab testing; importing one does not prove public trust. For final
   acceptance use the intended production signature and an otherwise unchanged
   Windows trust store. Never disable signature enforcement to declare a pass.
3. Stage the signed INF package through Windows driver-package installation and
   register the printer driver through the print subsystem. Confirm Windows
   resolves the declared Unidrv core dependency from its own driver store. Do
   not run `install-dev.ps1` or pre-copy files into the spool directory.
4. Inspect installed driver files, package-aware status, monitor registration and
   renderer loading. Capture a page to a file before connecting hardware.
5. Test firmware import, exact device/port matching, bidirectional configuration,
   A4 defaults and creation of one queue. Power-cycle, then print one controlled
   page and confirm job completion. No-paper recovery must not duplicate it.
6. Exercise repair, upgrade, uninstall and rollback. Confirm unrelated queues,
   monitors, drivers, jobs and files survive unchanged. Repeat on a fresh snapshot.

Record each result as passed, failed or not tested. WDK validation alone is not
installation, WHQL certification, signature trust or physical compatibility.

## Signing order

Choose the publisher identity and validate the available signing route before
promising a public install experience. If signing executable payloads, do so
before regenerating the catalog; then sign and timestamp the catalog. Recompute
the manifest and archive hashes after signing. The current candidate builder
does not perform signing or publish assets. A future release pipeline must
verify trust and catalog membership before producing a distributable asset.

## Sources

- [Microsoft: Unidrv core-driver packaging](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/package-aware-print-drivers-that-share-files)
- [Microsoft: core-driver dependencies](https://learn.microsoft.com/en-us/windows-hardware/drivers/print/using-core-drivers)
- [Microsoft: Inf2Cat](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/inf2cat)
- [Microsoft: Windows Protected Print Mode](https://learn.microsoft.com/en-us/windows/modern-print/windows-protected-print-mode/windows-protected-print-mode)

# Verify a Have Disk tester ZIP in the VM

Use an isolated Windows 11 Pro x64 VM with Secure Boot enabled. Preserve the starting VM state. Record the Windows build and revision, archive SHA-256, signer identity and source commit. Start with the Open ZJS driver, firmware and tester certificate absent. Record the existing printers and default queue.

1. Verify the release checksums, extract the entire ZIP inside the guest and compare the tester certificate with its separately recorded identity. Grant tester trust only inside the disposable guest as described in the [installation guide](MINI_INSTALL.md).
2. Check that a missing image and a wrong image each prevent staging. Supply the original image beside the INF, verify its hash, and check catalog membership for the image, INF, GPD, INI and both DLLs using SignTool.
3. Follow the wizard instructions with **FILE:** as the port and sharing disabled. Record the selected model, prompts and initial preferences. Check that the installed monitor, renderer, printer descriptions and firmware match the supplied files. Select A4 for the file tests.
4. Move the extracted folder aside. Use the queue helper with explicit output files for portrait, landscape, blank, multi-page and Letter. Validate each stream, compare geometry and reference bytes where available, and confirm the queue clears. Record whether installation used the interactive wizard or a scripted Windows INF install.
5. Remove the guest queue and driver package. Record any remaining monitor registration, monitor DLL and firmware. Verify unrelated printers and the default. Remove only the tester certificate added for this run from both guest stores, or restore the recorded baseline checkpoint. Distinguish successful checkpoint restoration from testing manual cleanup.

Record results against the exact ZIP. FILE: tests exercise rendering and spooling. Physical USB, firmware transfer and paper handling have their own [acceptance matrix](RELEASE_ACCEPTANCE.md).

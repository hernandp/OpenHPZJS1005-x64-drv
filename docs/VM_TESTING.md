# Verify a Have Disk tester ZIP in the VM

Use an isolated Windows 10 22H2 x64 or Windows 11 x64 VM with Secure Boot enabled. Run the procedure on each version for which you want to record VM coverage. Preserve the starting VM state. Record the Windows build and revision, archive SHA-256, signer identity and source commit. Start with the Benteveo driver, firmware and tester certificate absent. Record the existing printers and default queue.

1. Verify the release checksums, extract the entire ZIP inside the guest and compare the tester certificate with its separately recorded identity. Grant tester trust only inside the disposable guest as described in the [installation guide](MINI_INSTALL.md).
2. Check that a missing image and a wrong image each prevent staging. Supply the original image beside the INF, verify its hash, and check catalog membership for the image, INF, GPD, INI and all driver DLLs using SignTool.
3. Follow the wizard instructions with **FILE:** as the port and sharing disabled. Record the selected model, prompts and initial preferences. Check that the installed monitor, renderer, UI plug-in, printer descriptions and firmware match the supplied files. Select A4 for the file tests.
4. Move the extracted folder aside. Use the queue helper with explicit output files for portrait, landscape, blank, multi-page and Letter. Validate each stream, compare geometry and reference bytes where available, and confirm the queue clears. Record whether installation used the interactive wizard or a scripted Windows INF install.
5. Remove the guest queue and driver package through Windows dialogs. Follow the installation guide's manual cleanup blocks for the monitor and firmware. Exercise refusal with a dependent queue/driver, an identity mismatch, already absent files and repeated execution. If Windows reports the driver is in use, restart and retry. Record the actual result of every stage.
6. Reinstall the same package, restore recorded preferences and capture a page. Repeat complete removal, then install a distinguishable test version and verify the installed DLL versions, hashes, settings and file output. Record whether each reinstall used dialogs or scripted Windows APIs.
7. Verify unrelated printers and the default. Remove only the tester certificate added for this run from both guest stores, then confirm absence and signature trust failure. Preserve evidence before restoring the baseline checkpoint. Distinguish successful checkpoint restoration from testing manual cleanup.

Record results against the exact ZIP. FILE: tests exercise rendering and spooling. Physical USB, firmware transfer and paper handling have their own [acceptance matrix](RELEASE_ACCEPTANCE.md).

For alignment, verify that **Benteveo Alignment** starts at zero in millimeters.
Capture an unshifted A4 page with asymmetric markers, then apply **+5.08 mm**
horizontally and **−2.54 mm** vertically. Switch to inches and confirm **+0.2 in**
and **−0.1 in**. Reopen preferences and check that both values and the unit
persisted. Cancel a change, then override both values in one application job
and check that the saved defaults persist. Compare decoded portrait and
landscape captures at 600 dpi for direction, distance, clipping, and scale.
On a physical A4 sheet, measure the displacement against a known unshifted
reference of the same fixture.

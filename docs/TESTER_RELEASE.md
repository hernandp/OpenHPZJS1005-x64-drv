# Opt-in self-signed testing packages

Decision recorded 2026-10-20. This is the agreed distribution route for willing
technical testers while public signing is unresolved. **No public tester download
has been approved.** Existing CI artifacts remain unsigned engineering candidates.
Local self-signed rehearsal packages must pass the manual VM checks below before
publication is considered.

On 2026-10-21, the local `tester-31fee15d` archive passed a clean-VM command-line
rehearsal of trust refusal, explicit trust, tamper rejection, installation,
repair, GDI smoke printing, removal and certificate cleanup. See
[test status](TEST_STATUS.md) for the exact hash and remaining acceptance limits.
This does not approve publication or replace final-asset validation.

The ordinary public release still requires publicly trusted signing and final
release acceptance. A testing package must be labelled **SELF-SIGNED TEST BUILD —
NOT A PUBLICLY TRUSTED RELEASE** on its download page and in its included README.
It must not be presented as WHQL-certified or as a normal signed release.

## What testers are choosing

Windows must trust the certificate used to sign the package catalog. A tester
explicitly adds the designated public certificate to the local computer's
Trusted Root Certification Authorities and Trusted Publishers stores.
Administrator permission is required. This changes trust on that computer:
it can authorize other content signed with the same private key, not just one
download. A matching checksum verifies identity against the published value;
it does not establish that a package is safe or Microsoft-certified.

Setup must never import certificates, enable test mode, disable signature
enforcement or change Secure Boot/security policy. Do not silently perform those
actions in a wrapper installer either. Our guest-local certificate route already
worked with Secure Boot and signature enforcement enabled; each intended tester
configuration still needs validation. If policy blocks installation, stop and
report the error rather than weakening the policy.

Use a spare test PC where possible. Initial testing targets the original HP
LaserJet 1005 over USB on Windows 11 Pro 25H2 x64. Physical packaged-driver
acceptance is still pending. Other editions/builds and physical Letter support
must not be inferred from the installer minimum or VM file captures.
An existing development installation needs the separate
[migration and recovery procedure](DEVELOPMENT_MIGRATION.md).

## Maintainer checklist before offering a download

- Prepare a dedicated testing-signing identity and protect its private key
  outside source, CI artifacts and downloads. Do not distribute a PFX, private
  key, password, VM credentials, lab recovery data, HP firmware or Windows files.
  The existing disposable VM certificate is not a public distribution identity.
- Freeze the source revision and build a candidate with the existing validation
  pipeline. Apply any executable signatures before regenerating the catalog;
  sign the catalog afterwards. Record certificate identity, validity dates,
  algorithms and timestamp status. Do not promise operation after expiration
  without testing that behavior.
- Export only the public `.cer`. Publish its exact subject, certificate
  thumbprint (with algorithm identified), SHA-256 of the `.cer` file, and
  validity period on the project's release page, separately from the ZIP.
  These values must be concrete before testers are instructed to import it.
- Include this guide, the test-build label, supported/tested Windows versions,
  known limitations, matching GPL source, build provenance and a manifest
  regenerated after all signing and documentation changes. Publish final archive
  SHA-256 values. Check catalog membership and signature against the designated
  certificate; never reuse the unsigned candidate's ZIP/hash as the signed asset.
- Rehearse the exact distributed assets in a fresh VM: refusal without added
  trust, explicit certificate installation, successful installation and repair,
  upgrade/rollback where applicable, removal, restart/retry and certificate
  cleanup. Verify unrelated certificates and printers are preserved. Retain logs.
- Publish as an explicitly opt-in prerelease only after reviewing those assets
  and results. `package-candidate.ps1` still creates unsigned candidates.
  `package-test-signed.ps1` creates a separate signed rehearsal archive from its
  verified ZIPs; it never adds trust, installs a driver, or publishes assets.

### Preparing a local rehearsal package

Use PowerShell 7 on the signing workstation. Create the identity once with
`scripts/new-test-signing-identity.ps1 -OutputDirectory artifacts/test-signing-public`.
It creates a non-exportable RSA-3072 key in the current user's Personal store and
exports only the public certificate and identity metadata. It does not grant
trust. Keep this signing account and machine available; loss of the non-exportable
key means a new identity and an explicit certificate transition.

Build an unsigned candidate with `scripts/package-candidate.ps1`, then pass that
candidate directory and the identity's thumbprint to
`scripts/package-test-signed.ps1 -CandidatePath ... -CertificateThumbprint ...`.
Supply `-WdkRoot`, `-SignTool` and `-PythonExe` when tools are not at their defaults.
The script signs the four shipped PE files, regenerates/signs the catalog,
exports the public certificate and regenerates manifests/archive hashes. Current
rehearsal signatures are **not timestamped**; validity after certificate expiry
is not promised. Host signature diagnostics do not substitute for Windows trust
verification in the guest.

Follow [the manual VM checklist](VM_TESTER_REHEARSAL.md) using the exact output
ZIP and separately checked identity. Record install/repair/remove and certificate
cleanup results before any publication decision. A modified source snapshot is
recorded honestly and suitable for local rehearsal; freeze a reviewed revision
and revalidate the final artifacts before offering a public tester download.

## Tester installation once an identified build is available

1. Read the build-specific limitations and check its Windows version and printer
   model. Preserve recovery data before replacing any working installation.
2. Obtain the complete test ZIP and public certificate from the project release.
   Compare their SHA-256 values and the certificate's identity, thumbprint and
   validity with the separately published release information. Stop if anything
   differs. Never import a certificate supplied only by an unsolicited message.
3. If you accept the trust change, use Windows certificate management for the
   **Local Computer** to import that exact `.cer` into **Trusted Root
   Certification Authorities**, and separately into **Trusted Publishers**.
   Inspect the destination stores and certificate identity before confirming.
   Do not import a private-key file or unrelated certificates.
4. Extract the complete ZIP and follow the [installation guide](USER_GUIDE.md).
   Supply your own original `sihp1005.img`. Catalog signing authenticates the
   driver package; it does not automatically sign `Setup.exe`. The release must
   separately disclose Setup's signature status and expected elevation publisher.
   If a security policy blocks execution or installation, report it and stop.
5. Keep the complete extracted build and certificate information for repair,
   upgrade and recovery. Record version, Windows build, application bitness,
   selected paper/orientation/copies, expected/actual pages and exact errors.
   Use the [acceptance matrix](RELEASE_ACCEPTANCE.md); report failures as failures.
   Share harmless reproductions, not personal documents or firmware.

## Leaving the test

Use Setup's **Remove**, completing any requested restart/retry, before removing
certificate trust. Confirm that no other retained test installation needs this
certificate. In Local Computer certificate management, remove only the exact
designated certificate from each store where you installed it, identifying it by
the recorded thumbprint. Leave pre-existing and unrelated certificates alone.
Setup does not remove certificates automatically.

Removing trust does not uninstall installed software or undo its changes. Keep
the recovery backup until removal/restoration has been verified. Future repair
or rollback to a test-signed package may require its certificate again. Migration
from a test build to the future publicly signed build must be rehearsed; identical
version numbers do not establish that differently signed packages are interchangeable.

Hardware results from this route are useful evidence. They do not validate the
public trust experience. Repeat production-trust installation and a focused
printing/recovery regression with the final publicly signed release.

## References

- [Microsoft: installing test certificates](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/installing-test-certificates)
- [Microsoft: Trusted Publishers store](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/trusted-publishers-certificate-store)
- [Microsoft: Driver Store signature requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/driver-store)

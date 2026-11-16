# Changelog

Versions use `major.minor.release`. Windows driver metadata adds a trailing
`.0`. Dates identify when each version was introduced in the repository, not
public release dates. Earlier entries summarize changes made while that version
was current; these were development and tester builds.

## 0.1.2 — 2026-11-14

- Retry temporary USB disconnects while awaiting job completion, for up to 30 seconds per interruption.
- Keep recovery cancellable without replaying print data or acknowledging an unconfirmed job.
- Add disconnect, reconnect and retry-timeout trace events and regression coverage.
- Record physical A4 printing, paper-out, cover recovery and five cold-cycle results for the preceding tester package.
- Document remaining USB recovery, sleep/wake, VM and signing acceptance gaps.
- Add this version history to the repository and source archives.

Status: signed tester candidate; timed USB-interruption recovery and its follow-up
were physically validated on November 16, 2026, on build 26300.9974. Public
signing, VM acceptance and broader release checks remain pending.

## 0.1.1 — 2026-09-27

- Add upgrade and rollback recovery to the former Setup installer, later retired.
- Generate consistent Windows binary versions and verify package contents in CI.
- Add support reports, recovery backup procedures and application-printing acceptance records.
- Introduce a dedicated self-signed technical-tester distribution channel.
- Correct PJL job boundaries and expand physical printing diagnostics.
- Make the minimal Have Disk ZIP the official deployment path, with user-supplied firmware.
- Remove legacy Setup deployment tooling and document manual reinstall and complete removal.
- Rename the project and visible driver identity to Benteveo; add the project picture and source license headers.

## 0.1.0 — 2026-09-16

- Introduce the Windows x64 Unidrv renderer, GPD and ZJS/JBIG encoding support.
- Add firmware loading, printer completion monitoring and diagnostic utilities.
- Prepare a package-aware driver and verify installation in a clean Windows VM.
- Add the original native installer with firmware selection and lifecycle checks.
- Validate complete ZJS jobs before direct USB printing.

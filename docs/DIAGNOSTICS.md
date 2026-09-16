# Completion diagnostics

The monitor emits optional Windows TraceLogging events from provider
`OpenZJS.LaserJet1005`, GUID `{e8d2763b-f16e-4b57-b14c-c637f7915011}`.
Keyword 1 records lifecycle stages and errors. Keyword 2, at verbose level 5,
records incoming printer status bytes (up to 4096 bytes per read). Document and
firmware writes are not traced. Tracing does not send commands or print pages.
The monitor preserves Win32 last-error values around tracing and ignores trace
registration/write failures so diagnostics do not change print results.

In an administrator PowerShell, from the project directory, start a bounded
16 MB circular capture **before** submitting the one authorized hardware job:

```powershell
$tracePath = Join-Path (Get-Location) ('artifacts\monitor-' + [guid]::NewGuid().ToString('N') + '.etl')
logman.exe create trace HPLJ1005-Hardware -p '{e8d2763b-f16e-4b57-b14c-c637f7915011}' 3 5 -o $tracePath -f bincirc -max 16 -ets
```

Check exit code zero before proceeding. An existing session with this name must
be inspected rather than silently replaced. Leave the printer on through the
completion check. Stop and decode the trace even if the hardware test fails:

```powershell
logman.exe stop HPLJ1005-Hardware -ets
tracerpt.exe $tracePath -o ([IO.Path]::ChangeExtension($tracePath, '.xml')) -of XML -y
```

Match events by the generated job token. A successful sequence includes
`FirmwareReady`, `AwaitSetupEcho`, `SetupEchoReceived`, `AwaitCompletion`, `PrinterEnd`, `LowerEndDoc` with success,
and `CompletionAcknowledged`. `CompletionFailure` includes the exception text
or lower-port Win32 error. `QueueCancelled` identifies deletion/cancellation;
`CompletionUnconfirmed` means no successful printer END report was accepted.
The status bytes distinguish a missing response from a response rejected by
the parser. A trace alone does not prove the printed page looks correct.

Before page transmission, the monitor waits up to 30 seconds for the unique
`@PJL ECHO READY-<token>` response included in its setup commands. This confirms
setup parsing, not page completion. `SetupEchoTimeout` means no raster data was
forwarded. An experimental empty-job probe was removed from the CLI after
hardware testing showed that it left job-name state which attached to later
printed pages. Power-cycle after reproducing any such experiment. Do not create
empty PJL jobs as readiness checks. The production monitor sends
its setup echo as part of the actual page-bearing job and does not create empty
probe jobs.

`scripts/verify-monitor-trace.ps1` captures and decodes these events using only
the simulated monitor. It requires an administrator shell, stops its unique
session in a finally block, and sends no physical job. It checks that decoded
events cover completion acknowledgement, cancellation, status read failure,
and incoming printer bytes.

The CLI `usb-read-status output.bin` reads pending USB responses without writing
anything to the printer. Do not run it while the monitor is waiting for an active
job: it could consume the very report the monitor needs. Turning the printer off
also loses volatile firmware and pending device responses.

Windows tracing references: [TraceLogging](https://learn.microsoft.com/en-us/windows/win32/tracelogging/trace-logging-about)
and [logman create trace](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/logman-create-trace).

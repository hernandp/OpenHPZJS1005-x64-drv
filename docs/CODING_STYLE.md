# C++ coding style

Derived from the maintainer's edits to `src/firmware.cpp` and `src/main.cpp` on 2026-09-16. Applies to project-owned `.cpp` and `.hpp` files in `src/`, `include/`, `driver/`, and `tests/`. Keep the vendored JBIG sources and reference C adapters in their existing style.

## Conventions from the examples

- Use four spaces per indentation level, including namespace contents. Do not use tabs.
- Put opening braces on the declaration or control-statement line. Put `else`, `else if`, and `catch` on a new line after the preceding closing brace.
- Use PascalCase for project-owned functions and types: `GetFirmwareState`, `EnsureFirmware`, `ReadFile`, `FirmwareTransport`.
- Use camelCase for variables, parameters, and data members: `seenMfg`, `seenFirmware`, `pageNumber`. A function-valued data member is still a data member (`loadImage`).
- Use uppercase names with underscores for named compile-time constants, such as `FILESIZE_LIMIT`. Keep lowercase namespaces, including `lj1005`.
- Write spaces around operators, after commas, and between control keywords and parentheses: `if (condition)`, `const auto end = id.find(';');`.
- Attach pointer and reference markers to the type: `const char*`, `const Raster&`.
- Put each statement on its own line. Avoid compressed function bodies or an `if`, its body, and a following statement on the same line.
- Separate logical steps and function definitions with blank lines. A blank line after a function's opening brace is allowed, as in the examples, but is not required everywhere.
- Simple one-statement guards may omit braces, with the statement on the next indented line. Use braces for multi-statement branches and wherever nesting could make scope ambiguous. Existing braces need not be removed for style alone.
- Use `const` for values that do not change and `auto` where the initializer makes the type clear. Keep explicit fixed-width types where protocol widths matter.

```cpp
namespace lj1005 {
    bool EnsureFirmware(const FirmwareTransport& transport) {

        if (GetFirmwareState(transport.identify()) == FirmwareState::ready)
            return false;

        const auto firmware = WrapFirmware(transport.loadImage());
        transport.write(firmware);

        for (unsigned int attempt = 0; attempt < 20; ++attempt) {
            transport.waitHalfSecond();

            if (GetFirmwareState(transport.identify()) == FirmwareState::ready)
                return true;
        }
        throw std::runtime_error("Printer did not report firmware readiness within 10 seconds");
    }
}
```

## Defaults for details not established by the examples

The formatter uses a 120-column target, permits short inline lambdas and empty bodies, aligns access labels with the class body boundary, and retains existing include order. Long indivisible literals may exceed the line target. These are practical defaults, not inferred requirements from the two edited files. Keep existing acronym spellings such as `ReadPBM`, `InspectZjs`, and `UsbSend`. Private class fields may retain a trailing underscore (`token_`, `setupReady_`); enum values use lower camel case (`missing`, `ready`).

Windows/COM methods, exports, SDK fields, standard-library members, and JBIG API names retain their required spelling. Do not rename `main`, `wmain`, `DllGetClassObject`, `QueryInterface`, or SDK callbacks to fit a general rule. Keep file-local helpers in anonymous namespaces: formatting must not change linkage, lifetime, ownership, wire data, or control flow.

## Formatting and verification

Use clang-format **22** and the root `.clang-format`. The PowerShell 7 helper finds it on PATH or in Visual Studio's LLVM tools; an explicit path can be supplied with `-ClangFormat`.

```powershell
./scripts/format.ps1          # Apply layout to project-owned C++ files
./scripts/format.ps1 -Check   # Check without editing
./scripts/build.ps1          # Build and run the seven offline test suites
```

The formatter controls layout; it does not rename identifiers, choose logical blank-line groupings, or prove that moved braces preserve behavior. Update declarations and callers together when renaming, and review the semantic diff before accepting a style change. Keep include order around Windows SDK headers intact. No formatter or build command above installs a driver or submits a page.

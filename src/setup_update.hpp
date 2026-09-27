// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside setup.cpp's private namespace: update and rollback share the
// installer's ownership, trust, file replacement and removal primitives.

constexpr auto PREVIOUS = L"SOFTWARE\\OpenZJS\\LaserJet1005Update\\Previous";
constexpr auto TARGET = L"SOFTWARE\\OpenZJS\\LaserJet1005Update\\Target";
constexpr auto SAVED_QUEUE = L"SOFTWARE\\OpenZJS\\LaserJet1005Update\\Queue";

fs::path UpdateRoot() {
    return InstallRoot().parent_path() / L"OpenZJS LaserJet 1005 Update";
}

bool UpdateInProgress() {
    Key key(UPDATE);
    if (!key.value)
        return false;
    if (ReadSetting(UPDATE, L"Owner") != OWNER)
        Fail(L"Unrecognized update record. No installation changes were made.");
    return ReadSetting(UPDATE, L"Phase") != L"Available";
}

void UpdatePhase(const wchar_t* phase) {
    WriteSetting(UPDATE, L"Phase", phase);
#ifdef OPENZJS_SETUP_TESTING
    // Only the separately built VM fault-injection executable contains this.
    // The harness terminates this process to test durable restart recovery.
    std::array<wchar_t, 128> stop{};
    if (GetEnvironmentVariableW(L"OPENZJS_SETUP_FAIL_AT", stop.data(), static_cast<DWORD>(stop.size())) &&
        wcscmp(stop.data(), phase) == 0)
        Fail(L"Injected VM-only update failure.");
    stop.fill(0);
    if (GetEnvironmentVariableW(L"OPENZJS_SETUP_STOP_AT", stop.data(), static_cast<DWORD>(stop.size())) &&
        wcscmp(stop.data(), phase) == 0)
        for (;;)
            Sleep(100);
#endif
}

void DeleteUpdateFile(const fs::path& file) {
    RejectLinks(file);
    DeleteRepairTemporary(file);
    if (fs::exists(file))
        Check(DeleteFileW(file.c_str()), L"Cannot clear the saved update package. Close Setup and try again.");
}

void DiscardUpdate() {
    Key key(UPDATE);
    if (!key.value)
        return;
    if (ReadSetting(UPDATE, L"Owner") != OWNER)
        Fail(L"Unrecognized update record. Saved files will not be deleted.");
    RejectLinks(UpdateRoot());
    const auto staged = ReadSetting(TARGET, L"InfPath");
    if (!staged.empty() && fs::exists(staged) &&
        _wcsicmp(staged.c_str(), ReadSetting(STATE, L"InfPath").c_str()) != 0) {
        if (fs::path(staged).filename() != L"lj1005.inf")
            Fail(L"Unrecognized staged update package.");
        const auto error = DeletePrinterDriverPackageW(nullptr, staged.c_str(), L"Windows x64");
        if (FAILED(error) && error != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
            Fail(L"Cannot remove the unused update package. Restart Windows and retry.", static_cast<DWORD>(error));
    }
    for (const auto folder : {L"previous", L"target"}) {
        const auto root = UpdateRoot() / folder;
        for (const auto name : PAYLOAD)
            DeleteUpdateFile(root / L"driver" / name);
        DeleteUpdateFile(root / L"Setup.exe");
        if (fs::exists(root / L"driver"))
            Check(RemoveDirectoryW((root / L"driver").c_str()), L"Unexpected files in the saved update package.");
        if (fs::exists(root))
            Check(RemoveDirectoryW(root.c_str()), L"Unexpected files in the saved update package.");
    }
    for (const auto name : {L"firmware.img", L"queue-mode.bin", L"queue-security.bin"})
        DeleteUpdateFile(UpdateRoot() / name);
    if (fs::exists(UpdateRoot()))
        Check(RemoveDirectoryW(UpdateRoot().c_str()), L"Unexpected files in the update directory.");
    const auto error = RegDeleteTreeW(HKEY_LOCAL_MACHINE, UPDATE);
    if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND)
        Fail(L"Cannot clear the update record.", error);
}

std::vector<BYTE> PrinterInfo(HANDLE printer, DWORD level) {
    DWORD needed = 0;
    GetPrinterW(printer, level, nullptr, 0, &needed);
    std::vector<BYTE> bytes(needed);
    Check(GetPrinterW(printer, level, bytes.data(), needed, &needed), L"Cannot save printer preferences.");
    return bytes;
}

void SaveQueue() {
    const auto printer = OpenOwnedQueue();
    try {
        const auto bytes = PrinterInfo(printer, 2);
        const auto& info = *reinterpret_cast<const PRINTER_INFO_2W*>(bytes.data());
        for (const auto& entry : {std::pair{L"Port", info.pPortName},
                                  {L"Comment", info.pComment},
                                  {L"Location", info.pLocation},
                                  {L"ShareName", info.pShareName},
                                  {L"PrintProcessor", info.pPrintProcessor},
                                  {L"Datatype", info.pDatatype}})
            WriteSetting(SAVED_QUEUE, entry.first, entry.second ? entry.second : L"");
        for (const auto& entry : {std::pair{L"Attributes", info.Attributes},
                                  {L"Priority", info.Priority},
                                  {L"DefaultPriority", info.DefaultPriority},
                                  {L"StartTime", info.StartTime},
                                  {L"UntilTime", info.UntilTime}})
            WriteSetting(SAVED_QUEUE, entry.first, std::to_wstring(entry.second));
        const auto modeBytes = PrinterInfo(printer, 8);
        const auto mode = reinterpret_cast<const PRINTER_INFO_8W*>(modeBytes.data())->pDevMode;
        if (!mode || mode->dmSize < sizeof(DEVMODEW))
            Fail(L"Cannot save this printer's default settings.");
        const auto begin = reinterpret_cast<const BYTE*>(mode);
        const std::vector<BYTE> savedMode(begin, begin + mode->dmSize + mode->dmDriverExtra);
        WriteNew(UpdateRoot() / L"queue-mode.bin", savedMode);
        WriteSetting(SAVED_QUEUE, L"ModeHash", Hash(savedMode));
        const auto securityBytes = PrinterInfo(printer, 3);
        const auto security = reinterpret_cast<const PRINTER_INFO_3*>(securityBytes.data())->pSecurityDescriptor;
        if (!security || !IsValidSecurityDescriptor(security))
            Fail(L"Cannot save this printer's access permissions.");
        const auto sd = static_cast<const BYTE*>(security);
        const std::vector<BYTE> savedSecurity(sd, sd + GetSecurityDescriptorLength(security));
        WriteNew(UpdateRoot() / L"queue-security.bin", savedSecurity);
        WriteSetting(SAVED_QUEUE, L"SecurityHash", Hash(savedSecurity));
    }
    catch (...) {
        ClosePrinter(printer);
        throw;
    }
    ClosePrinter(printer);
}

void RestoreQueue() {
    auto mode = ReadBytes(UpdateRoot() / L"queue-mode.bin");
    auto security = ReadBytes(UpdateRoot() / L"queue-security.bin");
    if (Hash(mode) != ReadSetting(SAVED_QUEUE, L"ModeHash") || mode.size() < sizeof(DEVMODEW) ||
        Hash(security) != ReadSetting(SAVED_QUEUE, L"SecurityHash") || !IsValidSecurityDescriptor(security.data()))
        Fail(L"Saved printer settings are damaged. Recovery cannot continue.");
    const auto oldMode = reinterpret_cast<DEVMODEW*>(mode.data());
    if (oldMode->dmSize < sizeof(DEVMODEW) ||
        static_cast<std::size_t>(oldMode->dmSize) + oldMode->dmDriverExtra != mode.size())
        Fail(L"Saved printer settings have an invalid size.");
    const auto printer = OpenOwnedQueue();
    try {
        auto bytes = PrinterInfo(printer, 2);
        auto& info = *reinterpret_cast<PRINTER_INFO_2W*>(bytes.data());
        auto port = ReadSetting(SAVED_QUEUE, L"Port"), comment = ReadSetting(SAVED_QUEUE, L"Comment"),
             location = ReadSetting(SAVED_QUEUE, L"Location"), share = ReadSetting(SAVED_QUEUE, L"ShareName"),
             processor = ReadSetting(SAVED_QUEUE, L"PrintProcessor"), datatype = ReadSetting(SAVED_QUEUE, L"Datatype");
        info.pPortName = port.data();
        info.pComment = comment.data();
        info.pLocation = location.data();
        info.pShareName = share.empty() ? nullptr : share.data();
        info.pPrintProcessor = processor.data();
        info.pDatatype = datatype.data();
        info.Attributes = std::stoul(ReadSetting(SAVED_QUEUE, L"Attributes"));
        info.Priority = std::stoul(ReadSetting(SAVED_QUEUE, L"Priority"));
        info.DefaultPriority = std::stoul(ReadSetting(SAVED_QUEUE, L"DefaultPriority"));
        info.StartTime = std::stoul(ReadSetting(SAVED_QUEUE, L"StartTime"));
        info.UntilTime = std::stoul(ReadSetting(SAVED_QUEUE, L"UntilTime"));
        info.pSecurityDescriptor = nullptr;
        info.pDevMode = nullptr;
        Check(SetPrinterW(printer, 2, bytes.data(), 0), L"Cannot restore printer preferences.");
        const auto size = DocumentPropertiesW(nullptr, printer, const_cast<LPWSTR>(QUEUE), nullptr, nullptr, 0);
        if (size <= 0)
            Fail(L"Cannot convert saved paper settings for this driver.");
        std::vector<BYTE> converted(static_cast<std::size_t>(size));
        auto newMode = reinterpret_cast<DEVMODEW*>(converted.data());
        if (DocumentPropertiesW(nullptr, printer, const_cast<LPWSTR>(QUEUE), newMode, oldMode,
                                DM_IN_BUFFER | DM_OUT_BUFFER) != IDOK)
            Fail(L"Cannot restore saved paper settings.");
        PRINTER_INFO_8W global{newMode};
        Check(SetPrinterW(printer, 8, reinterpret_cast<BYTE*>(&global), 0), L"Cannot restore default paper settings.");
        PRINTER_INFO_3 permissions{security.data()};
        Check(SetPrinterW(printer, 3, reinterpret_cast<BYTE*>(&permissions), 0),
              L"Cannot restore printer access permissions.");
    }
    catch (...) {
        ClosePrinter(printer);
        throw;
    }
    ClosePrinter(printer);
}

fs::path StageUpdatePackage(const fs::path& root) {
    TrustedCatalog(root / L"driver" / L"lj1005.cat");
    std::array<wchar_t, 32768> published{}, stored{};
    Check(SetupCopyOEMInfW((root / L"driver" / L"lj1005.inf").c_str(), nullptr, SPOST_PATH, 0, published.data(),
                           static_cast<DWORD>(published.size()), nullptr, nullptr),
          L"Windows rejected the update package.");
    Check(SetupGetInfDriverStoreLocationW(published.data(), nullptr, nullptr, stored.data(),
                                          static_cast<DWORD>(stored.size()), nullptr),
          L"Cannot locate the update package.");
    return stored.data();
}

void ValidateUpdatePackage(const fs::path& root, const wchar_t* record) {
    RejectLinks(root);
    for (const auto name : PAYLOAD)
        if (!MatchesFile(root / L"driver" / name, ReadSetting(record, HashName(name).c_str())))
            Fail(L"A saved update package is damaged. No further changes were made.");
    if (!MatchesFile(root / L"Setup.exe", ReadSetting(record, L"Hash.Setup.exe")))
        Fail(L"The saved Setup is damaged. Recovery cannot continue.");
    TrustedCatalog(root / L"driver" / L"lj1005.cat");
}

void SaveUpdatePackage(const fs::path& source, const fs::path& root, const wchar_t* record, bool previous) {
    fs::create_directories(root / L"driver");
    for (const auto name : PAYLOAD) {
        const auto bytes = ReadBytes(source / L"driver" / name);
        if (previous && Hash(bytes) != ReadSetting(STATE, HashName(name).c_str()))
            Fail(L"The installed package is damaged. Repair it before upgrading.");
        WriteNew(root / L"driver" / name, bytes);
        WriteSetting(record, HashName(name).c_str(), Hash(bytes));
    }
    const auto setup = ReadBytes(previous ? source / L"Setup.exe" : ModulePath());
    if (previous && Hash(setup) != ReadSetting(STATE, L"Hash.Setup.exe"))
        Fail(L"The installed Setup is damaged. Repair it before upgrading.");
    WriteNew(root / L"Setup.exe", setup);
    WriteSetting(record, L"Hash.Setup.exe", Hash(setup));
    ValidateUpdatePackage(root, record);
}

void CheckUpdateOwnership() {
    CheckQueues();
    if (QueueExists()) {
        const auto printer = OpenOwnedQueue();
        ClosePrinter(printer);
    }
    const auto owner = ReadSetting(STATE, L"Owner");
    if (!owner.empty() && owner != OWNER)
        Fail(L"The installation ownership changed. Recovery stopped.");
}

bool FinishUpdate(bool rollback) {
    CheckUpdateOwnership();
    const auto root = UpdateRoot() / (rollback ? L"previous" : L"target");
    ValidateUpdatePackage(root, rollback ? PREVIOUS : TARGET);
    if (!rollback)
        ValidateUpdatePackage(UpdateRoot() / L"previous", PREVIOUS);
    for (const auto& entry : {std::pair{L"queue-mode.bin", L"ModeHash"}, {L"queue-security.bin", L"SecurityHash"}})
        if (!MatchesFile(UpdateRoot() / entry.first, ReadSetting(SAVED_QUEUE, entry.second)))
            Fail(L"Saved printer settings are damaged. Recovery cannot continue.");
    Firmware(UpdateRoot() / L"firmware.img");
    const bool offline = ReadSetting(UPDATE, L"Offline") == L"1";
    auto phase = ReadSetting(UPDATE, L"Phase");
    const auto removing = rollback ? L"RollbackRemoving" : L"UpgradeRemoving";
    const auto installing = rollback ? L"RollbackInstalling" : L"UpgradeInstalling";
    const auto restoring = rollback ? L"RollbackRestoringQueue" : L"UpgradeRestoringQueue";
    if (phase == removing) {
        if (ReadSetting(STATE, L"Owner") == OWNER) {
            // Pending deletes belong to the old installation: never install new
            // files at those paths until Windows has finished the restart.
            if (ReadSetting(STATE, L"Status") == L"RebootPending" &&
                (fs::exists(InstallRoot() / L"Setup.exe") || fs::exists(SystemPath(L"lj1005monitor.dll"))))
                return true;
            RemoveOwned(true);
            if (ReadSetting(STATE, L"Status") == L"RebootPending")
                return true;
        }
        UpdatePhase(installing);
        phase = installing;
    }
    if (phase == installing) {
        if (ReadSetting(STATE, L"Owner") == OWNER) {
            // A retry after process termination starts from a clean owned removal.
            UpdatePhase(removing);
            return FinishUpdate(rollback);
        }
        Install(UpdateRoot() / L"firmware.img", offline, root);
        UpdatePhase(restoring);
        phase = restoring;
    }
    if (phase != restoring)
        Fail(L"The update recovery record has an unknown phase.");
    RestoreQueue();
    if (rollback) {
        // Persist completion before deleting a backup so cleanup is retryable.
        UpdatePhase(L"RollbackComplete");
        DiscardUpdate();
    }
    else
        UpdatePhase(L"Available");
    return false;
}

bool RollbackUpdate() {
    Preflight();
    if (ReadSetting(UPDATE, L"Owner") != OWNER)
        Fail(L"There is no saved upgrade to roll back.");
    RejectLinks(UpdateRoot());
    const auto phase = ReadSetting(UPDATE, L"Phase");
    if (phase.empty() || phase == L"Preparing" || phase == L"RollbackComplete") {
        DiscardUpdate();
        return false;
    }
    CheckUpdateOwnership();
    ValidateUpdatePackage(UpdateRoot() / L"previous", PREVIOUS);
    if (phase != L"RollbackRemoving" && phase != L"RollbackInstalling" && phase != L"RollbackRestoringQueue")
        UpdatePhase(L"RollbackRemoving");
    return FinishUpdate(true);
}

bool ContinueUpgrade() {
    try {
        return FinishUpdate(false);
    }
    catch (...) {
        const auto failedPhase = ReadSetting(UPDATE, L"Phase");
        if (failedPhase == L"UpgradeInstalling" || failedPhase == L"UpgradeRestoringQueue") {
            bool pending = false;
            try {
                pending = RollbackUpdate();
            }
            catch (...) {
                Fail(
                    L"Upgrade failed and recovery is unfinished. Restart Windows, then choose Roll back. The saved previous package was preserved.");
            }
            if (pending)
                Fail(L"Upgrade failed. Restart Windows, then choose Roll back to restore the previous build.");
            Fail(L"Upgrade failed. The previous build and printer settings were restored.");
        }
        throw;
    }
}

std::uint64_t UpdateVersion(const fs::path& inf) {
    const auto text = PackageVersion(inf);
    std::uint64_t result = 0;
    std::size_t begin = 0;
    for (int field = 0; field < 4; ++field) {
        const auto end = text.find(L'.', begin);
        const auto part = text.substr(begin, end == std::wstring::npos ? end : end - begin);
        if (part.empty() || part.size() > 5 || (field < 3) != (end != std::wstring::npos))
            Fail(L"The driver version must contain four numeric fields.");
        for (const auto ch : part)
            if (ch < L'0' || ch > L'9')
                Fail(L"The driver version contains an invalid field.");
        const auto value = std::stoul(part);
        if (value > 65535)
            Fail(L"The driver version field is too large.");
        result = (result << 16) | value;
        begin = end == std::wstring::npos ? text.size() : end + 1;
    }
    return result;
}

bool UpgradeOwned(const fs::path& firmwareSource, bool offline) {
    Preflight();
    RejectLinks(InstallRoot());
    RejectLinks(UpdateRoot());
    const auto phase = ReadSetting(UPDATE, L"Phase");
    if (UpdateInProgress() && phase != L"Preparing") {
        if (phase == L"UpgradeRemoving")
            return ContinueUpgrade();
        // An interrupted installation is rolled back before another attempt.
        return RollbackUpdate();
    }
    if (ReadSetting(STATE, L"Owner") != OWNER || ReadSetting(STATE, L"Status") != L"Complete" ||
        ReadSetting(STATE, L"RepairSchema") != L"1")
        Fail(L"Upgrade requires a complete owned installation with repair records. Repair it first.");
    CheckUpdateOwnership();
    if (!QueueExists())
        Fail(L"Repair the missing printer queue before upgrading.");
    const auto firmware = Firmware(firmwareSource);
    if (UpdateVersion(ModulePath().parent_path() / L"driver" / L"lj1005.inf") <
        UpdateVersion(InstallRoot() / L"driver" / L"lj1005.inf"))
        Fail(
            L"This package is older than the installed version. Choose Roll back to restore the saved previous build.");
    if (!offline)
        UsbPort();
    const auto stored = fs::path(ReadSetting(STATE, L"InfPath"));
    if (!CheckRegisteredDriver(stored, DriverDirectory()))
        Fail(L"Repair the missing driver before upgrading.");
    for (const auto name : PAYLOAD)
        if (!MatchesFile(InstallRoot() / L"driver" / name, ReadSetting(STATE, HashName(name).c_str())) ||
            !MatchesFile(stored.parent_path() / name, ReadSetting(STATE, HashName(name).c_str())))
            Fail(L"The installed package is damaged. Repair it before upgrading.");
    if (!MatchesFile(SystemPath(L"lj1005monitor.dll"), ReadSetting(STATE, L"MonitorHash")) ||
        !MatchesFile(SystemPath(L"lj1005-firmware.img"), FW_HASH))
        Fail(L"The installed monitor or firmware is damaged. Repair it before upgrading.");
    bool same = Hash(ReadBytes(ModulePath())) == ReadSetting(STATE, L"Hash.Setup.exe");
    for (const auto name : PAYLOAD)
        same = same && Hash(ReadBytes(ModulePath().parent_path() / L"driver" / name)) ==
                           ReadSetting(STATE, HashName(name).c_str());
    if (same)
        Fail(L"This build is already installed. Choose Install or repair instead.");
    if (phase == L"Available" || phase == L"Preparing")
        DiscardUpdate();
    else if (fs::exists(UpdateRoot()))
        Fail(L"An unrecognized update directory exists. It will not be overwritten.");
    WriteSetting(UPDATE, L"Owner", OWNER);
    UpdatePhase(L"Preparing");
    WriteSetting(UPDATE, L"Offline", offline ? L"1" : L"0");
    fs::create_directories(UpdateRoot());
    SaveQueue();
    WriteNew(UpdateRoot() / L"firmware.img", firmware);
    SaveUpdatePackage(InstallRoot(), UpdateRoot() / L"previous", PREVIOUS, true);
    WriteSetting(PREVIOUS, L"InfPath", stored.wstring());
    SaveUpdatePackage(ModulePath().parent_path(), UpdateRoot() / L"target", TARGET, false);
    // Windows checks catalog membership before the working installation is removed.
    const auto targetInf = StageUpdatePackage(UpdateRoot() / L"target");
    WriteSetting(TARGET, L"InfPath", targetInf.wstring());
    UpdatePhase(L"UpgradeRemoving");
    return ContinueUpgrade();
}

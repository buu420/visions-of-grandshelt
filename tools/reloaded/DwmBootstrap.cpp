#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;

struct DwmExport {
    const char* name;
    WORD ordinal;
};

constexpr DwmExport exports[]{
#include "DwmApiExports.inc"
};

HMODULE proxy_module{};

HMODULE system_dwmapi() {
    wchar_t system[MAX_PATH]{};
    const auto length = GetSystemDirectoryW(system, static_cast<UINT>(std::size(system)));
    if (!length || length >= std::size(system)) return nullptr;
    const auto path = fs::path(system) / L"dwmapi.dll";
    const auto module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module == proxy_module ? nullptr : module;
}

fs::path module_path(HMODULE module) {
    wchar_t file[32768]{};
    const auto length = GetModuleFileNameW(module, file, static_cast<DWORD>(std::size(file)));
    if (!length || length >= std::size(file)) return {};
    return file;
}

std::uintmax_t log_length(const fs::path& path) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    return error ? 0 : size;
}

void wait_for_reader(const fs::path& log, std::uintmax_t offset) {
    const auto deadline = GetTickCount64() + 90000;
    do {
        std::ifstream file(log, std::ios::binary);
        if (file) {
            file.seekg(static_cast<std::streamoff>(offset));
            std::string line;
            while (std::getline(file, line)) {
                if (line.find("HOOK native Unreal bindings verified on the game thread") != std::string::npos)
                    return;
                if (line.find("ERROR unsupported game executable") != std::string::npos)
                    throw std::runtime_error("This Steam game build needs updated accessibility bindings.");
                if (line.find(" ERROR ") != std::string::npos)
                    throw std::runtime_error("The native reader could not start. See ResonanceAccess.log.");
            }
        }
        Sleep(200);
    } while (GetTickCount64() < deadline);
    throw std::runtime_error("Resonance Access did not become ready. See ResonanceAccess.log.");
}

DWORD WINAPI start_reloaded(void*) {
    fs::path log;
    try {
        const auto executable = module_path(nullptr);
        const auto proxy = module_path(proxy_module);
        const auto bin = proxy.parent_path();
        if (bin.empty() || _wcsicmp(executable.filename().c_str(), L"FFRS-Win64-Shipping.exe") != 0 ||
            _wcsicmp(executable.parent_path().c_str(), bin.c_str()) != 0) return 0;

        const auto reloaded = bin.parent_path().parent_path().parent_path() / L"ResonanceAccess" / L"Reloaded-II";
        log = reloaded / L"ResonanceReloaded.log";
        const auto host_path = reloaded / L"Loader" / L"X64" / L"ResonanceReloadedHost.dll";
        const auto host = LoadLibraryExW(host_path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!host) throw std::runtime_error("Could not load the Resonance Reloaded host.");

        using Start = DWORD (WINAPI*)(void*);
        const auto start = reinterpret_cast<Start>(GetProcAddress(host, "StartResonanceReloaded"));
        if (!start) throw std::runtime_error("The Resonance Reloaded host has no startup export.");
        const auto reader_log = reloaded / L"Mods" / L"Resonance.Access" / L"ResonanceAccess.log";
        const auto offset = log_length(reader_log);
        if (start(nullptr) != 0)
            throw std::runtime_error("Reloaded could not start Resonance Access. See ResonanceReloaded.log.");
        wait_for_reader(reader_log, offset);
        return 0;
    } catch (const std::exception& error) {
        if (!log.empty()) std::ofstream(log, std::ios::app) << error.what() << '\n';
        MessageBoxA(nullptr, error.what(), "Resonance Access", MB_ICONERROR | MB_OK);
        return 1;
    }
}
}

extern "C" {
    FARPROC DwmForwardSlots[std::size(exports)]{};

    FARPROC ResolveDwmExport(unsigned index) {
        try {
            static const auto original = system_dwmapi();
            if (!original || index >= std::size(exports)) return nullptr;
            const auto& entry = exports[index];
            const auto address = GetProcAddress(original, entry.name ? entry.name : MAKEINTRESOURCEA(entry.ordinal));
            if (address) InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&DwmForwardSlots[index]),
                reinterpret_cast<PVOID>(address));
            return address;
        } catch (const std::exception&) {
            return nullptr;
        }
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        proxy_module = instance;
        DisableThreadLibraryCalls(instance);
        // The worker can run after Windows finishes DLL initialization.
        // DllMain does not wait or initialize the managed runtime.
        if (const auto thread = CreateThread(nullptr, 0, start_reloaded, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}

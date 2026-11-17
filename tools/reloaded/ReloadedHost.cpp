#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <tuple>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
namespace fs = std::filesystem;

using InitializeHost = int(__cdecl*)(const wchar_t*, const void*, void**);
using GetHostDelegate = int(__cdecl*)(void*, int, void**);
using CloseHost = int(__cdecl*)(void*);
using LoadAssembly = int(__stdcall*)(const wchar_t*, const wchar_t*, const wchar_t*,
    const wchar_t*, void*, void**);
using StartManaged = int(__stdcall*)(const wchar_t*);

fs::path program_files() {
    wchar_t* path{};
    if (SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &path) != S_OK)
        throw std::runtime_error("Program Files is unavailable.");
    fs::path result(path);
    CoTaskMemFree(path);
    return result;
}

fs::path hostfxr_path() {
    const auto versions = program_files() / L"dotnet" / L"host" / L"fxr";
    std::tuple<unsigned long, unsigned long> latest{};
    fs::path selected;
    for (const auto& entry : fs::directory_iterator(versions)) {
        if (!entry.is_directory()) continue;
        const auto name = entry.path().filename().wstring();
        if (!name.starts_with(L"9.")) continue;
        wchar_t* end{};
        const auto minor = std::wcstoul(name.c_str() + 2, &end, 10);
        if (*end != L'.' || end == name.c_str() + 2) continue;
        const auto* patch_start = end + 1;
        const auto patch = std::wcstoul(patch_start, &end, 10);
        if (*end || end == patch_start) continue;
        const auto version = std::tuple{minor, patch};
        const auto candidate = entry.path() / L"hostfxr.dll";
        if ((selected.empty() || version > latest) && fs::is_regular_file(candidate)) {
            latest = version;
            selected = candidate;
        }
    }
    if (selected.empty()) throw std::runtime_error("The x64 .NET 9 Desktop Runtime is unavailable.");
    return selected;
}

template<class T>
T symbol(HMODULE module, const char* name) {
    const auto address = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!address) throw std::runtime_error(std::string("Missing .NET hosting export ") + name);
    return address;
}

int start(const fs::path& folder, const fs::path& root) {
    const auto fxr = hostfxr_path();
    const auto module = LoadLibraryExW(fxr.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) throw std::runtime_error("The x64 .NET host could not be loaded.");
    const auto initialize = symbol<InitializeHost>(module, "hostfxr_initialize_for_runtime_config");
    const auto get_delegate = symbol<GetHostDelegate>(module, "hostfxr_get_runtime_delegate");
    const auto close = symbol<CloseHost>(module, "hostfxr_close");

    void* context{};
    const auto configuration = folder / L"Reloaded.Mod.Loader.runtimeconfig.json";
    const int initialized = initialize(configuration.c_str(), nullptr, &context);
    if (initialized < 0 || !context)
        throw std::runtime_error(".NET 9 could not open the Reloaded runtime configuration.");

    LoadAssembly load{};
    constexpr int load_assembly_and_get_function_pointer = 5;
    const auto delegate_result = get_delegate(context, load_assembly_and_get_function_pointer,
        reinterpret_cast<void**>(&load));
    close(context);
    if (delegate_result < 0 || !load)
        throw std::runtime_error(".NET could not provide the portable assembly loader.");

    StartManaged entry{};
    const auto assembly = folder / L"Resonance.Reloaded.dll";
    const auto unmanaged = reinterpret_cast<const wchar_t*>(static_cast<std::intptr_t>(-1));
    const auto loaded = load(assembly.c_str(), L"Resonance.Reloaded.Entry, Resonance.Reloaded",
        L"Start", unmanaged, nullptr, reinterpret_cast<void**>(&entry));
    if (loaded < 0 || !entry)
        throw std::runtime_error("The Resonance Reloaded entry could not be loaded.");
    return entry(root.c_str());
}
}

extern "C" __declspec(dllexport) DWORD WINAPI StartResonanceReloaded(void*) {
    wchar_t module_path[32768]{};
    const auto length = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase),
        module_path, static_cast<DWORD>(std::size(module_path)));
    if (!length || length >= std::size(module_path)) return 1;
    const auto folder = fs::path(module_path).parent_path();
    const auto root = folder.parent_path().parent_path();
    try {
        return static_cast<DWORD>(start(folder, root));
    } catch (const std::exception& error) {
        std::ofstream(root / L"ResonanceReloaded.log", std::ios::app) << error.what() << '\n';
        return 1;
    }
}

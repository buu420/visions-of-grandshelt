# Build Visions of Grandshelt

Use Visual Studio 2022 Build Tools with the x64 C++ toolchain and MASM, the Windows SDK, Git and CMake 3.25 or newer. Install a .NET SDK capable of targeting .NET 9 to build the Reloaded managed adapter.

Obtain the headers from [Prism v0.18.3](https://github.com/ethindp/prism/releases/tag/v0.18.3) and the source from [MinHook v1.3.4](https://github.com/TsudaKageyu/minhook/tree/c3fcafdc10146beb5919319d0683e44e3c30d537). Set the two SDK paths to their extracted roots.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DPRISM_SDK="C:/Deps/prism-0.18.3" -DMINHOOK_SDK="C:/Deps/minhook-1.3.4"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
dotnet build tools/reloaded/Resonance.Reloaded.csproj -c Release -o build/reloaded-managed -p:ReloadedSdk="C:/Deps/Reloaded/Loader/X64"
```

Use the matching Reloaded 1.30.1 assemblies listed in `config/reloaded-runtime.json` for `ReloadedSdk`. The Windows release includes the verified game-local loader and managed adapter. The native reader expects the executable identity in [build-support.json](build-support.json) and verifies native class and property metadata before capturing game state.

The generated property descriptors and the baked spatial audio banks are included for building. To build the portable core and policy tests without the Windows reader, configure with `-DBUILD_NATIVE_MOD=OFF`.

using System.Runtime.InteropServices;
using Reloaded.Mod.Loader.IO.Config;
using Reloaded.Mod.Loader.Mods.Structs;

namespace Resonance.Reloaded;

public static class Entry
{
    private const string ModId = "Resonance.Access";
    private static NativeMod? resident;

    [UnmanagedCallersOnly]
    public static int Start(nint rootAddress)
    {
        string? log = null;
        try
        {
            string root = Marshal.PtrToStringUni(rootAddress)
                ?? throw new ArgumentException("The portable Reloaded directory is missing.");
            log = Path.Combine(root, "ResonanceReloaded.log");
            if (resident is not null)
                return 0;

            string executable = Environment.ProcessPath
                ?? throw new InvalidOperationException("The game executable could not be identified.");
            if (!string.Equals(Path.GetFileName(executable), "FFRS-Win64-Shipping.exe",
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("The Reloaded host is outside the Resonance game process.");

            string mods = Path.Combine(root, "Mods");
            var entry = ModConfig.GetAllMods(mods).Single(item => item.Config.ModId == ModId);
            string native = Path.GetFullPath(entry.Config.GetDllPath(entry.Path));
            string expected = Path.Combine(mods, ModId, "ResonanceAccess.dll");
            if (!string.Equals(native, expected, StringComparison.OrdinalIgnoreCase) || !File.Exists(native))
                throw new InvalidOperationException("The native mod does not match the portable profile.");

            // Use the bundled Reloaded loader's native mod adapter. This one
            // profile needs only native DLL loading and its ReloadedStart callback.
            var mod = new NativeMod(native);
            mod.Start(null!);
            resident = mod;
            File.AppendAllText(log, "Reloaded native mod entry point called.\n");
            return 0;
        }
        catch (Exception error)
        {
            try
            {
                if (log is not null) File.AppendAllText(log, error + "\n");
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            return 1;
        }
    }
}

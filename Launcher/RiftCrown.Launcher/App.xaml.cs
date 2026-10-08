using System.Windows;
using System.IO;
using RiftCrown.UpdateCore;

namespace RiftCrown.Launcher;

public partial class App : Application
{
    private Mutex? instanceMutex;
    protected override async void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        var arguments = e.Args.ToList();
        var playSmoke = arguments.Contains("--self-test-play");
        var selfTest = arguments.Contains("--self-test") || playSmoke;
        string? Argument(string name) { var index = arguments.IndexOf(name); return index >= 0 && index + 1 < arguments.Count ? arguments[index + 1] : null; }
        var saveRoot = Argument("--save-root") ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RiftCrownArena");
        var binaryRoot = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, ".."));
        var installRoot = Argument("--install-root") ?? (new DirectoryInfo(AppContext.BaseDirectory).Name.Equals("Launcher", StringComparison.OrdinalIgnoreCase) ? binaryRoot : AppContext.BaseDirectory);
        try
        {
            if (selfTest)
            {
                if (Argument("--save-root") is null || Argument("--install-root") is null) throw new InvalidOperationException("Self-test requires isolated --save-root and --install-root folders.");
                using var manager = new ReleaseManager(installRoot, saveRoot);
                await manager.RecoverAsync();
                var settings = new LauncherSettings();
                if (ReleaseJson.Read<LauncherSettings>(ReleaseJson.Write(settings)) != settings) throw new InvalidDataException("Settings JSON round-trip failed.");
                var window = new MainWindow(installRoot, saveRoot);
                if (playSmoke)
                {
                    await window.ValidatePlayAsync();
                    window.Close(); Shutdown(0); return;
                }
                var commandCount = window.ValidateBindings(saveRoot);
                window.Close();
                SafeFiles.AtomicWrite(Path.Combine(saveRoot, "launcher-self-test.json"), ReleaseJson.Write(new { passed = true, version = ReleaseManager.LauncherVersion, selfContained = true, commands = commandCount, resources = "WPF resources loaded; settings routed commands exercised; preview rendered", installed = manager.Installed()?.Version, utc = DateTime.UtcNow }));
                Shutdown(0); return;
            }
            var identity = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(Path.GetFullPath(installRoot).ToUpperInvariant())))[..24];
            instanceMutex = new Mutex(true, "Local\\RiftCrownLauncher-" + identity, out var created);
            if (!created) { MessageBox.Show("Rift Crown Launcher is already open for this installation.", "Rift Crown Arena"); Shutdown(0); return; }
            DispatcherUnhandledException += (_, error) =>
            {
                new LauncherLog(saveRoot, "Launcher").Write("Recoverable UI error.", error.Exception);
                error.Handled = true;
                MessageBox.Show(error.Exception.Message + "\nOpen Logs for details.", "Rift Crown Arena", MessageBoxButton.OK, MessageBoxImage.Warning);
            };
            var main = new MainWindow(installRoot, saveRoot); MainWindow = main; main.Show();
        }
        catch (Exception error)
        {
            try { new LauncherLog(saveRoot, "Launcher").Write("Launcher startup failed.", error); } catch (Exception) { }
            if (selfTest)
            {
                try { SafeFiles.AtomicWrite(Path.Combine(saveRoot, playSmoke ? "launcher-play-self-test.json" : "launcher-self-test.json"), ReleaseJson.Write(new { passed = false, error = error.ToString() })); } catch (Exception) { }
            }
            else MessageBox.Show(error.Message, "Rift Crown Arena", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
        }
    }
    protected override void OnExit(ExitEventArgs e) { instanceMutex?.Dispose(); base.OnExit(e); }
}

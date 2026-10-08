using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Imaging;
using RiftCrown.UpdateCore;

namespace RiftCrown.Launcher;

public partial class MainWindow
{
    // Explicit isolated QA entry point; invokes the same routed Play event as the visible control.
    public async Task ValidatePlayAsync()
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(150));
        var token = timeout.Token;
        await manager.RecoverAsync(token);
        RefreshInstallation();
        var release = installed ?? throw new InvalidDataException("An actual bundled native game is required for the Play smoke check.");
        if (!PlayButton.IsEnabled) throw new InvalidOperationException("The real Play control is unavailable. Close any running game first.");
        var nativeEntry = release.Manifest.Files.Single(file => file.Path is "RiftCrownArena/Binaries/Win64/RiftCrownArena-Win64-Shipping.exe" or "RiftCrownArena/Binaries/Win64/RiftCrownArena.exe");
        var nativeExecutable = SafeFiles.Inside(manager.ReleaseFolder(release), nativeEntry.Path);
        var capture = Path.Combine(saveRoot, "launcher-play-home.png");
        var engineUser = Path.Combine(saveRoot, "EngineUser");
        if (File.Exists(capture)) throw new InvalidDataException("Play smoke requires a fresh capture destination.");
        if (capture.Contains('"') || engineUser.Contains('"')) throw new InvalidDataException("The QA paths contain an invalid quote.");
        var verification = await manager.VerifyAsync(release, token: token);
        if (!verification.IsHealthy) throw new InvalidDataException("The actual installed game inventory is damaged.");
        settings = settings with
        {
            CheckOnStartup = false,
            LaunchArguments = $"/Game/Rift/Maps/Arena -RenderOffscreen -windowed -ResX=1280 -ResY=720 -UserDir=\"{engineUser}\" -RiftCapture=\"{capture}\" -RiftCapturePage=Home -RiftCaptureDelay=6 -RiftQuitAfterCapture -unattended -nosplash -NoSound -NoVSync"
        };
        Process? nativeProcess = null;
        var began = DateTime.UtcNow;
        try
        {
            playCommandOperation = null;
            PlayButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            var command = playCommandOperation ?? throw new InvalidOperationException("The routed Play control did not invoke its operation.");
            await command.WaitAsync(token);
            var bootstrap = launchedGame ?? throw new IOException("The real Play operation did not start a game: " + Status.Text);
            var bootstrapId = bootstrap.Id;
            var discovery = Stopwatch.StartNew();
            while (nativeProcess is null && discovery.Elapsed < TimeSpan.FromSeconds(20))
            {
                token.ThrowIfCancellationRequested();
                foreach (var candidate in Process.GetProcessesByName(Path.GetFileNameWithoutExtension(nativeExecutable)))
                {
                    try
                    {
                        if (!candidate.HasExited && candidate.StartTime.ToUniversalTime() >= began.AddSeconds(-1) &&
                            string.Equals(Path.GetFullPath(candidate.MainModule?.FileName ?? ""), nativeExecutable, StringComparison.OrdinalIgnoreCase))
                        {
                            nativeProcess = candidate; break;
                        }
                    }
                    catch (Exception error) when (error is Win32Exception or InvalidOperationException or ArgumentException) { }
                    candidate.Dispose();
                }
                if (nativeProcess is null) await Task.Delay(50, token);
            }
            if (nativeProcess is null) throw new IOException("The actual bundled native child did not start through the launcher bootstrap.");
            var nativeId = nativeProcess.Id;
            await nativeProcess.WaitForExitAsync(token);
            if (nativeProcess.ExitCode != 0) throw new IOException($"The actual native game exited with {nativeProcess.ExitCode}.");
            await bootstrap.WaitForExitAsync(token);
            if (bootstrap.ExitCode != 0) throw new IOException($"The packaged game bootstrap exited with {bootstrap.ExitCode}.");
            if (!File.Exists(capture)) throw new IOException("The game started by Play did not write its native renderer capture.");
            int width, height;
            using (var imageStream = File.OpenRead(capture))
            {
                var image = new PngBitmapDecoder(imageStream, BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad);
                width = image.Frames[0].PixelWidth; height = image.Frames[0].PixelHeight;
            }
            if (width != 1280 || height != 720) throw new InvalidDataException("The actual Play capture has an unexpected resolution.");
            var contexts = new List<JsonElement>();
            foreach (var filename in Directory.EnumerateFiles(Path.Combine(saveRoot, "Logs"), "RiftGame-*.log"))
            {
                foreach (var line in File.ReadLines(filename))
                {
                    using var document = JsonDocument.Parse(line);
                    var entry = document.RootElement;
                    if (entry.TryGetProperty("level", out var level) && level.GetString() is "Error" or "Fatal")
                        throw new InvalidDataException("The game launched by Play recorded an error: " + entry.GetProperty("message").GetString());
                    if (entry.TryGetProperty("event", out var kind) && kind.GetString() == "system_context" && entry.GetProperty("processId").GetInt32() == nativeId)
                    {
                        if (entry.GetProperty("build").GetString() != "Shipping" ||
                            !string.Equals(Path.GetFullPath(entry.GetProperty("saveRoot").GetString()!).TrimEnd('\\','/'), saveRoot.TrimEnd('\\','/'), StringComparison.OrdinalIgnoreCase))
                            throw new InvalidDataException("The launcher-started game did not use the expected Shipping build and isolated environment save root.");
                        contexts.Add(entry.Clone());
                    }
                }
            }
            if (contexts.Count == 0) throw new InvalidDataException("No matching Shipping diagnostics were emitted by the actual Play child.");
            if (!File.Exists(Path.Combine(saveRoot, "ue_save.json"))) throw new InvalidDataException("The launcher-started native game did not save its isolated profile.");
            SafeFiles.AtomicWrite(Path.Combine(saveRoot, "launcher-play-self-test.json"), ReleaseJson.Write(new
            {
                passed = true, utc = DateTime.UtcNow, version = ReleaseManager.LauncherVersion, installed = release.Version,
                routedCommand = "PlayButton.Click", verifiedGameFiles = verification.Checked, bootstrapProcessId = bootstrapId,
                bootstrapExitCode = bootstrap.ExitCode, nativeProcessId = nativeId, nativeExitCode = nativeProcess.ExitCode,
                nativeExecutable, nativeSha256 = nativeEntry.Sha256, saveRoot, saveRootSource = "RIFT_SAVE_ROOT environment",
                engineUserRoot = engineUser, capture, width, height, contexts
            }));
        }
        finally
        {
            if (nativeProcess is not null)
            {
                if (!nativeProcess.HasExited) { nativeProcess.Kill(entireProcessTree: true); await nativeProcess.WaitForExitAsync(); }
                nativeProcess.Dispose();
            }
            if (launchedGame is not null && !launchedGame.HasExited) { launchedGame.Kill(entireProcessTree: true); await launchedGame.WaitForExitAsync(); }
        }
    }
}

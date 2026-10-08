using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using RiftCrown.UpdateCore;

namespace RiftCrown.Launcher;

public partial class MainWindow
{
    // Explicit isolated QA entry point: only the production Check event runs; no Install event runs.
    public async Task ValidateUpdateAsync(string? expectedManifestPath)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(45));
        var token = timeout.Token;
        var defaults = new LauncherSettings();
        if (File.Exists(settingsPath) || settings != defaults)
            throw new InvalidDataException("The public update smoke requires fresh isolated defaults without saved endpoint overrides.");
        var endpoint = new Uri(settings.ManifestUrl);
        if (endpoint.Scheme != Uri.UriSchemeHttps || endpoint.Host != "github.com" ||
            endpoint.AbsolutePath != "/nhicksenterprises2025-maker/aether-arena/releases/latest/download/update-manifest.json")
            throw new InvalidDataException("The actual default public GitHub latest-manifest endpoint is required.");
        RefreshInstallation();
        if (installed is not null) throw new InvalidDataException("The public update smoke requires an empty isolated game installation.");
        async Task<Dictionary<string, string>> InventoryAsync()
        {
            var inventory = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var file in Directory.EnumerateFiles(manager.GameRoot, "*", SearchOption.AllDirectories))
            {
                SafeFiles.EnsureNoLinks(manager.GameRoot, file);
                inventory.Add(Path.GetRelativePath(manager.GameRoot, file), await SafeFiles.HashAsync(file, token));
            }
            return inventory;
        }
        var before = await InventoryAsync();
        var clock = Stopwatch.StartNew();
        updateCheckCommandOperation = null;
        CheckButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        var command = updateCheckCommandOperation ?? throw new InvalidOperationException("The routed Check for Updates control did not invoke its operation.");
        await command.WaitAsync(token);
        var manifest = available ?? throw new IOException("The real public update check did not return a release: " + Status.Text);
        manager.ValidateManifest(manifest);
        var canonical = ReleaseJson.Write(manifest);
        if (expectedManifestPath is not null)
        {
            var expected = ReleaseJson.Read<ReleaseManifest>(await File.ReadAllTextAsync(expectedManifestPath, token));
            manager.ValidateManifest(expected);
            if (canonical != ReleaseJson.Write(expected))
                throw new InvalidDataException("The actual latest public release manifest differs from the expected final local manifest.");
        }
        var download = new Uri(manifest.DownloadUrl);
        var expectedArchive = $"/nhicksenterprises2025-maker/aether-arena/releases/download/v{manifest.Version}/RiftCrownArena-Windows-x64-{manifest.Version}.zip";
        if (download.Scheme != Uri.UriSchemeHttps || download.Host != "github.com" || download.AbsolutePath != expectedArchive)
            throw new InvalidDataException("The public latest manifest does not point to the expected version-pinned GitHub package.");
        if (string.IsNullOrWhiteSpace(manifest.PatchNotes) || PatchNotes.Text != manifest.PatchNotes || NotesVersion.Text != "Release " + manifest.Version ||
            !UpdateButton.IsEnabled || UpdateButton.Content as string != "INSTALL" || PlayButton.IsEnabled || RepairButton.IsEnabled)
            throw new InvalidDataException("The actual update control did not bind the public release, patch notes, and available Install action correctly.");
        var after = await InventoryAsync();
        if (before.Count != after.Count || before.Any(entry => !after.TryGetValue(entry.Key, out var hash) || hash != entry.Value) || manager.Installed() is not null)
            throw new InvalidDataException("Checking the public release changed the isolated game installation.");
        SafeFiles.AtomicWrite(Path.Combine(saveRoot, "launcher-update-self-test.json"), ReleaseJson.Write(new
        {
            passed = true, utc = DateTime.UtcNow, launcherVersion = ReleaseManager.LauncherVersion,
            launcherSha256 = await SafeFiles.HashAsync(Environment.ProcessPath ?? throw new IOException("The actual launcher process path is unavailable."), token),
            routedCommand = "CheckButton.Click", endpoint = settings.ManifestUrl, elapsedSeconds = clock.Elapsed.TotalSeconds,
            releaseVersion = manifest.Version, manifest.SchemaVersion, manifest.Platform, manifest.MinimumLauncherVersion,
            manifest.DownloadUrl, archiveSha256 = manifest.Sha256, archiveSize = manifest.Size,
            manifestCanonicalSha256 = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(canonical))).ToLowerInvariant(),
            gameFiles = manifest.Files.Count, inventoryBytes = manifest.Files.Sum(file => file.Size),
            patchNotesDisplayed = true, installAvailable = UpdateButton.IsEnabled,
            expectedFinalManifestMatched = expectedManifestPath is not null,
            gameInstallationUnchanged = true, gameDownloaded = false, gameInstalled = false, saveRoot,
            status = Status.Text
        }));
    }
}

using System.Diagnostics;
using System.IO.Compression;
using System.Net;

namespace RiftCrown.UpdateCore;

public sealed class ReleaseManager : IDisposable
{
    public const string LauncherVersion = "1.0.0";
    private readonly HttpClient http;
    private readonly bool allowLoopback;
    private readonly LauncherLog log;
    private readonly Func<bool> gameRunning;
    public string InstallRoot { get; }
    public string SaveRoot { get; }
    public string GameRoot => Path.Combine(InstallRoot, "Game");
    private string PointerPath => Path.Combine(GameRoot, "installed.json");
    private string PreviousPath => Path.Combine(GameRoot, "installed.previous.json");
    private string JournalPath => Path.Combine(GameRoot, "update-journal.json");

    public ReleaseManager(string installRoot, string saveRoot, bool allowLoopback = false, Func<bool>? gameRunning = null, HttpMessageHandler? handler = null)
    {
        InstallRoot = Path.GetFullPath(installRoot); SaveRoot = Path.GetFullPath(saveRoot);
        SafeFiles.DistinctRoots(InstallRoot, SaveRoot);
        SafeFiles.EnsureNoLinks(InstallRoot, Path.Combine(InstallRoot, "Game"));
        Directory.CreateDirectory(GameRoot);
        this.allowLoopback = allowLoopback;
        this.gameRunning = gameRunning ?? (() => false);
        log = new LauncherLog(SaveRoot, "Updater");
        http = handler is null ? new HttpClient(new SocketsHttpHandler { ConnectTimeout = TimeSpan.FromSeconds(20), AllowAutoRedirect = true }) : new HttpClient(handler);
        http.Timeout = Timeout.InfiniteTimeSpan;
        http.DefaultRequestHeaders.UserAgent.ParseAdd($"RiftCrownLauncher/{LauncherVersion}");
    }

    public static Version ParseVersion(string value)
    {
        if (string.IsNullOrWhiteSpace(value) || !System.Version.TryParse(value, out var version) || version.Major < 0 || version.Minor < 0 || version.Build < 0 || value.Contains('-') || value.Length > 40)
            throw new InvalidDataException("The release version must have three or four nonnegative numeric components.");
        return new Version(version.Major, version.Minor, version.Build, Math.Max(0, version.Revision));
    }

    private Uri ValidateUrl(string value)
    {
        if (!Uri.TryCreate(value, UriKind.Absolute, out var uri) || !string.IsNullOrEmpty(uri.UserInfo) || !string.IsNullOrEmpty(uri.Fragment) ||
            (uri.Scheme != Uri.UriSchemeHttps && !(allowLoopback && uri.Scheme == Uri.UriSchemeHttp && uri.IsLoopback)))
            throw new InvalidDataException("Update addresses must use HTTPS. Only isolated tests may use HTTP loopback.");
        return uri;
    }

    public void ValidateManifest(ReleaseManifest manifest)
    {
        if (manifest.SchemaVersion != 1 || manifest.Platform != "windows-x64") throw new InvalidDataException("Unsupported release manifest or platform.");
        ParseVersion(manifest.Version);
        if (ParseVersion(manifest.MinimumLauncherVersion) > ParseVersion(LauncherVersion)) throw new InvalidDataException($"This release requires launcher {manifest.MinimumLauncherVersion}. Install the newer launcher first.");
        ValidateUrl(manifest.DownloadUrl);
        if (manifest.Size <= 0 || manifest.Size > 16L * 1024 * 1024 * 1024 || !HashValid(manifest.Sha256)) throw new InvalidDataException("Invalid package size or SHA-256.");
        if (manifest.PatchNotes is null || manifest.PatchNotes.Length > 131072) throw new InvalidDataException("Patch notes exceed the supported size.");
        SafeFiles.RelativePath(manifest.Executable);
        if (!manifest.Executable.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("The game entry point must be an executable.");
        if (manifest.Files is null || manifest.Files.Count is < 1 or > 100000) throw new InvalidDataException("The release must contain a file inventory.");
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        long expanded = 0;
        foreach (var file in manifest.Files)
        {
            if (file is null || !paths.Add(SafeFiles.RelativePath(file.Path)) || file.Size < 0 || !HashValid(file.Sha256)) throw new InvalidDataException("Invalid or duplicate release file.");
            expanded = checked(expanded + file.Size);
            if (expanded > 64L * 1024 * 1024 * 1024) throw new InvalidDataException("The release is larger than the supported installation limit.");
        }
        if (!paths.Contains(manifest.Executable)) throw new InvalidDataException("The entry point is missing from the release inventory.");
        foreach (var path in paths)
            for (var separator = path.LastIndexOf('/'); separator > 0; separator = path.LastIndexOf('/', separator - 1))
                if (paths.Contains(path[..separator])) throw new InvalidDataException("A package file is also used as a directory.");
    }

    private static bool HashValid(string? value) => value is { Length: 64 } && value.All(Uri.IsHexDigit);

    public async Task<ReleaseManifest> CheckAsync(string endpoint, CancellationToken token = default)
    {
        var uri = ValidateUrl(endpoint);
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token);
        deadline.CancelAfter(TimeSpan.FromSeconds(30));
        using var response = await http.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, deadline.Token);
        response.EnsureSuccessStatusCode();
        if (response.RequestMessage?.RequestUri is Uri destination) ValidateUrl(destination.AbsoluteUri);
        if (response.Content.Headers.ContentLength > 32 * 1024 * 1024) throw new InvalidDataException("The manifest is too large.");
        await using var stream = await response.Content.ReadAsStreamAsync(deadline.Token);
        using var output = new MemoryStream();
        var buffer = new byte[65536];
        int read;
        while ((read = await stream.ReadAsync(buffer, deadline.Token)) > 0)
        {
            if (output.Length + read > 32 * 1024 * 1024) throw new InvalidDataException("The manifest is too large.");
            output.Write(buffer, 0, read);
        }
        var manifest = ReleaseJson.Read<ReleaseManifest>(System.Text.Encoding.UTF8.GetString(output.ToArray()));
        ValidateManifest(manifest);
        log.Write($"Checked release {manifest.Version} ({manifest.Size} bytes).");
        return manifest;
    }

    public InstalledRelease? Installed()
    {
        if (!File.Exists(PointerPath)) return null;
        var pointer = ReleaseJson.Read<InstalledRelease>(File.ReadAllText(PointerPath));
        ValidatePointer(pointer);
        return pointer;
    }

    private void ValidatePointer(InstalledRelease pointer)
    {
        if (pointer.Manifest is null || pointer.ReleasePath is null || pointer.SchemaVersion != 1 || pointer.Version != pointer.Manifest.Version || pointer.Executable != pointer.Manifest.Executable || !pointer.ReleasePath.StartsWith("releases/", StringComparison.Ordinal))
            throw new InvalidDataException("The installed release record is invalid.");
        ValidateManifest(pointer.Manifest);
        if (pointer.ReleasePath.Split('/').Length != 2) throw new InvalidDataException("The release folder is invalid.");
        SafeFiles.Inside(GameRoot, pointer.ReleasePath);
    }

    public string ReleaseFolder(InstalledRelease release)
    {
        ValidatePointer(release); return SafeFiles.Inside(GameRoot, release.ReleasePath);
    }

    public string ExecutablePath(InstalledRelease release)
    {
        return SafeFiles.Inside(ReleaseFolder(release), release.Executable);
    }

    public async Task<RepairResult> VerifyAsync(InstalledRelease release, IProgress<UpdateProgress>? progress = null, CancellationToken token = default)
        => await VerifyFolderAsync(ReleaseFolder(release), release.Manifest, progress, token);

    private static async Task<RepairResult> VerifyFolderAsync(string folder, ReleaseManifest manifest, IProgress<UpdateProgress>? progress, CancellationToken token)
    {
        var damaged = new List<string>();
        var count = 0;
        foreach (var file in manifest.Files)
        {
            token.ThrowIfCancellationRequested();
            var path = SafeFiles.Inside(folder, file.Path);
            try
            {
                if (!File.Exists(path) || new FileInfo(path).Length != file.Size || !string.Equals(await SafeFiles.HashAsync(path, token), file.Sha256, StringComparison.OrdinalIgnoreCase)) damaged.Add(file.Path);
            }
            catch (IOException) { damaged.Add(file.Path); }
            catch (UnauthorizedAccessException) { damaged.Add(file.Path); }
            count++;
            progress?.Report(new("Verifying files", count, manifest.Files.Count, file.Path));
        }
        return new(count, damaged);
    }

    public async Task<string> ValidateEntryPointAsync(InstalledRelease release, CancellationToken token = default)
    {
        var path = ExecutablePath(release);
        var entry = release.Manifest.Files.Single(f => string.Equals(f.Path, release.Executable, StringComparison.OrdinalIgnoreCase));
        if (!File.Exists(path) || new FileInfo(path).Length != entry.Size || !string.Equals(await SafeFiles.HashAsync(path, token), entry.Sha256, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The game executable is missing or damaged. Run Repair Files.");
        return path;
    }

    public async Task<InstalledRelease> InstallAsync(ReleaseManifest manifest, IProgress<UpdateProgress>? progress = null, bool repair = false, CancellationToken token = default)
    {
        ValidateManifest(manifest);
        using var transactionLock = AcquireLock();
        if (gameRunning()) throw new InvalidOperationException("Close Rift Crown Arena before updating or repairing its files.");
        try { await RecoverLockedAsync(token); }
        catch (InvalidDataException error) when (!File.Exists(JournalPath) && File.Exists(PointerPath))
        {
            // A verified explicit install can recover an unreadable pointer without touching prior release trees or saves.
            File.Move(PointerPath, PointerPath + ".corrupt-" + DateTime.UtcNow.ToString("yyyyMMddHHmmssfff"), true);
            log.Write("Preserved damaged installation record before verified reinstall.", error);
        }
        var previous = Installed();
        if (previous is not null && ParseVersion(manifest.Version) < ParseVersion(previous.Version)) throw new InvalidDataException("An older release cannot replace the installed release.");
        if (!repair && previous is not null && ParseVersion(manifest.Version) == ParseVersion(previous.Version)) return previous;
        CheckFreeSpace(manifest);
        var id = Guid.NewGuid().ToString("N");
        var stageRelative = $"staging/{id}";
        var stage = SafeFiles.Inside(GameRoot, stageRelative);
        Directory.CreateDirectory(stage);
        var archive = SafeFiles.Inside(stage, "package.zip.part");
        var content = SafeFiles.Inside(stage, "content");
        var releaseRelative = $"releases/{manifest.Version}-{manifest.Sha256[..12].ToLowerInvariant()}-{id[..8]}";
        var candidate = new InstalledRelease { Version = manifest.Version, ReleasePath = releaseRelative, Executable = manifest.Executable, Manifest = manifest };
        var committed = false;
        try
        {
            await DownloadAsync(manifest, archive, progress, token);
            progress?.Report(new("Checking package", 0, 1, "SHA-256"));
            if (new FileInfo(archive).Length != manifest.Size || !string.Equals(await SafeFiles.HashAsync(archive, token), manifest.Sha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The downloaded package does not match its SHA-256 or size. Your current game is unchanged.");
            await ExtractAsync(archive, content, manifest, progress, token);
            var check = await VerifyFolderAsync(content, manifest, progress, token);
            if (!check.IsHealthy) throw new InvalidDataException("Package inventory verification failed: " + string.Join(", ", check.Damaged.Take(5)));
            token.ThrowIfCancellationRequested();
            if (gameRunning()) throw new InvalidOperationException("The game started during staging. Close it before applying the update.");
            progress?.Report(new("Installing", 0, 1, manifest.Version));
            SafeFiles.AtomicWrite(JournalPath, ReleaseJson.Write(new UpdateJournal(previous, candidate, stageRelative)));
            Directory.CreateDirectory(Path.Combine(GameRoot, "releases"));
            Directory.Move(content, ReleaseFolder(candidate));
            if (previous is not null) SafeFiles.AtomicWrite(PreviousPath, ReleaseJson.Write(previous));
            SafeFiles.AtomicWrite(PointerPath, ReleaseJson.Write(candidate));
            committed = true;
            try { File.Delete(JournalPath); } catch (IOException error) { log.Write("Committed update journal cleanup will resume on next startup.", error); }
            log.Write($"Committed {manifest.Version} to {candidate.ReleasePath} ({(repair ? "repair" : "update")}).");
            progress?.Report(new("Ready", 1, 1, $"Rift Crown Arena {manifest.Version}"));
            return candidate;
        }
        catch (Exception error)
        {
            log.Write($"Installation {manifest.Version} failed before commit={(!committed)}.", error);
            if (!committed && File.Exists(JournalPath)) await RecoverLockedAsync(CancellationToken.None);
            throw;
        }
        finally
        {
            try { SafeFiles.DeleteTree(GameRoot, stageRelative); }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException) { log.Write("Staging cleanup deferred.", error); }
        }
    }

    private FileStream AcquireLock()
    {
        try { return new FileStream(Path.Combine(GameRoot, "update.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
        catch (IOException error) { throw new InvalidOperationException("Another launcher is already updating this installation.", error); }
    }

    private void CheckFreeSpace(ReleaseManifest manifest)
    {
        var required = checked(manifest.Size + manifest.Files.Sum(f => f.Size) + 128L * 1024 * 1024);
        var volume = new DriveInfo(Path.GetPathRoot(GameRoot)!);
        if (volume.IsReady && volume.AvailableFreeSpace < required) throw new IOException($"This update needs {required / (1024 * 1024)} MB of available space.");
    }

    private async Task DownloadAsync(ReleaseManifest manifest, string destination, IProgress<UpdateProgress>? progress, CancellationToken token)
    {
        using var headerDeadline = CancellationTokenSource.CreateLinkedTokenSource(token);
        headerDeadline.CancelAfter(TimeSpan.FromSeconds(30));
        using var response = await http.GetAsync(ValidateUrl(manifest.DownloadUrl), HttpCompletionOption.ResponseHeadersRead, headerDeadline.Token);
        response.EnsureSuccessStatusCode();
        if (response.RequestMessage?.RequestUri is Uri redirected) ValidateUrl(redirected.AbsoluteUri);
        if (response.Content.Headers.ContentLength is long length && length != manifest.Size) throw new InvalidDataException("The server reported a different package size.");
        await using var source = await response.Content.ReadAsStreamAsync(token);
        await using var target = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None, 131072, FileOptions.Asynchronous);
        var buffer = new byte[131072]; long received = 0;
        var clock = Stopwatch.StartNew();
        while (true)
        {
            using var idle = CancellationTokenSource.CreateLinkedTokenSource(token); idle.CancelAfter(TimeSpan.FromSeconds(30));
            var read = await source.ReadAsync(buffer, idle.Token);
            if (read == 0) break;
            received += read;
            if (received > manifest.Size) throw new InvalidDataException("The download exceeds its declared size.");
            await target.WriteAsync(buffer.AsMemory(0, read), token);
            if (clock.ElapsedMilliseconds > 100 || received == manifest.Size)
            { progress?.Report(new("Downloading", received, manifest.Size, $"{received / 1048576d:F1} / {manifest.Size / 1048576d:F1} MB")); clock.Restart(); }
        }
        await target.FlushAsync(token); target.Flush(true);
        if (received != manifest.Size) throw new InvalidDataException("The package download was incomplete.");
    }

    private static async Task ExtractAsync(string archive, string content, ReleaseManifest manifest, IProgress<UpdateProgress>? progress, CancellationToken token)
    {
        Directory.CreateDirectory(content);
        using var zip = ZipFile.OpenRead(archive);
        var inventory = manifest.Files.ToDictionary(f => f.Path, StringComparer.OrdinalIgnoreCase);
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        if (zip.Entries.Count > manifest.Files.Count * 2 + 1000) throw new InvalidDataException("The archive has too many entries.");
        var count = 0;
        foreach (var entry in zip.Entries)
        {
            token.ThrowIfCancellationRequested();
            var directory = entry.FullName.EndsWith('/');
            var name = directory ? entry.FullName.TrimEnd('/') : entry.FullName;
            SafeFiles.RelativePath(name);
            var unixType = (entry.ExternalAttributes >> 16) & 0xF000;
            if (unixType == 0xA000 || (entry.ExternalAttributes & (int)FileAttributes.ReparsePoint) != 0) throw new InvalidDataException("Archive links are unsupported.");
            var path = SafeFiles.Inside(content, name);
            if (!seen.Add(name)) throw new InvalidDataException("The archive contains duplicate paths.");
            if (directory) { Directory.CreateDirectory(path); continue; }
            if (!inventory.TryGetValue(name, out var declared) || entry.Length != declared.Size) throw new InvalidDataException("The archive differs from its file inventory.");
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            await using var input = entry.Open();
            await using var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None, 131072, FileOptions.Asynchronous);
            var buffer = new byte[131072]; long written = 0;
            int read;
            while ((read = await input.ReadAsync(buffer, token)) > 0)
            {
                written += read;
                if (written > declared.Size) throw new InvalidDataException("An extracted file exceeds its declared size.");
                await output.WriteAsync(buffer.AsMemory(0, read), token);
            }
            if (written != declared.Size) throw new InvalidDataException("An extracted file is incomplete.");
            await output.FlushAsync(token);
            count++;
            progress?.Report(new("Extracting", count, manifest.Files.Count, name));
        }
        if (count != manifest.Files.Count) throw new InvalidDataException("The archive is missing release files.");
    }

    public async Task<string?> RecoverAsync(CancellationToken token = default)
    {
        using var transactionLock = AcquireLock();
        return await RecoverLockedAsync(token);
    }

    private async Task<string?> RecoverLockedAsync(CancellationToken token)
    {
        if (!File.Exists(JournalPath))
        {
            try { Installed(); }
            catch (Exception error) when (error is InvalidDataException or System.Text.Json.JsonException or IOException)
            {
                if (!File.Exists(PreviousPath)) throw new InvalidDataException("The installation record is damaged and no previous release is available. Check for updates to reinstall.", error);
                var previous = ReleaseJson.Read<InstalledRelease>(File.ReadAllText(PreviousPath)); ValidatePointer(previous);
                if (!(await VerifyAsync(previous, null, token)).IsHealthy) throw new InvalidDataException("The current and previous installations need repair.", error);
                SafeFiles.AtomicWrite(PointerPath, ReleaseJson.Write(previous)); log.Write("Recovered previous installation record.");
                return "Recovered the previous verified game version.";
            }
            return null;
        }
        var journal = ReleaseJson.Read<UpdateJournal>(File.ReadAllText(JournalPath));
        ValidatePointer(journal.Candidate);
        if (!journal.StagingPath.StartsWith("staging/", StringComparison.Ordinal) || journal.StagingPath.Split('/').Length != 2) throw new InvalidDataException("Invalid update recovery record.");
        SafeFiles.Inside(GameRoot, journal.StagingPath);
        InstalledRelease? installed = null;
        try { installed = Installed(); } catch (Exception error) when (error is InvalidDataException or System.Text.Json.JsonException) { log.Write("Recovery found an invalid active record.", error); }
        var accepted = installed?.ReleasePath == journal.Candidate.ReleasePath && (await VerifyAsync(journal.Candidate, null, token)).IsHealthy;
        if (!accepted)
        {
            if (journal.Previous is not null)
            {
                ValidatePointer(journal.Previous);
                if (!(await VerifyAsync(journal.Previous, null, token)).IsHealthy) throw new InvalidDataException("The previous version is damaged; recovery requires Repair Files.");
                SafeFiles.AtomicWrite(PointerPath, ReleaseJson.Write(journal.Previous));
            }
            else if (File.Exists(PointerPath)) File.Delete(PointerPath);
        }
        SafeFiles.DeleteTree(GameRoot, journal.StagingPath);
        File.Delete(JournalPath);
        log.Write(accepted ? "Recovered a completed update transaction." : "Rolled back an interrupted update transaction.");
        return accepted ? "Finished recovery of the verified update." : "Recovered the previous game version after an interrupted update.";
    }

    public void Dispose() => http.Dispose();
}

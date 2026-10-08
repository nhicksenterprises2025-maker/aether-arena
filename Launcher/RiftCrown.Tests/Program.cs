using System.IO.Compression;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using RiftCrown.UpdateCore;

var outputPath = args.FirstOrDefault() ?? Path.Combine(Path.GetTempPath(), "rift-launcher-qa.json");
var outcomes = new List<object>();
var failed = 0;
using var server = new FixtureServer();

async Task Check(string name, Func<TestWorld, Task> action)
{
    using var world = new TestWorld(server);
    var timer = System.Diagnostics.Stopwatch.StartNew();
    try { await action(world); outcomes.Add(new { name, passed = true, milliseconds = timer.ElapsedMilliseconds }); Console.WriteLine("PASS " + name); }
    catch (Exception error) { failed++; outcomes.Add(new { name, passed = false, milliseconds = timer.ElapsedMilliseconds, error = error.ToString() }); Console.WriteLine("FAIL " + name + ": " + error.Message); }
}
static void Require(bool condition, string message) { if (!condition) throw new Exception(message); }
static async Task Reject(Func<Task> action)
{
    try { await action(); }
    catch (Exception error) when (error is InvalidDataException or IOException or InvalidOperationException or OperationCanceledException or HttpRequestException or System.Text.Json.JsonException) { return; }
    throw new Exception("An invalid operation unexpectedly succeeded.");
}

await Check("valid loopback manifest, streamed install, complete file verification and save preservation", async w =>
{
    var release = w.Package("1.0.0");
    var checkedManifest = await w.Manager.CheckAsync(w.Endpoint(release));
    var installed = await w.Manager.InstallAsync(checkedManifest);
    Require(installed.Version == "1.0.0" && (await w.Manager.VerifyAsync(installed)).IsHealthy, "Install did not produce verified files.");
    Require(File.Exists(await w.Manager.ValidateEntryPointAsync(installed)), "Entry point verification failed.");
    w.AssertSaves();
});
await Check("same-version update is idempotent and downgrade is rejected", async w =>
{
    var release = w.Package("2.0.0"); var initial = await w.Manager.InstallAsync(release);
    var second = await w.Manager.InstallAsync(release);
    Require(second.ReleasePath == initial.ReleasePath, "Same-version update changed the installation.");
    await Reject(() => w.Manager.InstallAsync(w.Package("1.0.0"))); w.AssertSaves();
});
await Check("missing and corrupt game files are detected and restored from verified package", async w =>
{
    var release = w.Package("1.0.0"); var initial = await w.Manager.InstallAsync(release);
    File.Delete(SafeFiles.Inside(w.Manager.ReleaseFolder(initial), "Content/data.bin"));
    File.WriteAllText(w.Manager.ExecutablePath(initial), "CORRUPT");
    var scan = await w.Manager.VerifyAsync(initial); Require(scan.Damaged.Count == 2, "Repair failed to detect both damaged files.");
    var fixedRelease = await w.Manager.InstallAsync(release, repair: true);
    Require(fixedRelease.ReleasePath != initial.ReleasePath && (await w.Manager.VerifyAsync(fixedRelease)).IsHealthy, "Repair did not commit a clean verified release.");
    w.AssertSaves();
});
await Check("archive SHA mismatch keeps the current pointer and every save byte", async w =>
{
    var first = await w.Manager.InstallAsync(w.Package("1.0.0"));
    var wrong = w.Package("2.0.0") with { Sha256 = new string('a', 64) };
    await Reject(() => w.Manager.InstallAsync(wrong));
    Require(w.Manager.Installed()?.ReleasePath == first.ReleasePath, "Hash failure altered the pointer."); w.AssertSaves();
});
await Check("declared size mismatch is rejected before commit", async w =>
{
    var release = w.Package("1.0.0"); await Reject(() => w.Manager.InstallAsync(release with { Size = release.Size + 1 }));
    Require(w.Manager.Installed() is null, "Invalid size installed a release."); w.AssertSaves();
});
await Check("file inventory SHA mismatch rejects a validly hashed archive", async w =>
{
    var release = w.Package("1.0.0");
    var files = release.Files.Select(f => f.Path.EndsWith(".bin") ? f with { Sha256 = new string('0', 64) } : f).ToList();
    await Reject(() => w.Manager.InstallAsync(release with { Files = files })); Require(w.Manager.Installed() is null, "Incorrect inventory was installed.");
});
foreach (var path in new[] { "../escape.exe", "/absolute.exe", "C:/escape.exe", "dir\\escape.exe", "file:stream", "CON.txt", "folder./file.exe", "LPT1/data" })
    await Check("manifest rejects unsafe Windows path " + path, async w =>
    {
        var release = w.Package("1.0.0");
        await Reject(() => { w.Manager.ValidateManifest(release with { Files = [new PackageFile(path, 0, new string('0', 64))] }); return Task.CompletedTask; });
    });
await Check("ZIP traversal is rejected even if top-level archive hash is correct", async w =>
{
    var release = w.Package("1.0.0", extraEntry: "../escape.exe"); await Reject(() => w.Manager.InstallAsync(release));
    Require(!File.Exists(Path.Combine(w.Root, "escape.exe")) && w.Manager.Installed() is null, "ZIP escaped or committed."); w.AssertSaves();
});
await Check("ZIP links are rejected", async w =>
{
    var release = w.Package("1.0.0", link: true); await Reject(() => w.Manager.InstallAsync(release)); Require(w.Manager.Installed() is null, "Archive link installed.");
});
await Check("ZIP duplicate normalized paths are rejected", async w =>
{
    var release = w.Package("1.0.0", extraEntry: "Content/DATA.bin"); await Reject(() => w.Manager.InstallAsync(release));
});
await Check("manifest duplicate and file-directory conflicts are rejected", async w =>
{
    var release = w.Package("1.0.0");
    await Reject(() => { w.Manager.ValidateManifest(release with { Files = [.. release.Files, release.Files[0]] }); return Task.CompletedTask; });
    await Reject(() => { w.Manager.ValidateManifest(release with { Files = [.. release.Files, new("Content", 0, new string('0', 64))] }); return Task.CompletedTask; });
});
await Check("unsupported schema, platform, future launcher and invalid version are rejected", async w =>
{
    var release = w.Package("1.0.0");
    foreach (var invalid in new[] { release with { SchemaVersion = 2 }, release with { Platform = "linux-x64" }, release with { MinimumLauncherVersion = "9.0.0" }, release with { Version = "../../bad" } })
        await Reject(() => { w.Manager.ValidateManifest(invalid); return Task.CompletedTask; });
    Require(ReleaseManager.ParseVersion("1.0.0") == ReleaseManager.ParseVersion("1.0.0.0"), "Version normalization failed.");
});
await Check("remote plain HTTP and credential-bearing URLs are rejected", async w =>
{
    var release = w.Package("1.0.0");
    foreach (var url in new[] { "http://example.org/package.zip", "https://user:secret@example.org/package.zip", "file:///C:/package.zip" })
        await Reject(() => { w.Manager.ValidateManifest(release with { DownloadUrl = url }); return Task.CompletedTask; });
});
await Check("cancellation never installs or removes saved data", async w =>
{
    var release = w.Package("1.0.0"); using var cancel = new CancellationTokenSource(); cancel.Cancel();
    await Reject(() => w.Manager.InstallAsync(release, token: cancel.Token)); Require(w.Manager.Installed() is null, "Cancelled install committed."); w.AssertSaves();
});
await Check("active game prevents update and repair", async w =>
{
    using var manager = new ReleaseManager(w.InstallRoot, w.SaveRoot, allowLoopback: true, gameRunning: () => true);
    await Reject(() => manager.InstallAsync(w.Package("1.0.0"))); w.AssertSaves();
});
await Check("exclusive update lock prevents a second transaction", async w =>
{
    using var held = new FileStream(Path.Combine(w.Manager.GameRoot, "update.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
    await Reject(() => w.Manager.InstallAsync(w.Package("1.0.0"))); w.AssertSaves();
});
await Check("install roots cannot contain or sit within save folders", async w =>
{
    await Reject(() => { using var manager = new ReleaseManager(w.SaveRoot, Path.Combine(w.SaveRoot, "Nested")); return Task.CompletedTask; });
    await Reject(() => { using var manager = new ReleaseManager(Path.Combine(w.SaveRoot, "Install"), w.SaveRoot); return Task.CompletedTask; });
    w.AssertSaves();
});
await Check("interrupted transaction before activation restores old pointer and cleans only staging", async w =>
{
    var previous = await w.Manager.InstallAsync(w.Package("1.0.0")); var candidate = await w.Manager.InstallAsync(w.Package("2.0.0"));
    SafeFiles.AtomicWrite(Path.Combine(w.Manager.GameRoot, "installed.json"), ReleaseJson.Write(previous));
    var staging = SafeFiles.Inside(w.Manager.GameRoot, "staging/fixture-interrupted"); Directory.CreateDirectory(staging); File.WriteAllText(Path.Combine(staging, "partial"), "partial");
    SafeFiles.AtomicWrite(Path.Combine(w.Manager.GameRoot, "update-journal.json"), ReleaseJson.Write(new { previous, candidate, stagingPath = "staging/fixture-interrupted" }));
    await w.Manager.RecoverAsync();
    Require(w.Manager.Installed()?.Version == "1.0.0" && !Directory.Exists(staging), "Pre-activation rollback failed."); w.AssertSaves();
});
await Check("interrupted transaction after activation accepts fully verified new pointer", async w =>
{
    var previous = await w.Manager.InstallAsync(w.Package("1.0.0")); var candidate = await w.Manager.InstallAsync(w.Package("2.0.0"));
    Directory.CreateDirectory(SafeFiles.Inside(w.Manager.GameRoot, "staging/fixture-committed"));
    SafeFiles.AtomicWrite(Path.Combine(w.Manager.GameRoot, "update-journal.json"), ReleaseJson.Write(new { previous, candidate, stagingPath = "staging/fixture-committed" }));
    await w.Manager.RecoverAsync(); Require(w.Manager.Installed()?.Version == "2.0.0", "Committed recovery rolled back good files."); w.AssertSaves();
});
await Check("corrupt candidate after interrupted activation rolls back to verified previous release", async w =>
{
    var previous = await w.Manager.InstallAsync(w.Package("1.0.0")); var candidate = await w.Manager.InstallAsync(w.Package("2.0.0"));
    File.WriteAllText(w.Manager.ExecutablePath(candidate), "broken");
    Directory.CreateDirectory(SafeFiles.Inside(w.Manager.GameRoot, "staging/fixture-corrupt"));
    SafeFiles.AtomicWrite(Path.Combine(w.Manager.GameRoot, "update-journal.json"), ReleaseJson.Write(new { previous, candidate, stagingPath = "staging/fixture-corrupt" }));
    await w.Manager.RecoverAsync(); Require(w.Manager.Installed()?.Version == "1.0.0", "Recovery kept a damaged candidate."); w.AssertSaves();
});
await Check("damaged installed JSON recovers verified previous pointer", async w =>
{
    await w.Manager.InstallAsync(w.Package("1.0.0")); await w.Manager.InstallAsync(w.Package("2.0.0"));
    File.WriteAllText(Path.Combine(w.Manager.GameRoot, "installed.json"), "{corrupt");
    await w.Manager.RecoverAsync(); Require(w.Manager.Installed()?.Version == "1.0.0", "Record fallback failed."); w.AssertSaves();
});
await Check("verified reinstall repairs an unreadable first installation record without a backup", async w =>
{
    await w.Manager.InstallAsync(w.Package("1.0.0"));
    File.WriteAllText(Path.Combine(w.Manager.GameRoot, "installed.json"), "{corrupt");
    var installed = await w.Manager.InstallAsync(w.Package("2.0.0"));
    Require(installed.Version == "2.0.0" && (await w.Manager.VerifyAsync(installed)).IsHealthy, "Explicit reinstall could not recover a corrupt first record.");
    Require(Directory.GetFiles(w.Manager.GameRoot, "installed.json.corrupt-*").Length == 1, "Damaged record was not preserved."); w.AssertSaves();
});
await Check("malformed network JSON never becomes available installation", async w =>
{
    var endpoint = server.Add(Encoding.UTF8.GetBytes("{bad")); await Reject(async () => { await w.Manager.CheckAsync(endpoint); });
    Require(w.Manager.Installed() is null, "Malformed JSON changed installation.");
});
await Check("HTTP failure is surfaced without touching saves", async w =>
{
    var endpoint = server.Add([], 404); await Reject(async () => { await w.Manager.CheckAsync(endpoint); }); w.AssertSaves();
});

SafeFiles.AtomicWrite(outputPath, ReleaseJson.Write(new { suite = "RiftCrown.UpdateCore isolated integration", passed = failed == 0, tests = outcomes.Count, failures = failed, utc = DateTime.UtcNow, outcomes }));
Console.WriteLine($"{outcomes.Count - failed}/{outcomes.Count} passed. Report: {outputPath}");
return failed == 0 ? 0 : 1;

sealed class TestWorld : IDisposable
{
    public string Root { get; } = Path.Combine(Path.GetTempPath(), "RiftLauncherQA", Guid.NewGuid().ToString("N"));
    public string InstallRoot => Path.Combine(Root, "Install");
    public string SaveRoot => Path.Combine(Root, "Saves");
    public ReleaseManager Manager { get; }
    private readonly FixtureServer server;
    private readonly Dictionary<string, byte[]> saves = new(StringComparer.Ordinal);
    public TestWorld(FixtureServer server)
    {
        this.server = server; Directory.CreateDirectory(SaveRoot);
        foreach (var filename in new[] { "player_save.json", "lab_v15.json", "ue_save.json", "Replays/one.json" })
        {
            var bytes = Encoding.UTF8.GetBytes("isolated-save-fixture-" + filename);
            var path = SafeFiles.Inside(SaveRoot, filename); Directory.CreateDirectory(Path.GetDirectoryName(path)!); File.WriteAllBytes(path, bytes); saves[filename] = bytes;
        }
        Manager = new(InstallRoot, SaveRoot, allowLoopback: true);
    }
    public ReleaseManifest Package(string version, string? extraEntry = null, bool link = false)
    {
        var files = new Dictionary<string, byte[]> { ["RiftCrownArena.exe"] = Encoding.UTF8.GetBytes("isolated-test-entry-" + version), ["Content/data.bin"] = Encoding.UTF8.GetBytes("release-resource-" + version), ["Config/runtime.json"] = Encoding.UTF8.GetBytes("{\"fixture\":true}") };
        using var memory = new MemoryStream();
        using (var zip = new ZipArchive(memory, ZipArchiveMode.Create, true))
        {
            foreach (var (name, bytes) in files) { var entry = zip.CreateEntry(name); if (link && name.EndsWith(".bin")) entry.ExternalAttributes = 0xA000 << 16; using var stream = entry.Open(); stream.Write(bytes); }
            if (extraEntry is not null) { var entry = zip.CreateEntry(extraEntry); using var stream = entry.Open(); stream.WriteByte(42); }
        }
        var archive = memory.ToArray();
        return new() { Version = version, DownloadUrl = server.Add(archive), Sha256 = Hash(archive), Size = archive.Length, PatchNotes = "Isolated integration fixture " + version, Files = files.Select(f => new PackageFile(f.Key, f.Value.Length, Hash(f.Value))).ToList() };
    }
    public string Endpoint(ReleaseManifest release) => server.Add(Encoding.UTF8.GetBytes(ReleaseJson.Write(release)));
    private static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
    public void AssertSaves() { foreach (var (name, bytes) in saves) { var actual = File.ReadAllBytes(SafeFiles.Inside(SaveRoot, name)); if (!actual.SequenceEqual(bytes)) throw new Exception("Saved data changed: " + name); } }
    public void Dispose() { Manager.Dispose(); if (Directory.Exists(Root)) Directory.Delete(Root, true); }
}

sealed class FixtureServer : IDisposable
{
    private readonly HttpListener listener = new();
    private readonly Dictionary<string, (byte[] Bytes, int Status)> responses = [];
    private readonly CancellationTokenSource stop = new();
    private readonly Task loop;
    public string BaseUrl { get; }
    public FixtureServer()
    {
        var probe = new TcpListener(IPAddress.Loopback, 0); probe.Start(); var port = ((IPEndPoint)probe.LocalEndpoint).Port; probe.Stop();
        BaseUrl = $"http://127.0.0.1:{port}/"; listener.Prefixes.Add(BaseUrl); listener.Start();
        loop = Task.Run(async () =>
        {
            while (!stop.IsCancellationRequested)
            {
                HttpListenerContext context;
                try { context = await listener.GetContextAsync(); } catch (HttpListenerException) { break; } catch (ObjectDisposedException) { break; }
                (byte[] Bytes, int Status) response;
                lock (responses) response = responses.GetValueOrDefault(context.Request.Url!.AbsolutePath, ([], 404));
                try { context.Response.StatusCode = response.Status; context.Response.ContentLength64 = response.Bytes.Length; await context.Response.OutputStream.WriteAsync(response.Bytes); }
                catch (Exception error) when (error is IOException or HttpListenerException or ObjectDisposedException) { }
                finally { context.Response.Close(); }
            }
        });
    }
    public string Add(byte[] content, int status = 200) { var id = "/" + Guid.NewGuid().ToString("N"); lock (responses) responses[id] = (content, status); return BaseUrl.TrimEnd('/') + id; }
    public void Dispose() { stop.Cancel(); listener.Close(); loop.GetAwaiter().GetResult(); stop.Dispose(); }
}

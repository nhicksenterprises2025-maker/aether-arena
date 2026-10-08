using System.Text.Json;
using System.Text.Json.Serialization;

namespace RiftCrown.UpdateCore;

public sealed record PackageFile(string Path, long Size, string Sha256);
public sealed record ReleaseManifest
{
    public int SchemaVersion { get; init; } = 1;
    public string Version { get; init; } = "";
    public string Platform { get; init; } = "windows-x64";
    public string DownloadUrl { get; init; } = "";
    public string Sha256 { get; init; } = "";
    public long Size { get; init; }
    public string Executable { get; init; } = "RiftCrownArena.exe";
    public string PatchNotes { get; init; } = "";
    public string MinimumLauncherVersion { get; init; } = "1.0.0";
    public List<PackageFile> Files { get; init; } = [];
}
public sealed record InstalledRelease
{
    public int SchemaVersion { get; init; } = 1;
    public string Version { get; init; } = "";
    public string ReleasePath { get; init; } = "";
    public string Executable { get; init; } = "";
    public ReleaseManifest Manifest { get; init; } = new();
}
public sealed record LauncherSettings
{
    public int SchemaVersion { get; init; } = 1;
    public string ManifestUrl { get; init; } = "https://github.com/nhicksenterprises2025-maker/aether-arena/releases/latest/download/update-manifest.json";
    public bool CheckOnStartup { get; init; } = true;
    public string LaunchArguments { get; init; } = "";
    public string GameInstallRoot { get; init; } = "";
}
public sealed record UpdateProgress(string Phase, long Bytes, long Total, string Detail)
{
    public double Fraction => Total > 0 ? Math.Clamp((double)Bytes / Total, 0, 1) : 0;
}
public sealed record RepairResult(int Checked, List<string> Damaged)
{
    public bool IsHealthy => Damaged.Count == 0;
}
internal sealed record UpdateJournal(InstalledRelease? Previous, InstalledRelease Candidate, string StagingPath);

public static class ReleaseJson
{
    public static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = false,
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.Never,
        MaxDepth = 32
    };
    public static T Read<T>(string value) => JsonSerializer.Deserialize<T>(value, Options)
        ?? throw new InvalidDataException("The JSON document has no object.");
    public static string Write<T>(T value) => JsonSerializer.Serialize(value, Options);
}

using System.Security.Cryptography;
using System.Text;

namespace RiftCrown.UpdateCore;

public static class SafeFiles
{
    public static string RelativePath(string input)
    {
        if (string.IsNullOrWhiteSpace(input) || input.Length > 240 || input.Contains('\\') || input.Contains(':') || input.StartsWith('/') || input.EndsWith('/'))
            throw new InvalidDataException($"Invalid package path: {input}");
        var segments = input.Split('/');
        foreach (var segment in segments)
        {
            if (segment is "" or "." or ".." || segment.EndsWith('.') || segment.EndsWith(' ') || segment.Any(c => c < 32 || "<>\"|?*".Contains(c)))
                throw new InvalidDataException($"Invalid package path: {input}");
            var stem = segment.Split('.')[0].ToUpperInvariant();
            if (stem is "CON" or "PRN" or "AUX" or "NUL" || (stem.Length == 4 && (stem.StartsWith("COM") || stem.StartsWith("LPT")) && stem[3] is >= '0' and <= '9'))
                throw new InvalidDataException($"Reserved Windows package path: {input}");
        }
        return input;
    }

    public static string Inside(string root, string relative)
    {
        RelativePath(relative);
        var fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var full = Path.GetFullPath(Path.Combine(fullRoot, relative.Replace('/', Path.DirectorySeparatorChar)));
        if (!full.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("A package path leaves the installation folder.");
        EnsureNoLinks(fullRoot.TrimEnd(Path.DirectorySeparatorChar), full);
        return full;
    }

    public static void EnsureNoLinks(string root, string target)
    {
        var current = Path.GetFullPath(root);
        for (var ancestor = Directory.GetParent(current); ancestor is not null; ancestor = ancestor.Parent)
            if (ancestor.Exists && (ancestor.Attributes & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("Installation path contains a directory link.");
        if ((Directory.Exists(current) || File.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
            throw new InvalidDataException("Installation root is a directory link.");
        foreach (var part in Path.GetRelativePath(current, target).Split(Path.DirectorySeparatorChar, StringSplitOptions.RemoveEmptyEntries))
        {
            current = Path.Combine(current, part);
            if ((Directory.Exists(current) || File.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("A package path follows a directory link.");
        }
    }

    public static void DistinctRoots(string installRoot, string saveRoot)
    {
        static string Prefix(string p) => Path.GetFullPath(p).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        var install = Prefix(installRoot); var save = Prefix(saveRoot);
        if (install.StartsWith(save, StringComparison.OrdinalIgnoreCase) || save.StartsWith(install, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("The installation and save folders must be separate.");
    }

    public static async Task<string> HashAsync(string path, CancellationToken token = default)
    {
        await using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 131072, FileOptions.Asynchronous | FileOptions.SequentialScan);
        return Convert.ToHexString(await SHA256.HashDataAsync(stream, token)).ToLowerInvariant();
    }

    public static void AtomicWrite(string path, string text)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try
        {
            using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                var bytes = Encoding.UTF8.GetBytes(text);
                stream.Write(bytes); stream.Flush(true);
            }
            File.Move(temporary, path, true);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }

    public static void DeleteTree(string root, string relative)
    {
        var full = Inside(root, relative);
        if (!Directory.Exists(full)) return;
        foreach (var item in Directory.EnumerateFileSystemEntries(full, "*", SearchOption.AllDirectories))
            EnsureNoLinks(root, item);
        Directory.Delete(full, true);
    }
}

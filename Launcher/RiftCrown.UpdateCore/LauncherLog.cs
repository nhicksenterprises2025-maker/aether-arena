namespace RiftCrown.UpdateCore;

public sealed class LauncherLog
{
    private readonly string path;
    private readonly object sync = new();
    public LauncherLog(string saveRoot, string category)
    {
        var folder = Path.Combine(saveRoot, "Logs", category);
        Directory.CreateDirectory(folder);
        path = Path.Combine(folder, $"{DateTime.UtcNow:yyyy-MM-dd}.log");
        foreach (var old in Directory.GetFiles(folder, "*.log").OrderByDescending(File.GetLastWriteTimeUtc).Skip(14))
        { try { File.Delete(old); } catch (IOException) { } catch (UnauthorizedAccessException) { } }
    }
    public void Write(string message, Exception? error = null)
    {
        var entry = $"{DateTime.UtcNow:O} {message}" + (error is null ? "" : $" | {error.GetType().Name}: {error.Message}") + Environment.NewLine;
        lock (sync)
        {
            try
            {
                if (File.Exists(path) && new FileInfo(path).Length > 4 * 1024 * 1024) File.Move(path, path + ".previous", true);
                File.AppendAllText(path, entry);
            }
            catch (IOException) { } catch (UnauthorizedAccessException) { }
        }
    }
}

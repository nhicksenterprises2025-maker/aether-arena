using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using RiftCrown.UpdateCore;

namespace RiftCrown.Launcher;

public partial class MainWindow : Window
{
    private readonly string bundledRoot;
    private readonly string saveRoot;
    private readonly string settingsPath;
    private readonly LauncherLog log;
    private LauncherSettings settings;
    private ReleaseManager manager;
    private InstalledRelease? installed;
    private ReleaseManifest? available;
    private CancellationTokenSource? operation;
    private Process? launchedGame;
    private Task? playCommandOperation;
    private bool busy;
    private readonly System.Windows.Threading.DispatcherTimer processPoll = new() { Interval = TimeSpan.FromSeconds(2) };

    public MainWindow(string installRoot, string saveRoot)
    {
        InitializeComponent();
        bundledRoot = Path.GetFullPath(installRoot); this.saveRoot = Path.GetFullPath(saveRoot);
        settingsPath = Path.Combine(saveRoot, "Launcher", "settings.json");
        log = new LauncherLog(saveRoot, "Launcher");
        settings = LoadSettings();
        manager = CreateManager(settings);
        Loaded += async (_, _) =>
        {
            await PerformAsync(async token => { var recovered = await manager.RecoverAsync(token); RefreshInstallation(); SetStatus(recovered ?? (installed is null ? "Game files are not installed. Check for updates to install." : "Ready to play.")); });
            if (settings.CheckOnStartup) await CheckForUpdatesAsync();
        };
        Closing += Window_Closing;
        Closed += (_, _) => { processPoll.Stop(); manager.Dispose(); launchedGame?.Dispose(); };
        processPoll.Tick += (_, _) => RefreshButtons(); processPoll.Start();
        RefreshButtons();
    }

    public int ValidateBindings(string previewFolder)
    {
        var commands = new System.Windows.Controls.Button[] { PlayButton, CheckButton, UpdateButton, RepairButton, SettingsButton, SaveFolderButton, LogsButton, CancelButton, SettingsSaveButton, SettingsCancelButton };
        if (commands.Any(control => control is null) || new object?[] { EndpointInput, InstallInput, ArgumentsInput, StartupCheck }.Any(control => control is null))
            throw new InvalidOperationException("A launcher command or setting is missing its WPF control.");
        if (Application.Current.TryFindResource("Ink") is null) throw new InvalidOperationException("Launcher branding resources are unavailable.");
        SettingsButton.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));
        if (SettingsOverlay.Visibility != Visibility.Visible) throw new InvalidOperationException("Settings command is disconnected.");
        EndpointInput.Text = "invalid endpoint";
        SettingsSaveButton.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));
        if (string.IsNullOrEmpty(SettingsError.Text)) throw new InvalidOperationException("Settings validation is disconnected.");
        SettingsCancelButton.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));
        if (SettingsOverlay.Visibility != Visibility.Collapsed) throw new InvalidOperationException("Settings cancel command is disconnected.");
        var content = (FrameworkElement)Content;
        content.Measure(new Size(1000, 660)); content.Arrange(new Rect(0, 0, 1000, 660)); content.UpdateLayout();
        var bitmap = new System.Windows.Media.Imaging.RenderTargetBitmap(1000, 660, 96, 96, System.Windows.Media.PixelFormats.Pbgra32);
        bitmap.Render(content);
        var encoder = new System.Windows.Media.Imaging.PngBitmapEncoder(); encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bitmap));
        Directory.CreateDirectory(previewFolder);
        using var stream = File.Create(Path.Combine(previewFolder, "launcher-preview.png")); encoder.Save(stream);
        return commands.Length;
    }

    private LauncherSettings LoadSettings()
    {
        try { if (File.Exists(settingsPath)) return ReleaseJson.Read<LauncherSettings>(File.ReadAllText(settingsPath)); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or System.Text.Json.JsonException)
        { log.Write("Could not load launcher settings; defaults retained.", error); }
        return new LauncherSettings();
    }

    private ReleaseManager CreateManager(LauncherSettings value) => new(string.IsNullOrWhiteSpace(value.GameInstallRoot) ? bundledRoot : value.GameInstallRoot, saveRoot, gameRunning: IsGameRunning);

    private bool IsGameRunning()
    {
        try { if (launchedGame is not null && !launchedGame.HasExited) return true; } catch (InvalidOperationException) { }
        foreach (var name in new[] { "RiftCrownArena", "RiftCrownArena-Win64-Shipping" })
        {
            foreach (var process in Process.GetProcessesByName(name))
            {
                using (process) { try { if (!process.HasExited) return true; } catch (InvalidOperationException) { } }
            }
        }
        return false;
    }

    private void RefreshInstallation()
    {
        installed = manager.Installed();
        InstalledVersion.Text = installed is null ? "Game not installed" : "Game " + installed.Version;
        if (installed is not null)
        {
            PatchNotes.Text = installed.Manifest.PatchNotes;
            NotesVersion.Text = "Installed " + installed.Version;
        }
        RefreshButtons();
    }

    private void RefreshButtons()
    {
        var running = IsGameRunning();
        PlayButton.IsEnabled = !busy && !running && installed is not null;
        PlayButton.Content = running ? "PLAYING" : "PLAY";
        CheckButton.IsEnabled = !busy;
        UpdateButton.IsEnabled = !busy && !running && available is not null && (installed is null || ReleaseManager.ParseVersion(available.Version) > ReleaseManager.ParseVersion(installed.Version));
        UpdateButton.Content = installed is null ? "INSTALL" : "UPDATE";
        RepairButton.IsEnabled = !busy && !running && installed is not null;
        SettingsButton.IsEnabled = !busy && !running;
        CancelButton.Visibility = busy ? Visibility.Visible : Visibility.Collapsed;
    }

    private void SetStatus(string text) => Status.Text = text;

    private async Task PerformAsync(Func<CancellationToken, Task> action)
    {
        if (busy) return;
        busy = true; operation = new(); Progress.Value = 0; RefreshButtons();
        try { await action(operation.Token); }
        catch (OperationCanceledException) { SetStatus("Operation cancelled. The committed game version is preserved."); log.Write("Launcher operation cancelled."); }
        catch (Exception error)
        {
            log.Write("Launcher operation failed.", error);
            SetStatus(error.Message + " Open Logs for details.");
        }
        finally { operation.Dispose(); operation = null; busy = false; RefreshButtons(); }
    }

    private IProgress<UpdateProgress> ProgressReporter() => new Progress<UpdateProgress>(value =>
    {
        Progress.Value = value.Fraction * 100;
        SetStatus(value.Phase + (string.IsNullOrEmpty(value.Detail) ? "" : " — " + value.Detail));
    });

    private async Task CheckForUpdatesAsync()
    {
        await PerformAsync(async token =>
        {
            SetStatus("Checking for updates…");
            available = null;
            var manifest = await manager.CheckAsync(settings.ManifestUrl, token);
            available = manifest;
            PatchNotes.Text = manifest.PatchNotes; NotesVersion.Text = "Release " + manifest.Version;
            SetStatus(installed is null ? $"Rift Crown Arena {manifest.Version} is ready to install." : ReleaseManager.ParseVersion(manifest.Version) > ReleaseManager.ParseVersion(installed.Version) ? $"Update {manifest.Version} is available." : "Your installed game is current.");
            Progress.Value = 100;
        });
    }

    private async void Check_Click(object sender, RoutedEventArgs e) => await CheckForUpdatesAsync();
    private async void Update_Click(object sender, RoutedEventArgs e)
    {
        var release = available;
        if (release is null) return;
        await PerformAsync(async token =>
        {
            var reporter = ProgressReporter();
            installed = await Task.Run(() => manager.InstallAsync(release, reporter, token: token), token);
            RefreshInstallation(); SetStatus($"Rift Crown Arena {installed.Version} is ready to play.");
        });
    }

    private async void Repair_Click(object sender, RoutedEventArgs e)
    {
        var release = installed;
        if (release is null) return;
        await PerformAsync(async token =>
        {
            var reporter = ProgressReporter();
            var result = await Task.Run(() => manager.VerifyAsync(release, reporter, token), token);
            if (result.IsHealthy) { SetStatus($"All {result.Checked:N0} game files verified. No repair needed."); Progress.Value = 100; return; }
            SetStatus($"Repairing {result.Damaged.Count:N0} missing or damaged files…");
            installed = await Task.Run(() => manager.InstallAsync(release.Manifest, reporter, repair: true, token: token), token);
            RefreshInstallation(); SetStatus("Repair complete. Your profile, decks and replays are preserved.");
        });
    }

    private async void Play_Click(object sender, RoutedEventArgs e)
    {
        var release = installed;
        if (release is null || IsGameRunning()) return;
        await (playCommandOperation = PerformAsync(async token =>
        {
            SetStatus("Starting Rift Crown Arena…");
            var executable = await Task.Run(() => manager.ValidateEntryPointAsync(release, token), token);
            var start = new ProcessStartInfo(executable) { WorkingDirectory = Path.GetDirectoryName(executable)!, UseShellExecute = false, Arguments = settings.LaunchArguments };
            start.Environment["RIFT_SAVE_ROOT"] = saveRoot;
            launchedGame?.Dispose();
            launchedGame = Process.Start(start) ?? throw new IOException("Windows did not start the packaged game.");
            log.Write($"Started game {release.Version}, process {launchedGame.Id}.");
            SetStatus("Rift Crown Arena is running."); Progress.Value = 100;
        }));
    }

    private void Cancel_Click(object sender, RoutedEventArgs e) => operation?.Cancel();
    private void Saves_Click(object sender, RoutedEventArgs e) => OpenFolder(saveRoot);
    private void Logs_Click(object sender, RoutedEventArgs e) => OpenFolder(Path.Combine(saveRoot, "Logs"));
    private void OpenFolder(string path)
    {
        try { Directory.CreateDirectory(path); Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
        catch (Exception error) { log.Write("Opening folder failed.", error); SetStatus(error.Message); }
    }

    private void Settings_Click(object sender, RoutedEventArgs e)
    {
        EndpointInput.Text = settings.ManifestUrl;
        InstallInput.Text = manager.InstallRoot;
        ArgumentsInput.Text = settings.LaunchArguments;
        StartupCheck.IsChecked = settings.CheckOnStartup;
        SettingsError.Text = ""; SettingsOverlay.Visibility = Visibility.Visible; EndpointInput.Focus();
    }
    private void SettingsCancel_Click(object sender, RoutedEventArgs e) => SettingsOverlay.Visibility = Visibility.Collapsed;
    private void SettingsSave_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (!Uri.TryCreate(EndpointInput.Text.Trim(), UriKind.Absolute, out var endpoint) || endpoint.Scheme != Uri.UriSchemeHttps || !string.IsNullOrEmpty(endpoint.UserInfo) || !string.IsNullOrEmpty(endpoint.Fragment)) throw new InvalidDataException("Use an HTTPS manifest URL without credentials or a fragment.");
            if (ArgumentsInput.Text.Length > 2048 || ArgumentsInput.Text.Any(c => c < 32)) throw new InvalidDataException("Launch arguments cannot contain control characters or exceed 2,048 characters.");
            var value = settings with { ManifestUrl = endpoint.AbsoluteUri, GameInstallRoot = Path.GetFullPath(InstallInput.Text.Trim()), LaunchArguments = ArgumentsInput.Text.Trim(), CheckOnStartup = StartupCheck.IsChecked == true };
            var replacement = CreateManager(value);
            SafeFiles.AtomicWrite(settingsPath, ReleaseJson.Write(value));
            manager.Dispose(); manager = replacement; settings = value; available = null;
            installed = null; RefreshInstallation(); SettingsOverlay.Visibility = Visibility.Collapsed;
            SetStatus("Settings saved. Check for updates to use the selected endpoint.");
        }
        catch (Exception error) { SettingsError.Text = error.Message; log.Write("Settings validation failed.", error); }
    }

    private void Window_Closing(object? sender, CancelEventArgs e)
    {
        if (!busy) return;
        operation?.Cancel(); e.Cancel = true; SetStatus("Cancelling safely. Close the launcher when the operation finishes.");
    }
}

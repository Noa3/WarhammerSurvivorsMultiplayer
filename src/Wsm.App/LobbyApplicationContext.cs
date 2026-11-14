using Wsm.Launcher;

namespace Wsm.App;

internal sealed class LobbyApplicationContext : ApplicationContext
{
    private readonly LobbyForm lobby = new();
    private CancellationTokenSource lifetime = new();
    private readonly System.Windows.Forms.Timer watchdog = new() { Interval = 400 };
    private readonly string? runtimeOverride;
    private OwnedGameLaunch? owned;
    private TaskCompletionSource? startCompletion;
    private Task? stopTask;
    private bool starting;

    public LobbyApplicationContext(string? initialGame, string? runtimeOverride)
    {
        this.runtimeOverride = runtimeOverride;
        MainForm = lobby;
        lobby.BeforeClose = CloseOwnedAsync;
        lobby.LaunchGameRequested += async (_, _) => await ChooseAndStartAsync();
        lobby.FormClosed += (_, _) => { watchdog.Stop(); watchdog.Dispose(); lifetime.Cancel(); lifetime.Dispose(); };
        lobby.Shown += async (_, _) => { if (initialGame is not null) await StartAsync(initialGame); };
        watchdog.Tick += async (_, _) => await CheckExitedAsync();
        lobby.Show();
    }
    private void Track(OwnedGameLaunch candidate)
    {
        if (owned is not null && !ReferenceEquals(owned, candidate)) throw new InvalidOperationException("Ein Spielprozess wird bereits verwaltet.");
        owned = candidate;
        lobby.SetGameLaunchEnabled(false);
        watchdog.Start();
    }
    internal void TrackRunning(OwnedGameLaunch candidate)
    {
        Track(candidate);
        // Native Unity child/popup composition was not reliable; never hide the usable lobby.
        if (!lobby.IsDisposed && !lobby.Visible) lobby.Show();
    }
    private async Task CheckExitedAsync()
    {
        if (owned is null || stopTask is not null || !owned.Process.HasExited) return;
        try
        {
            await StopOwnedAsync();
            if (!lobby.IsDisposed && !starting) lobby.SetGameLaunchEnabled(true);
        }
        catch (Exception ex)
        {
            watchdog.Stop();
            if (!lobby.IsDisposed) MessageBox.Show(lobby, "Prozess konnte nicht freigegeben werden: " + ex.Message + "\nBeim Schließen wird erneut versucht.", "WSM", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }
    private async Task StopOwnedAsync()
    {
        if (owned is null) return;
        var operation = stopTask ??= StopCoreAsync(owned);
        try { await operation; }
        finally { if (ReferenceEquals(stopTask, operation)) stopTask = null; }
    }
    private async Task StopCoreAsync(OwnedGameLaunch launch)
    {
        await launch.DisposeAsync();
        if (ReferenceEquals(owned, launch)) owned = null;
    }
    private async Task<bool> CloseOwnedAsync()
    {
        lifetime.Cancel();
        if (startCompletion is not null) await startCompletion.Task;
        try
        {
            await StopOwnedAsync();
            return true;
        }
        catch (Exception ex)
        {
            // Preserve the launch and its process handle for an explicit retry.
            lifetime.Dispose(); lifetime = new CancellationTokenSource();
            MessageBox.Show(lobby, "Spiel konnte nicht beendet werden: " + ex.Message + "\nDie Lobby bleibt geöffnet. Schließen Sie sie erneut, um es noch einmal zu versuchen.", "WSM", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return false;
        }
    }
    private async Task ChooseAndStartAsync()
    {
        if (starting || owned is not null) return;
        using var picker = new FolderBrowserDialog { Description = "Spielinstallationsordner auswählen", UseDescriptionForTitle = true };
        if (picker.ShowDialog(lobby) == DialogResult.OK) await StartAsync(picker.SelectedPath);
    }
    private async Task StartAsync(string installation)
    {
        if (starting || owned is not null || lobby.IsDisposed) return;
        if (MessageBox.Show(lobby,
            "Das Spiel wird aus einer separaten Kopie im Fenstermodus gestartet. Die Kopie isoliert Spielstände, Benutzerprofil und Steam Cloud NICHT: Das Spiel kann Ihre üblichen Daten lesen und verändern.\n\nDie Lobby verbindet noch kein gemeinsames Spiel; ein Start über die Lobby bleibt gesperrt. Die Lobby bleibt als separates Fenster geöffnet; es gibt noch kein Ingame-Menü.\n\nSpiel jetzt wirklich starten?",
            "Spielstart bestätigen", MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) != DialogResult.Yes) return;
        starting = true; startCompletion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        lobby.SetGameLaunchEnabled(false);
        try
        {
            string root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WSM");
            var candidate = await OwnedGameLaunch.PrepareAndStartAsync(installation, runtimeOverride ?? Path.Combine(root, "runtime-cache"), Path.Combine(root, "logs"), lifetime.Token);
            Track(candidate);
            var process = candidate.Process;
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(lifetime.Token);
            timeout.CancelAfter(TimeSpan.FromSeconds(45));
            IntPtr window;
            while (true)
            {
                timeout.Token.ThrowIfCancellationRequested(); process.Refresh();
                if (process.HasExited) throw new InvalidOperationException("Das Spiel wurde vor dem Öffnen des Fensters beendet.");
                window = process.MainWindowHandle;
                if (window != IntPtr.Zero) break;
                await Task.Delay(200, timeout.Token);
            }
            TrackRunning(candidate);
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested) { }
        catch (Exception ex)
        {
            if (!lobby.IsDisposed) MessageBox.Show(lobby, "Spielstart fehlgeschlagen: " + ex.Message + (owned is null ? "" : "\nDer gestartete Prozess bleibt verwaltet und wird beim Schließen beendet."), "WSM", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            starting = false;
            if (owned is null && !lobby.IsDisposed) lobby.SetGameLaunchEnabled(true);
            startCompletion?.TrySetResult(); startCompletion = null;
        }
    }
}

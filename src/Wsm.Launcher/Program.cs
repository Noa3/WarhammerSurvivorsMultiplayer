using Wsm.Launcher;

try
{
    if (OperatingSystem.IsWindows() && args is ["play-installed"])
    {
        var selected = SteamGameDirectory.Select(Environment.GetEnvironmentVariable("WSM_GAME"),
            SteamGameDirectory.SteamRoots(), Console.In, Console.Out);
        if (selected == null) return 1;
        var local = Environment.GetEnvironmentVariable("LOCALAPPDATA");
        if (string.IsNullOrWhiteSpace(local) || !Path.IsPathFullyQualified(local))
            throw new ArgumentException("LOCALAPPDATA must be an absolute directory.");
        var home = Path.Combine(local, "WarhammerSurvivorsMultiplayer");
        var bridgeFile = Path.Combine(AppContext.BaseDirectory, "..", "bridge", "Wsm.Bridge.dll");
        args = ["play", selected, Path.Combine(home, "cache"), bridgeFile, Path.Combine(home, "logs")];
    }
    switch (args)
    {
        case ["play", var game, var cache, var bridge, var logs] when OperatingSystem.IsWindows():
            using (var cancellation = new CancellationTokenSource())
            {
                ConsoleCancelEventHandler handler = (_, e) => { e.Cancel = true; cancellation.Cancel(); };
                Console.CancelKeyPress += handler;
                try { return await InGameLaunch.RunAsync(game, cache, bridge, logs, cancellation.Token); }
                catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
                {
                    Console.Error.WriteLine("WSM: Launch canceled; owned child stopped.");
                    return 130;
                }
                finally { Console.CancelKeyPress -= handler; }
            }
        case ["inspect", var game]:
            Console.WriteLine(GameInspection.Inspect(game).ToJson());
            return 0;
        case ["probe", var runtime, var bridge, var logs] when OperatingSystem.IsWindows():
            return await RuntimeProbe.RunAsync(runtime, bridge, logs);
        case ["ui-probe", var runtime, var bridge, var logs] when OperatingSystem.IsWindows():
            return await RuntimeProbe.RunAsync(runtime, bridge, logs, mainThreadProbe: true);
        case ["coop-probe", var runtime, var bridge, var logs] when OperatingSystem.IsWindows():
            return await RuntimeProbe.RunAsync(runtime, bridge, logs, mainThreadProbe: true, enableLocalCoop: true);
        case ["prepare", var game, var destination]:
            var inspection = GameInspection.Inspect(game);
            Console.WriteLine(inspection.ToJson());
            RuntimePreparation.Prepare(game, destination);
            Console.WriteLine("Prepared isolated runtime: " + Path.GetFullPath(destination));
            Console.WriteLine("Original game files were not changed. This does not isolate the game's user-save directory.");
            return 0;
        default:
            Console.WriteLine("Warhammer Survivors Multiplayer launcher and development tools");
            Console.WriteLine("  play-installed (portable package; start via Start-WSM.cmd for the save warning)");
            Console.WriteLine("  play <game-directory> <cache-directory> <Wsm.Bridge.dll> <log-directory>\n  inspect <game-directory>\n  prepare <game-directory> <new-runtime-directory>\n  probe <runtime-directory> <Wsm.Bridge.dll> <log-directory>\n  ui-probe <runtime-directory> <Wsm.Bridge.dll> <log-directory>\n  coop-probe <runtime-directory> <Wsm.Bridge.dll> <log-directory>");
            return args.Length == 0 ? 0 : 2;
    }
}
catch (Exception ex) when (ex is IOException or InvalidDataException or ArgumentException or UnauthorizedAccessException or BadImageFormatException or System.ComponentModel.Win32Exception)
{
    Console.Error.WriteLine("WSM: " + ex.Message);
    return 1;
}

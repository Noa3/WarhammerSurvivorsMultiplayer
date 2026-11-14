using Wsm.Launcher;

try
{
    switch (args)
    {
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
            Console.WriteLine("Warhammer Survivors Multiplayer - development tools (not yet a playable co-op mod)");
            Console.WriteLine("  inspect <game-directory>\n  prepare <game-directory> <new-runtime-directory>\n  probe <runtime-directory> <Wsm.Bridge.dll> <log-directory>\n  ui-probe <runtime-directory> <Wsm.Bridge.dll> <log-directory>\n  coop-probe <runtime-directory> <Wsm.Bridge.dll> <log-directory>");
            return args.Length == 0 ? 0 : 2;
    }
}
catch (Exception ex) when (ex is IOException or InvalidDataException or ArgumentException or UnauthorizedAccessException or BadImageFormatException or System.ComponentModel.Win32Exception)
{
    Console.Error.WriteLine("WSM: " + ex.Message);
    return 1;
}

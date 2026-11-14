namespace Wsm.App;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        string? game = null, runtime = null;
        try
        {
            for (int i = 0; i < args.Length; i++)
            {
                if (args[i] == "--game" && game is null && i + 1 < args.Length) game = args[++i];
                else if (args[i] == "--runtime" && runtime is null && i + 1 < args.Length) runtime = args[++i];
                else throw new ArgumentException("Verwendung: Wsm.App.exe [--game <Spielordner> [--runtime <Cacheordner>]]");
            }
            if (game is null && runtime is not null) throw new ArgumentException("--runtime erfordert --game.");
        }
        catch (ArgumentException ex) { MessageBox.Show(ex.Message, "WSM", MessageBoxButtons.OK, MessageBoxIcon.Error); return; }
        Application.Run(new LobbyApplicationContext(game, runtime));
    }
}

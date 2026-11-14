namespace Wsm.OwnedWindowFixture;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        bool ignoreClose = args is ["--ignore-close"];
        using var form = new Form { Text = "WSM owned-window fixture", Size = new Size(1100, 900) };
        form.FormClosing += (_, e) => { if (ignoreClose) e.Cancel = true; };
        using var deadline = new System.Windows.Forms.Timer { Interval = 20000 };
        deadline.Tick += (_, _) => { ignoreClose = false; form.Close(); };
        deadline.Start();
        Application.Run(form);
    }
}

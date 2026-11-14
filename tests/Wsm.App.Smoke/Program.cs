using System.Net;
using System.Net.Sockets;
using Wsm.App;

internal static class Program
{
    private static int result = 1;
    [STAThread]
    private static int Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        var host = new LobbyForm(); var guest = new LobbyForm();
        host.Shown += async (_, _) => {
            try
            {
                if (args is ["--policy-ui"]) await PolicyUiRegression.Run();
                else if (args.Length == 0) { await Run(host, guest); Console.WriteLine("PASS: real WinForms controls, loopback HTTP, roster/loadouts/readiness/rules/start/leave/shutdown"); }
                else throw new ArgumentException("Supported filter: --policy-ui");
                result = 0;
            }
            catch (Exception ex) { Console.Error.WriteLine("FAIL: " + ex); }
            finally { guest.Close(); host.Close(); }
        };
        Application.Run(host);
        return result;
    }
    private static T C<T>(LobbyForm form, string name) where T : Control => (T)(form.Controls.Find(name, true).Single());
    private static void Click(LobbyForm f, string name) { var b = C<Button>(f, name); if (!b.Enabled) throw new Exception(name + " disabled"); b.PerformClick(); }
    private static async Task Wait(Func<bool> condition, string message)
    {
        var until = DateTime.UtcNow.AddSeconds(12);
        while (!condition()) { if (DateTime.UtcNow > until) throw new Exception("Timeout: " + message); await Task.Delay(50); }
    }
    private static ListViewItem Row(LobbyForm f, string name) => C<ListView>(f, "Roster").Items.Cast<ListViewItem>().Single(x => x.Text == name);
    private static async Task Run(LobbyForm host, LobbyForm guest)
    {
        await PolicyUiRegression.Run();
        await FeedbackRegression.Run();
        await LifecycleRegression.Run();
        var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start(); var port = ((IPEndPoint)listener.LocalEndpoint).Port; listener.Stop();
        C<TextBox>(host, "PlayerName").Text = "SmokeHost"; C<NumericUpDown>(host, "Port").Value = port;
        C<NumericUpDown>(host, "BotCount").Value = 1; Click(host, "Host");
        await Wait(() => C<Button>(host, "Leave").Enabled, "host create: " + C<Label>(host, "Status").Text);
        guest.Show(); C<TextBox>(guest, "PlayerName").Text = "SmokeGuest"; C<NumericUpDown>(guest, "Port").Value = port; Click(guest, "Join");
        await Wait(() => C<Button>(guest, "Leave").Enabled && C<ListView>(guest, "Roster").Items.Count == 3, "guest join");
        await Wait(() => C<ListView>(host, "Roster").Items.Count == 3, "host sees guest join");
        C<TextBox>(host, "HeroId").Text = "synthetic-host-hero"; C<TextBox>(host, "WeaponIds").Text = "synthetic-host-weapon"; Click(host, "Loadout");
        await Wait(() => Row(host, "SmokeHost").SubItems[2].Text == "synthetic-host-hero", "host loadout");
        C<TextBox>(guest, "HeroId").Text = "synthetic-guest-hero"; C<TextBox>(guest, "WeaponIds").Text = "synthetic-guest-weapon;synthetic-guest-second"; Click(guest, "Loadout");
        await Wait(() => Row(guest, "SmokeGuest").SubItems[2].Text == "synthetic-guest-hero" && C<Button>(guest, "Ready").Enabled, "guest loadout");
        await Wait(() => Row(host, "SmokeGuest").SubItems[3].Text == "synthetic-guest-weapon, synthetic-guest-second", "host sees guest");
        if (Row(host, "SmokeHost").SubItems[3].Text != "synthetic-host-weapon") throw new Exception("Host loadout overwritten");
        Click(host, "Ready"); Click(guest, "Ready");
        await Wait(() => Row(host, "SmokeHost").SubItems[4].Text == "Ja" && Row(host, "SmokeGuest").SubItems[4].Text == "Ja", "readiness");
        Click(guest, "Ready"); await Wait(() => Row(guest, "SmokeGuest").SubItems[4].Text == "Nein", "guest unready");
        Click(guest, "Ready"); await Wait(() => Row(guest, "SmokeGuest").SubItems[4].Text == "Ja", "guest ready again");
        await Wait(() => Row(host, "SmokeGuest").SubItems[4].Text == "Ja", "host sees guest ready again");
        Click(host, "Start"); await Wait(() => C<Label>(host, "Status").Text.Contains("Gameplay adapter unavailable"), "honest start rejection");
        C<NumericUpDown>(host, "BotCount").Value = 3; Click(host, "ApplyRules");
        await Wait(() => C<Label>(host, "Status").Text.Contains("Not enough slots"), "capacity conflict rejected");
        if (C<ListView>(host, "Roster").Items.Count != 3) throw new Exception("Capacity conflict removed players");
        C<NumericUpDown>(host, "BotCount").Value = 1;
        C<ComboBox>(host, "BotDifficulty").SelectedIndex = 2; C<ComboBox>(host, "DefeatRule").SelectedIndex = 1; Click(host, "ApplyRules");
        await Wait(() => Row(host, "SmokeHost").SubItems[4].Text == "Nein" && Row(host, "SmokeGuest").SubItems[4].Text == "Nein", "rule resets readiness");
        await Wait(() => C<ComboBox>(guest, "BotDifficulty").SelectedIndex == 2 && C<ComboBox>(guest, "DefeatRule").SelectedIndex == 1, "guest sees host rules");
        if (C<Button>(guest, "ApplyRules").Enabled || C<Button>(guest, "Start").Enabled) throw new Exception("Guest host-only controls enabled");
        await Task.Delay(TimeSpan.FromSeconds(17));
        if (C<ListView>(host, "Roster").Items.Count != 3 || Row(guest, "SmokeGuest").Text != "SmokeGuest") throw new Exception("Guest expired despite polling");
        Click(guest, "Leave"); await Wait(() => C<Button>(guest, "Join").Enabled, "guest leave");
        await Wait(() => C<ListView>(host, "Roster").Items.Count == 2, "host sees guest leave");
        guest.Close(); await Wait(() => guest.IsDisposed, "guest shutdown");
        Click(host, "Leave"); await Wait(() => C<Button>(host, "Host").Enabled && C<ListView>(host, "Roster").Items.Count == 0, "host shutdown");
        using var closeTest = new LobbyForm(); closeTest.Show(); C<NumericUpDown>(closeTest, "Port").Value = port;
        Click(closeTest, "Host"); await Wait(() => C<Button>(closeTest, "Leave").Enabled, "second host create");
        closeTest.Close(); await Wait(() => closeTest.IsDisposed, "connected host closes");
        var probe = new TcpListener(IPAddress.Loopback, port); probe.Start(); probe.Stop();
        var lostHost = new LobbyForm(); var lostGuest = new LobbyForm();
        try
        {
            lostHost.Show(); lostGuest.Show();
            C<NumericUpDown>(lostHost, "Port").Value = port; Click(lostHost, "Host");
            await Wait(() => C<Button>(lostHost, "Leave").Enabled, "loss fixture host");
            C<NumericUpDown>(lostGuest, "Port").Value = port; Click(lostGuest, "Join");
            await Wait(() => C<ListView>(lostGuest, "Roster").Items.Count == 2, "loss fixture join");
            lostHost.Close(); await Wait(() => lostHost.IsDisposed, "loss fixture host closed");
            await Wait(() => C<ListView>(lostGuest, "Roster").Items.Count == 0 && !C<Button>(lostGuest, "Ready").Enabled,
                "lost server must clear stale roster and disable readiness");
        }
        finally
        {
            lostHost.Close(); lostGuest.Close();
            await Wait(() => lostHost.IsDisposed && lostGuest.IsDisposed, "loss fixture teardown");
        }
    }
}

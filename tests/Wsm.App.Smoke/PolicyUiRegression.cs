using System.Net;
using System.Net.Sockets;
using Wsm.App;

internal static class PolicyUiRegression
{
    private static T Control<T>(LobbyForm form, string name) where T : System.Windows.Forms.Control
        => form.Controls.Find(name, true).SingleOrDefault() as T ?? throw new Exception("Missing policy UI control: " + name);
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Click(LobbyForm form, string name)
    {
        var button = Control<Button>(form, name);
        Check(button.Enabled, name + " must be enabled"); button.PerformClick();
    }
    private static async Task Wait(Func<bool> predicate, string description)
    {
        using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(12));
        try { while (!predicate()) await Task.Delay(25, deadline.Token); }
        catch (OperationCanceledException) { throw new Exception("Timeout: " + description); }
    }
    private static bool Ready(LobbyForm form) => Control<ListView>(form, "Roster").Items.Cast<ListViewItem>().All(r => r.SubItems[4].Text == "Ja");

    public static async Task Run()
    {
        var host = new LobbyForm(); var guest = new LobbyForm();
        try
        {
            host.Show(); guest.Show();
            var xp = Control<ComboBox>(host, "ExperienceSharing");
            var drops = Control<ComboBox>(host, "ExperienceDropLayout");
            var pickups = Control<ComboBox>(host, "PickupSharing");
            Check(xp.SelectedIndex == 0 && drops.SelectedIndex == 0 && pickups.SelectedIndex == 0, "Personal rewards at synchronized positions must be the defaults.");
            var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
            int port = ((IPEndPoint)listener.LocalEndpoint).Port; listener.Stop();
            Control<NumericUpDown>(host, "Port").Value = port;
            Click(host, "Host"); await Wait(() => Control<Button>(host, "Leave").Enabled, "host created");
            Control<NumericUpDown>(guest, "Port").Value = port;
            Click(guest, "Join"); await Wait(() => Control<ListView>(guest, "Roster").Items.Count == 2, "guest admitted");
            foreach (var form in new[] { host, guest })
            {
                Control<TextBox>(form, "HeroId").Text = "fixture-hero";
                Control<TextBox>(form, "WeaponIds").Text = "fixture-weapon";
                Click(form, "Loadout");
                await Wait(() => Control<Label>(form, "Status").Text.StartsWith("Auswahl übernommen"), "selection saved");
                Click(form, "Ready"); await Wait(() => Control<Button>(form, "Ready").Text == "Nicht mehr bereit", "own readiness");
            }
            await Wait(() => Ready(host) && Ready(guest), "all ready");
            var confirmed = Control<Label>(host, "AppliedRules").Text;
            Check(confirmed.Contains("Botplätze: 0 (Normal)") && confirmed.Contains("Niederlage: Alle Spieler besiegt"),
                "The confirmed summary must include the actual bot count, difficulty and defeat rule.");
            Control<NumericUpDown>(host, "BotCount").Value = 1;
            Control<ComboBox>(host, "BotDifficulty").SelectedIndex = 2;
            Control<ComboBox>(host, "DefeatRule").SelectedIndex = 1;
            drops.SelectedIndex = 1;
            xp.SelectedIndex = 1; pickups.SelectedIndex = 1;
            Check(Control<Label>(host, "AppliedRules").Text == confirmed, "Drafts must not replace confirmed bot/defeat rules.");
            Check(drops.SelectedIndex == 0 && !drops.Enabled, "Team XP must force synchronized drops.");
            Check(Control<ComboBox>(guest, "ExperienceSharing").SelectedIndex == 0 && Ready(host), "Unapplied drafts must not mutate the lobby.");
            Click(host, "ApplyRules");
            await Wait(() => Control<ComboBox>(guest, "ExperienceSharing").SelectedIndex == 1 && Control<ComboBox>(guest, "PickupSharing").SelectedIndex == 1, "guest sees applied policy");
            Check(!Ready(host) && !Ready(guest), "Applied policy invalidates readiness.");
            Check(Control<Label>(host, "AppliedRules").Text.Contains("Botplätze: 1 (Schwer)") &&
                Control<Label>(guest, "AppliedRules").Text.Contains("Niederlage: Ein Spieler besiegt"), "Applied bot/defeat rules must be confirmed on both peers.");
            foreach (var name in new[] { "ExperienceSharing", "ExperienceDropLayout", "PickupSharing" })
                Check(!Control<ComboBox>(guest, name).Enabled, "Guest must not edit " + name);
            Check(Control<Label>(guest, "AppliedRules").Text.Contains("Team-XP") && Control<Label>(guest, "AppliedRules").Text.Contains("persönlich"), "Applied rule summary must explain team XP and independent choices.");
            var listBottom = Control<ListView>(host, "Roster").RectangleToScreen(Control<ListView>(host, "Roster").ClientRectangle).Bottom;
            var statusTop = Control<Label>(host, "Status").PointToScreen(Point.Empty).Y;
            Check(listBottom <= statusTop, "The status must not overlap the player list.");
            var capture = Environment.GetEnvironmentVariable("WSM_UI_SCREENSHOT_DIRECTORY");
            if (!string.IsNullOrWhiteSpace(capture))
            {
                Directory.CreateDirectory(capture);
                foreach (var pair in new[] { (Form: host, Name: "host"), (Form: guest, Name: "guest") })
                {
                    using var bitmap = new Bitmap(pair.Form.Width, pair.Form.Height);
                    pair.Form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, pair.Form.Size));
                    bitmap.Save(Path.Combine(capture, pair.Name + "-policy.png"), System.Drawing.Imaging.ImageFormat.Png);
                }
            }
            var originalSize = host.Size;
            host.Size = host.MinimumSize;
            await Task.Delay(50);
            var panel = Control<Panel>(host, "LobbyScroll");
            var status = Control<Label>(host, "Status");
            panel.ScrollControlIntoView(status);
            var visible = panel.RectangleToScreen(panel.ClientRectangle);
            var bounds = status.RectangleToScreen(status.ClientRectangle);
            Check(visible.Contains(bounds), $"Status must be reachable at the minimum window size: view={visible}; status={bounds}.");
            Check(Control<ListView>(host, "Roster").RectangleToScreen(Control<ListView>(host, "Roster").ClientRectangle).Bottom <= bounds.Top,
                "Minimum window layout must not overlap the roster and status.");
            if (!string.IsNullOrWhiteSpace(capture))
            {
                using var bitmap = new Bitmap(host.Width, host.Height);
                host.DrawToBitmap(bitmap, new Rectangle(Point.Empty, host.Size));
                bitmap.Save(Path.Combine(capture, "host-minimum-policy.png"), System.Drawing.Imaging.ImageFormat.Png);
            }
            host.Size = originalSize;
            xp.SelectedIndex = 0;
            Check(drops.Enabled, "Personal XP must allow personal layouts again.");
            drops.SelectedIndex = 1; pickups.SelectedIndex = 0;
            Click(host, "ApplyRules");
            await Wait(() => Control<ComboBox>(guest, "ExperienceSharing").SelectedIndex == 0 && Control<ComboBox>(guest, "ExperienceDropLayout").SelectedIndex == 1 && Control<ComboBox>(guest, "PickupSharing").SelectedIndex == 0, "personal policy restored");
            Console.WriteLine("PASS real policy UI: host apply, guest read-only, readiness reset, team constraint, personal defaults");
        }
        finally
        {
            guest.Close(); host.Close();
            await Wait(() => guest.IsDisposed && host.IsDisposed, "policy windows and lobby disposed");
        }
    }
}

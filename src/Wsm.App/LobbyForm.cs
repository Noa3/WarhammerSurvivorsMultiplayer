using System.Net;
using System.Net.Sockets;
using Wsm.Lobby;
using Wsm.Contracts;

namespace Wsm.App;

public sealed class LobbyForm : Form
{
    private readonly TextBox playerName = Input("PlayerName", "Spieler");
    private readonly TextBox ip = Input("IpAddress", "127.0.0.1");
    private readonly NumericUpDown port = Number("Port", 1, 65535, 7780);
    private readonly ComboBox bind = Choice("BindAddress", "Nur dieser PC (Loopback)", "Alle IPv4-Netzwerkschnittstellen (LAN)");
    private readonly TextBox hero = Input("HeroId", "");
    private readonly TextBox weapons = Input("WeaponIds", "");
    private readonly NumericUpDown bots = Number("BotCount", 0, 3, 0);
    private readonly ComboBox difficulty = Choice("BotDifficulty", "Einfach", "Normal", "Schwer");
    private readonly ComboBox defeat = Choice("DefeatRule", "Alle Spieler besiegt", "Ein Spieler besiegt");
    private readonly ComboBox experience = Choice("ExperienceSharing", "Persönliche XP", "Team-XP – voller Betrag für jeden");
    private readonly ComboBox dropLayout = Choice("ExperienceDropLayout", "Gleiche Positionen", "Persönliche Positionen");
    private readonly ComboBox pickupSharing = Choice("PickupSharing", "Persönlich", "Münzen und Verbrauchsgegenstände fürs Team");
    private readonly Label appliedRules = Label("AppliedRules", "Noch keine Host-Regeln bestätigt.");
    private readonly Button host = Button("Host", "Lobby erstellen");
    private readonly Button join = Button("Join", "Beitreten");
    private readonly Button leave = Button("Leave", "Verlassen / trennen");
    private readonly Button loadout = Button("Loadout", "Auswahl übernehmen");
    private readonly Button ready = Button("Ready", "Bereit");
    private readonly Button rules = Button("ApplyRules", "Host-Regeln übernehmen");
    private readonly Button start = Button("Start", "Start anfragen");
    private readonly Label status = Label("Status", "Nicht verbunden.");
    private readonly Label address = Label("Address", "");
    private readonly ListView roster = new() { Name = "Roster", AccessibleName = "Lobby-Spielerliste", Dock = DockStyle.Fill, View = View.Details, FullRowSelect = true, GridLines = true, MinimumSize = new Size(400, 110) };
    private readonly SemaphoreSlim gate = new(1, 1);
    private CancellationTokenSource lifetime = new();
    private LobbyServer? server;
    private LobbyClient? client;
    private Task? polling;
    private LobbySnapshot? snapshot;
    private bool closing;
    private bool busy;

    public LobbyForm()
    {
        Text = "Warhammer Survivors – Lobby"; Name = "LobbyForm"; MinimumSize = new Size(760, 680); Size = new Size(1000, 850); StartPosition = FormStartPosition.CenterScreen;
        difficulty.SelectedIndex = 1;
        hero.PlaceholderText = "Helden-ID eingeben"; weapons.PlaceholderText = "1–8 Waffen-IDs, durch Komma oder Semikolon getrennt";
        roster.Columns.Add("Name", 140); roster.Columns.Add("Typ", 80); roster.Columns.Add("Held-ID", 170); roster.Columns.Add("Waffen-IDs", 260); roster.Columns.Add("Bereit", 75);
        var scroll = new Panel { Name = "LobbyScroll", Dock = DockStyle.Fill, AutoScroll = true };
        var layout = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, ColumnCount = 2, RowCount = 0, Padding = new Padding(12) };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 205)); layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        Controls.Add(scroll); scroll.Controls.Add(layout);
        AddWide(layout, Label("Development", "Entwicklungsstand: Spielanbindung fehlt. Start ist derzeit nicht möglich."));
        AddWide(layout, Label("Security", "Nur vertrauenswürdiges LAN/VPN: HTTP unverschlüsselt, kein Passwort; nicht öffentlich freigeben. Keine Firewall-Änderungen."));
        AddRow(layout, "Spielername", playerName); AddRow(layout, "Host-Bindung", bind); AddRow(layout, "IP-Adresse des Hosts", ip); AddRow(layout, "Port", port);
        AddWide(layout, Flow(host, join, leave)); AddWide(layout, address);
        AddWide(layout, Label("IdsWarning", "Helden- und Waffen-IDs sind noch nicht gegen Spiel geprüft. Keine Spielauswahl verfügbar."));
        AddRow(layout, "Meine Helden-ID", hero); AddRow(layout, "Meine Waffen-IDs", weapons); AddWide(layout, Flow(loadout, ready));
        AddRow(layout, "Bots (max. 4 Plätze gesamt)", bots); AddRow(layout, "Bot-Schwierigkeit", difficulty); AddRow(layout, "Niederlage", defeat);
        AddRow(layout, "Erfahrungspunkte", experience); AddRow(layout, "XP-Drop-Positionen", dropLayout); AddRow(layout, "Pickups", pickupSharing);
        AddWide(layout, Label("PolicyHelp", "Team-XP zählt einen Drop einmal und gibt jedem den vollen Betrag. Persönliche XP: jeder sammelt selbst. Waffen, Artefakte und Upgrade-Auswahl bleiben persönlich. Regeln werden erst mit der Spielanbindung im Spiel wirksam."));
        AddWide(layout, Flow(rules, start)); AddWide(layout, appliedRules); AddWide(layout, Label("RosterLabel", "Lobby-Teilnehmer"));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 140)); layout.Controls.Add(roster, 0, layout.RowCount); layout.SetColumnSpan(roster, 2); layout.RowCount++;
        AddWide(layout, status);
        host.Click += async (_, _) => await ExecuteAsync(CreateAsync);
        join.Click += async (_, _) => await ExecuteAsync(JoinAsync, connecting: true);
        leave.Click += async (_, _) => await ExecuteAsync(DisconnectAsync);
        loadout.Click += async (_, _) => await ExecuteAsync(SetLoadoutAsync);
        ready.Click += async (_, _) => await ExecuteAsync(SetReadyAsync);
        rules.Click += async (_, _) => await ExecuteAsync(ApplyRulesAsync);
        start.Click += async (_, _) => await ExecuteAsync(StartAsync);
        experience.SelectedIndexChanged += (_, _) =>
        {
            if (experience.SelectedIndex == (int)ExperienceSharing.Team) dropLayout.SelectedIndex = (int)ExperienceDropLayout.Synchronized;
            UpdateControls();
        };
        FormClosing += OnClosing;
        UpdateControls();
    }

    private static TextBox Input(string name, string value) => new() { Name = name, AccessibleName = name, Text = value, Dock = DockStyle.Fill };
    private static NumericUpDown Number(string name, int min, int max, int value) => new() { Name = name, AccessibleName = name, Minimum = min, Maximum = max, Value = value, Dock = DockStyle.Fill };
    private static ComboBox Choice(string name, params string[] values) { var c = new ComboBox { Name = name, AccessibleName = name, DropDownStyle = ComboBoxStyle.DropDownList, Dock = DockStyle.Fill }; c.Items.AddRange(values); c.SelectedIndex = 0; return c; }
    private static Button Button(string name, string text) => new() { Name = name, AccessibleName = text, Text = text, AutoSize = true };
    private static Label Label(string name, string text) => new() { Name = name, AccessibleName = name, Text = text, AutoSize = true, MaximumSize = new Size(950, 0), Margin = new Padding(3, 6, 3, 6) };
    private static FlowLayoutPanel Flow(params Control[] controls) { var f = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, WrapContents = true }; f.Controls.AddRange(controls); return f; }
    private static void AddRow(TableLayoutPanel t, string label, Control control) { int row = t.RowCount++; t.RowStyles.Add(new RowStyle(SizeType.AutoSize)); t.Controls.Add(Label("Label" + control.Name, label), 0, row); t.Controls.Add(control, 1, row); }
    private static void AddWide(TableLayoutPanel t, Control control) { int row = t.RowCount++; t.RowStyles.Add(new RowStyle(SizeType.AutoSize)); t.Controls.Add(control, 0, row); t.SetColumnSpan(control, 2); }

    private LobbyRules CurrentRules() => new((int)bots.Value, (BotDifficulty)difficulty.SelectedIndex, (DefeatRule)defeat.SelectedIndex)
    {
        Policy = (snapshot?.Rules.Policy ?? new CoopPolicy()) with
        {
            ExperienceSharing = (ExperienceSharing)experience.SelectedIndex,
            ExperienceDropLayout = (ExperienceDropLayout)dropLayout.SelectedIndex,
            PickupSharing = (PickupSharing)pickupSharing.SelectedIndex
        }
    };
    private async Task CreateAsync(CancellationToken ct)
    {
        server = await LobbyServer.StartAsync(bind.SelectedIndex == 0 ? IPAddress.Loopback : IPAddress.Any, (int)port.Value, playerName.Text.Trim(), CurrentRules(), cancellationToken: ct);
        address.Text = bind.SelectedIndex == 0 ? $"Nur lokal: 127.0.0.1:{server.Address.Port}" : $"LAN-IPv4-Vorschläge: {await LocalAddressesAsync(ct)} · Port {server.Address.Port} (Erreichbarkeit nicht geprüft)";
        status.Text = "Lobby erstellt."; RefreshSnapshot(server.Snapshot); StartPolling();
    }
    private static async Task<string> LocalAddressesAsync(CancellationToken ct)
    {
        try { var addresses = (await Dns.GetHostAddressesAsync(Dns.GetHostName(), ct)).Where(a => a.AddressFamily == AddressFamily.InterNetwork && !IPAddress.IsLoopback(a)).Select(a => a.ToString()).Distinct().ToArray(); return addresses.Length == 0 ? "keine gefunden" : string.Join(", ", addresses); }
        catch (SocketException) { return "nicht ermittelbar"; }
    }
    private async Task JoinAsync(CancellationToken ct)
    {
        if (!IPAddress.TryParse(ip.Text.Trim(), out var parsed) || parsed.AddressFamily != AddressFamily.InterNetwork) throw new ArgumentException("Bitte eine IPv4-Adresse ohne Port oder Pfad eingeben.");
        address.Text = $"Verbindungsziel: {parsed}:{port.Value}";
        status.Text = "Verbindung wird aufgebaut …";
        var joined = await LobbyClient.JoinAsync(new UriBuilder("http", parsed.ToString(), (int)port.Value).Uri, playerName.Text.Trim(), ct);
        LobbySnapshot initial;
        try { initial = await joined.GetStateAsync(ct); }
        catch (Exception failure)
        {
            try { await joined.DisposeAsync(); }
            catch (Exception cleanup) { throw new AggregateException("Join failed and cleanup failed.", failure, cleanup); }
            throw;
        }
        client = joined;
        address.Text = $"Verbunden mit {parsed}:{port.Value}"; status.Text = "Beigetreten.";
        RefreshSnapshot(initial); StartPolling();
    }
    private async Task SetLoadoutAsync(CancellationToken ct)
    {
        var selection = weapons.Text.Split([',', ';'], StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        if (server is not null) server.SetHostLoadout(hero.Text.Trim(), selection);
        else if (client is not null) await client.SetLoadoutAsync(hero.Text.Trim(), selection, ct);
        status.Text = "Auswahl übernommen; Bereitschaft wurde bei Änderungen zurückgesetzt."; await FetchAsync(ct);
    }
    private async Task SetReadyAsync(CancellationToken ct)
    {
        var mine = snapshot?.Slots.FirstOrDefault(s => s.PlayerId == (client?.PlayerId ?? server?.Snapshot.Slots[0].PlayerId));
        if (mine is null) throw new InvalidOperationException("Eigener Lobby-Platz nicht verfügbar.");
        if (server is not null) server.SetHostReady(!mine.Ready); else if (client is not null) await client.SetReadyAsync(!mine.Ready, ct);
        await FetchAsync(ct); status.Text = mine.Ready ? "Nicht mehr bereit." : "Bereit.";
    }
    private async Task ApplyRulesAsync(CancellationToken ct) { server!.Configure(CurrentRules()); await FetchAsync(ct); status.Text = "Host-Regeln übernommen; Bereitschaft kann zurückgesetzt sein."; }
    private Task StartAsync(CancellationToken ct) { var result = server!.TryStart(); status.Text = result.Started ? "Start gemeldet." : "Start abgelehnt: " + result.Reason; return Task.CompletedTask; }
    private async Task FetchAsync(CancellationToken ct) { if (server is not null) RefreshSnapshot(server.Snapshot); else if (client is not null) RefreshSnapshot(await client.GetStateAsync(ct)); }
    private void RefreshSnapshot(LobbySnapshot value)
    {
        snapshot = value; roster.BeginUpdate(); roster.Items.Clear();
        foreach (var s in value.Slots) { var item = new ListViewItem(s.Name); item.SubItems.Add(s.Kind.ToString()); item.SubItems.Add(s.HeroId ?? "–"); item.SubItems.Add(string.Join(", ", s.WeaponIds)); item.SubItems.Add(s.Ready ? "Ja" : "Nein"); roster.Items.Add(item); }
        roster.EndUpdate();
        var policy = value.Rules.Policy;
        if (server is null)
        {
            bots.Value = value.Rules.BotCount; difficulty.SelectedIndex = (int)value.Rules.BotDifficulty; defeat.SelectedIndex = (int)value.Rules.DefeatRule;
            experience.SelectedIndex = (int)policy.ExperienceSharing;
            dropLayout.SelectedIndex = (int)policy.ExperienceDropLayout;
            pickupSharing.SelectedIndex = (int)policy.PickupSharing;
        }
        appliedRules.Text = $"Bestätigte Regeln: Botplätze: {value.Rules.BotCount} ({difficulty.Items[(int)value.Rules.BotDifficulty]}); Niederlage: {defeat.Items[(int)value.Rules.DefeatRule]}; " +
            (policy.ExperienceSharing == ExperienceSharing.Team ? "Team-XP (voller Betrag pro Spieler)" : "persönliche XP") + "; " +
            (policy.ExperienceDropLayout == ExperienceDropLayout.Synchronized ? "gleiche XP-Drop-Positionen" : "persönliche XP-Drop-Positionen") + "; " +
            (policy.PickupSharing == PickupSharing.Team ? "Münzen/Verbrauchsgegenstände geteilt" : "Pickups persönlich") +
            ". Gegner und HP gemeinsam; Waffen, Artefakte und Upgrade-Auswahl persönlich.";
        UpdateControls();
    }
    private void UpdateControls()
    {
        bool connected = server is not null || client is not null; bool can = !busy && !closing;
        host.Enabled = join.Enabled = can && !connected; leave.Enabled = can && connected;
        loadout.Enabled = ready.Enabled = can && connected && snapshot is not null; rules.Enabled = start.Enabled = can && server is not null && snapshot is not null;
        bind.Enabled = ip.Enabled = port.Enabled = playerName.Enabled = can && !connected;
        bots.Enabled = difficulty.Enabled = defeat.Enabled = experience.Enabled = pickupSharing.Enabled = can && client is null && snapshot?.Started != true;
        dropLayout.Enabled = experience.Enabled && experience.SelectedIndex == (int)ExperienceSharing.Personal;
        var ownId = client?.PlayerId ?? (server is not null ? server.Snapshot.Slots[0].PlayerId : Guid.Empty);
        ready.Text = snapshot?.Slots.FirstOrDefault(s => s.PlayerId == ownId && s.Kind != SlotKind.Bot)?.Ready == true ? "Nicht mehr bereit" : "Bereit";
    }
    private void StartPolling() { polling = PollAsync(lifetime.Token); }
    private async Task PollAsync(CancellationToken ct)
    {
        bool failed = false;
        try
        {
            using var timer = new PeriodicTimer(TimeSpan.FromSeconds(1.5));
            while (await timer.WaitForNextTickAsync(ct))
            {
                try
                {
                    await gate.WaitAsync(ct);
                    try
                    {
                        using var request = CancellationTokenSource.CreateLinkedTokenSource(ct);
                        request.CancelAfter(TimeSpan.FromSeconds(4));
                        await FetchAsync(request.Token);
                    }
                    finally { gate.Release(); }
                    if (failed) { status.Text = "Verbindung wiederhergestellt."; failed = false; }
                }
                catch (OperationCanceledException) when (ct.IsCancellationRequested) { throw; }
                catch (Exception ex)
                {
                    failed = true;
                    // A previous snapshot must not advertise current readiness after connection loss.
                    snapshot = null;
                    appliedRules.Text = "Host-Regeln nicht aktuell – Verbindung unterbrochen.";
                    roster.Items.Clear();
                    UpdateControls();
                    status.Text = "Aktualisierung fehlgeschlagen: " + ConnectionFeedback.Describe(ex) + " – erneuter Versuch; ggf. trennen und neu beitreten.";
                }
            }
        }
        catch (OperationCanceledException) when (ct.IsCancellationRequested) { }
    }
    private async Task ExecuteAsync(Func<CancellationToken, Task> action, bool connecting = false)
    {
        if (busy || closing) return; busy = true; UpdateControls();
        try { await gate.WaitAsync(lifetime.Token); try { await action(lifetime.Token); } finally { gate.Release(); } }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested) { }
        catch (Exception ex) { status.Text = (connecting ? $"{ip.Text.Trim()}:{port.Value}: " : "") + ConnectionFeedback.Describe(ex); }
        finally { busy = false; if (!closing) UpdateControls(); }
    }
    private async Task DisconnectAsync(CancellationToken ct)
    {
        // Called with the gate held; cancel polling but do not await it until the gate is released.
        lifetime.Cancel();
        Exception? failure = null;
        if (client is not null) { var old = client; client = null; try { await old.DisposeAsync(); } catch (Exception ex) { failure = ex; } }
        if (server is not null) { var old = server; server = null; try { await old.DisposeAsync(); } catch (Exception ex) { failure ??= ex; } }
        snapshot = null; roster.Items.Clear(); address.Text = ""; appliedRules.Text = "Noch keine Host-Regeln bestätigt."; status.Text = failure is null ? "Nicht verbunden." : "Getrennt; Aufräumen fehlgeschlagen: " + failure.Message;
        lifetime.Dispose(); lifetime = new CancellationTokenSource();
    }
    private async void OnClosing(object? sender, FormClosingEventArgs e)
    {
        e.Cancel = true;
        if (closing) return;
        closing = true; lifetime.Cancel(); UpdateControls();
        try { if (polling is not null) await polling; await gate.WaitAsync(); try { await DisconnectAsync(CancellationToken.None); } finally { gate.Release(); } }
        catch (Exception ex) { status.Text = "Fehler beim Schließen: " + ex.Message; }
        finally { lifetime.Dispose(); gate.Dispose(); FormClosing -= OnClosing; Close(); }
    }
}

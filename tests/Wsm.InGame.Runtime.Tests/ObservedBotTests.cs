using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Wsm.Core;
using Wsm.InGame.Runtime;

internal static class ObservedBotTests
{
    private static JsonObject Frame() => JsonNode.Parse("""
        {"kind":"botFrame","requestId":1,"bot":{"run":1,"slot":0,"tick":1,"primary":"host","phase":"playing",
         "actor":{"id":"ally","x":0,"y":0,"alive":true},"weaponRange":5,"maxStep":1,
         "enemies":[{"id":"orc","x":19,"y":0,"alive":true}]}}
        """)!.AsObject();
    private static int Submit(IngameSessionController host, JsonObject frame) => host.SubmitGameplay(host.GameplayEpoch, Encoding.UTF8.GetBytes(frame.ToJsonString()));
    private static async Task<JsonObject> Result(IngameSessionController host, JsonObject frame)
    {
        int code = Submit(host, frame);
        if (code != 0) throw new Exception($"valid observed bot frame refused: {code}");
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        var buffer = new byte[8192];
        while (true)
        {
            timeout.Token.ThrowIfCancellationRequested();
            int n = host.PollGameplay(buffer);
            if (n == 0) { await Task.Delay(3, timeout.Token); continue; }
            if (n < 0) throw new Exception($"poll failed: {n}");
            var item = JsonNode.Parse(buffer.AsSpan(0,n))!.AsObject();
            if ((string?)item["kind"] != "submission") continue;
            if ((long?)item["requestId"] != (long?)frame["requestId"]) throw new Exception("wrong frame correlation");
            return item;
        }
    }
    private static void Check(bool value, string why) { if (!value) throw new Exception(why); }
    public static async Task Decisions()
    {
        await using var host = new IngameSessionController(new SessionSettings(1, DeathMode.AllDead, 2, BotDifficulty.Hard));
        Check(await host.HostAsync(0, false) == TransitionResult.Started, "host setup");
        var frame = Frame();
        var body = frame["bot"]!;
        Check(Submit(host, frame) == NativeExports.InvalidArgument, "multiple bot slots require the complete team before first decision");
        body["team"] = JsonNode.Parse("""["ally","other"]""");
        body["enemies"]![0]!["id"] = "other";
        Check(Submit(host, frame) == NativeExports.InvalidArgument, "unseen friendly cannot be the first attack target");
        body["enemies"]![0]!["id"] = "orc";
        JsonNode command = (await Result(host, frame))["botDecision"] ?? throw new Exception("missing actual command");
        Check((double?)command["x"] == 1 && (double?)command["y"] == 0 && (string?)command["actor"] == "ally", "observe first actor, move right");
        body["tick"] = 2; body["actor"]!["x"] = 18;
        command = (await Result(host, frame))["botDecision"]!;
        Check((double?)command["x"] < 0 && (string?)command["attackTarget"] == "orc", "use observed position not last prediction");
        Check((await Result(host, frame))["error"] is not null, "replayed tick produced another command");
        body["phase"] = "upgrade"; body["tick"] = 3;
        body["offer"] = JsonNode.Parse("""{"owner":"ally","level":2,"choices":["blade","boots"]}""");
        command = (await Result(host, frame))["botDecision"]!;
        string? choice = (string?)command["choice"];
        Check(choice is "blade" or "boots" && (float?)command["x"] == 0 && command["attackTarget"] is null, "personal modal choice is offered; no movement");
        body["tick"] = 4; body["offer"]!["choices"] = JsonNode.Parse("""["boots","blade"]""");
        Check((string?)(await Result(host, frame))["botDecision"]?["choice"] == choice, "same modal remains stable across a reorder");
        body["tick"] = 5; body["offer"]!["owner"] = "host";
        command = (await Result(host, frame))["botDecision"]!;
        Check(command["choice"] is null && (float?)command["x"] == 0, "never select host upgrade");
        body.AsObject().Remove("offer"); body["phase"] = "playing"; body["actor"]!["alive"] = false; body["tick"] = 6;
        command = (await Result(host, frame))["botDecision"]!;
        Check((float?)command["x"] == 0 && command["attackTarget"] is null, "dead actor must stay neutral");
        body["actor"]!["alive"] = true; body["slot"] = 1; body["tick"] = 1;
        Check(Submit(host, frame) == NativeExports.InvalidArgument, "one actor cannot occupy both slots");
        body["actor"]!["id"] = "other";
        Check((string?)(await Result(host, frame))["botDecision"]?["actor"] == "other", "two distinct bot actors");
        body["tick"] = 2; body["team"] = JsonNode.Parse("""["impostor","other"]""");
        Check((await Result(host, frame))["error"] is not null, "cannot rewrite a teammate during this run");
        body["run"] = 2; body["slot"] = 0;
        body["team"] = JsonNode.Parse("""["other","ally"]""");
        Check((long?)(await Result(host, frame))["botDecision"]?["run"] == 2, "fresh stage accepts new bindings");
        body["run"] = 1; body["tick"] = 99;
        Check((await Result(host, frame))["error"] is not null, "late frame from previous run");
    }
    public static async Task Guards()
    {
        var frame = Frame();
        await using var host = new IngameSessionController();
        Check(await host.HostAsync(0, false) == TransitionResult.Started, "host setup");
        Check(Submit(host,frame) == NativeExports.InvalidArgument, "bot slots disabled");
        var oldEpoch = host.GameplayEpoch;
        await host.LeaveAsync();
        Check(await host.HostAsync(0, false, default, null, new(1,DeathMode.AllDead,1,BotDifficulty.Easy)) == TransitionResult.Started,"rehost");
        Check(host.SubmitGameplay(oldEpoch, Encoding.UTF8.GetBytes(frame.ToJsonString())) == NativeExports.Unavailable, "old epoch");
        await using var guest = new IngameSessionController();
        Check(await guest.JoinAsync("127.0.0.1",host.Status.BoundPort) == TransitionResult.Started,"guest setup");
        Check(Submit(guest,frame) == NativeExports.InvalidArgument,"guest cannot plan host bots");
        var body = frame["bot"]!;
        body["slot"] = 1; Check(Submit(host,frame) == NativeExports.InvalidArgument,"unconfigured slot"); body["slot"] = 0;
        body["actor"]!["id"] = "host"; Check(Submit(host,frame) == NativeExports.InvalidArgument,"primary cannot be bot");
        body["actor"]!["id"] = "ally"; body["enemies"]![0]!.AsObject().Remove("x");
        Check(Submit(host,frame) == NativeExports.InvalidArgument,"missing observation must not default to origin");
        frame = Frame();
        var command = (await Result(host,frame))["botDecision"]!;
        Check(Math.Abs((double)command["x"]! - .4) < .0001,"use Easy from the active host");
    }
}

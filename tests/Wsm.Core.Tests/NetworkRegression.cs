using System.Buffers.Binary;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Wsm.Core;

internal static class NetworkRegression
{
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    public static async Task Run()
    {
        await SendContextRegression.Run();
        await InterruptedWrite();
        await ProtocolClients();
        await SlowReaderAndOversize();
    }
    private static async Task InterruptedWrite()
    {
        var stream = new ControlledStream();
        bool closed = false;
        using var peer = new Peer(stream, () => closed = true);
        var first = peer.SendAsync(new Message("one"), CancellationToken.None);
        await stream.Entered.Task.WaitAsync(TimeSpan.FromSeconds(3));
        using var canceled = new CancellationTokenSource();
        var waiting = peer.SendAsync(new Message("two"), canceled.Token);
        canceled.Cancel();
        try { await waiting; throw new Exception("Waiting write not canceled"); } catch (OperationCanceledException) { }
        Check(!closed, "Canceled lock wait closed peer");
        stream.Release.TrySetResult();
        await first;
        Check(!closed, "Successful first frame closed peer");
        stream.FailNextPayload = true;
        try { await peer.SendAsync(new Message("three"), CancellationToken.None); throw new Exception("Partial write did not fail"); }
        catch (OperationCanceledException) { }
        Check(stream.PartialBytesWritten > 0 && closed, "Partial-write cancellation did not close peer");
        try { await peer.SendAsync(new Message("four"), CancellationToken.None); throw new Exception("Poisoned peer reused"); }
        catch (ObjectDisposedException) { }
        Console.WriteLine("PASS deterministic partial-write and canceled lock wait");
    }
    private sealed class ControlledStream : Stream
    {
        public readonly TaskCompletionSource Entered = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public readonly TaskCompletionSource Release = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private int writes;
        public bool FailNextPayload;
        private readonly MemoryStream sink = new();
        public int PartialBytesWritten;
        public override async ValueTask WriteAsync(ReadOnlyMemory<byte> buffer, CancellationToken cancellationToken = default)
        {
            int n = Interlocked.Increment(ref writes);
            if (n == 2) { Entered.TrySetResult(); await Release.Task; }
            if (FailNextPayload && n == 4) { PartialBytesWritten = buffer.Length / 2; sink.Write(buffer.Span[..PartialBytesWritten]); throw new OperationCanceledException("Injected after header and partial payload"); }
            sink.Write(buffer.Span);
        }
        public override bool CanRead => false; public override bool CanSeek => false; public override bool CanWrite => true;
        public override long Length => throw new NotSupportedException(); public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
        public override void Flush() => throw new NotSupportedException();
        public override int Read(byte[] buffer, int offset, int count) => throw new NotSupportedException();
        public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
        public override void SetLength(long value) => throw new NotSupportedException();
        public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
    }
    private static async Task Send(Stream stream, string json, CancellationToken token)
    {
        var bytes = Encoding.UTF8.GetBytes(json); var header = new byte[4]; BinaryPrimitives.WriteInt32BigEndian(header, bytes.Length);
        await stream.WriteAsync(header, token); await stream.WriteAsync(bytes, token);
    }
    private static async Task<string> Read(Stream stream, CancellationToken token)
    {
        var header = new byte[4]; await stream.ReadExactlyAsync(header, token);
        var data = new byte[BinaryPrimitives.ReadInt32BigEndian(header)]; await stream.ReadExactlyAsync(data, token); return Encoding.UTF8.GetString(data);
    }
    private static async Task ProtocolClients()
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        var host = new SessionHost(new SessionSettings(2, DeathMode.AllDead, 0, BotDifficulty.Normal), "x"); host.Start();
        foreach (var payload in new[] { "{", "{\"Type\":\"snapshot\"}" })
        {
            using var tcp = new TcpClient(); await tcp.ConnectAsync(IPAddress.Loopback, host.Port, timeout.Token);
            var stream = tcp.GetStream();
            await Send(stream, $"{{\"Type\":\"join\",\"Version\":{SessionHost.ProtocolVersion},\"Password\":\"x\",\"DisplayName\":\"Player\"}}", timeout.Token);
            await Read(stream, timeout.Token); var id = await host.Joined.ReadAsync(timeout.Token); await Read(stream, timeout.Token);
            await Send(stream, payload, timeout.Token);
            Check(await stream.ReadAsync(new byte[1], timeout.Token) == 0, "Invalid client was not closed");
            Check(await host.Disconnected.ReadAsync(timeout.Token) == id, "Invalid client did not disconnect");
        }
        using (var tcp = new TcpClient())
        {
            await tcp.ConnectAsync(IPAddress.Loopback, host.Port, timeout.Token);
            var stream = tcp.GetStream();
            await Send(stream, $"{{\"Type\":\"join\",\"Version\":{SessionHost.ProtocolVersion},\"Password\":\"x\",\"DisplayName\":\"Player\"}}", timeout.Token);
            var welcome = await Read(stream, timeout.Token);
            var id = await host.Joined.ReadAsync(timeout.Token);
            await Read(stream, timeout.Token); // initial roster
            for (int i = 0; i < 129; i++) await Send(stream, $"{{\"Type\":\"input\",\"Input\":{{\"PlayerId\":\"{id}\",\"X\":0,\"Y\":0}}}}", timeout.Token);
            Check(await stream.ReadAsync(new byte[1], timeout.Token) == 0, "Queue overflow did not close client");
            Check(await host.Disconnected.ReadAsync(timeout.Token) == id, "Overflow client did not disconnect");
            int count = 0; while (host.Commands.TryRead(out _)) count++;
            Check(count == 128, $"Expected 128 queued commands, got {count}");
        }
        await Task.WhenAll(host.DisposeAsync().AsTask(), host.DisposeAsync().AsTask()).WaitAsync(timeout.Token);
        Console.WriteLine("PASS malformed, unauthorized and overflowing clients; concurrent host disposal");
    }
    private static async Task SlowReaderAndOversize()
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
        await using var host = new SessionHost(new SessionSettings(2, DeathMode.AllDead, 0, BotDifficulty.Normal), "x"); host.Start();
        using var slow = new TcpClient(); slow.ReceiveBufferSize = 1024;
        await slow.ConnectAsync(IPAddress.Loopback, host.Port, timeout.Token);
        await Send(slow.GetStream(), $"{{\"Type\":\"join\",\"Version\":{SessionHost.ProtocolVersion},\"Password\":\"x\",\"DisplayName\":\"Player\"}}", timeout.Token);
        await Read(slow.GetStream(), timeout.Token); var slowId = await host.Joined.ReadAsync(timeout.Token);
        await using var healthy = await SessionClient.ConnectAsync(IPAddress.Loopback, host.Port, "x", timeout.Token);
        await host.Joined.ReadAsync(timeout.Token);
        var big = Enumerable.Range(0, 1100).Select(_ => new WorldEntity(new string('x', 128), 0, 0, true)).ToArray();
        var huge = new WorldSnapshot(1, Enumerable.Range(0, 2048).Select(_ => new WorldEntity(new string('\\', 128), 0, 0, true)).ToArray());
        try { await host.PublishSnapshotAsync(huge, timeout.Token); throw new Exception("Oversize snapshot accepted with peers"); } catch (InvalidDataException) { }
        Check(host.ConnectedClients == 2, "Oversize local snapshot disconnected a peer");
        var escaped = Enumerable.Range(0, 2048).Select(_ => new WorldEntity(new string('é', 128), 0, 0, true)).ToArray();
        int low = 0, high = escaped.Length;
        while (low < high)
        {
            int mid = (low + high + 1) / 2;
            try { Peer.Encode(new Message("snapshot", Snapshot: new WorldSnapshot(4, escaped[..mid]))); low = mid; }
            catch (InvalidDataException) { high = mid - 1; }
        }
        Check(low > 0 && low < escaped.Length, "Failed to bracket escaped UTF-8 frame limit");
        await host.PublishSnapshotAsync(new WorldSnapshot(4, escaped[..low]), timeout.Token);
        Check((await healthy.Snapshots.ReadAsync(timeout.Token)).Tick == 4, "Near-limit escaped snapshot not delivered");
        try { await host.PublishSnapshotAsync(new WorldSnapshot(5, escaped[..(low + 1)]), timeout.Token); throw new Exception("Escaped over-limit frame accepted"); } catch (InvalidDataException) { }
        Check(host.ConnectedClients == 2, "Escaped oversize disconnected peers");
        var watch = Stopwatch.StartNew();
        for (int i = 0; i < 160; i++) { await host.PublishSnapshotAsync(new WorldSnapshot(i + 5, big), timeout.Token); await Task.Delay(8, timeout.Token); }
        var status = host.OutboundStatus(slowId);
        await Task.Delay(250, timeout.Token);
        var after = host.OutboundStatus(slowId);
        Check(status.Writing && after.Writing && status.Sent == after.Sent, "Non-reading peer did not reach a blocked write");
        for (int i = 0; i < 128; i++) host.PublishReward(slowId, new(Guid.NewGuid(), slowId, RewardKind.Xp, 1));
        try { host.PublishReward(slowId, new(Guid.NewGuid(), slowId, RewardKind.Xp, 1)); throw new Exception("Control overflow accepted"); } catch (InvalidOperationException) { }
        var reward = new RewardReceipt(Guid.NewGuid(), healthy.PlayerId, RewardKind.Xp, 2);
        host.PublishReward(healthy.PlayerId, reward);
        Check(await healthy.Rewards.ReadAsync(timeout.Token) == reward && healthy.Progression.Xp == 2, "Healthy peer reward delivery after slow control overflow");
        await host.PublishSnapshotAsync(new WorldSnapshot(9999, [new WorldEntity("last", 0, 0, true)]), timeout.Token);
        WorldSnapshot snapshot;
        do { snapshot = await healthy.Snapshots.ReadAsync(timeout.Token); } while (snapshot.Tick != 9999);
        Check(watch.Elapsed < TimeSpan.FromSeconds(10), "Slow reader blocked healthy client");
        Console.WriteLine($"PASS real TCP non-reading peer and healthy delivery ({watch.ElapsedMilliseconds} ms)");
    }
}

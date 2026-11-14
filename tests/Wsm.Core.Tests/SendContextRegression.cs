using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Text.Json;
using Wsm.Core;

internal static class SendContextRegression
{
    private sealed class FrozenContext : SynchronizationContext
    {
        private readonly ConcurrentQueue<(SendOrPostCallback Callback, object? State)> queue = new();
        public override void Post(SendOrPostCallback callback, object? state) => queue.Enqueue((callback, state));
        public void Pump() { while (queue.TryDequeue(out var work)) work.Callback(work.State); }
    }
    private sealed class GatedStream : Stream
    {
        internal readonly TaskCompletionSource Entered = new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal readonly TaskCompletionSource Release = new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal readonly MemoryStream Sink = new();
        private int writes;
        public override async ValueTask WriteAsync(ReadOnlyMemory<byte> bytes, CancellationToken cancellationToken = default)
        {
            if (Interlocked.Increment(ref writes) == 1)
            {
                Entered.SetResult();
                await Release.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            }
            Sink.Write(bytes.Span);
        }
        public override bool CanRead => false;
        public override bool CanSeek => false;
        public override bool CanWrite => true;
        public override long Length => Sink.Length;
        public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
        public override void Flush() => throw new NotSupportedException();
        public override int Read(byte[] buffer, int offset, int count) => throw new NotSupportedException();
        public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
        public override void SetLength(long length) => throw new NotSupportedException();
        public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
    }
    public static async Task Run()
    {
        var context = new FrozenContext();
        using var stream = new GatedStream();
        using var peer = new Peer(stream, () => { });
        var previous = SynchronizationContext.Current;
        Task first;
        try
        {
            SynchronizationContext.SetSynchronizationContext(context);
            first = peer.SendAsync(new Message("input", Input: new(Guid.NewGuid(), 0, 0)), CancellationToken.None);
        }
        finally { SynchronizationContext.SetSynchronizationContext(previous); }
        await stream.Entered.Task.WaitAsync(TimeSpan.FromSeconds(2));
        var second = peer.SendEncodedAsync(JsonSerializer.SerializeToUtf8Bytes(new { Type = "pong", Nonce = Guid.NewGuid() }), CancellationToken.None);
        var both = Task.WhenAll(first, second);
        bool completed;
        try
        {
            stream.Release.SetResult();
            completed = await Task.WhenAny(both, Task.Delay(1500)) == both;
        }
        finally
        {
            // Release the intentionally frozen test context even on the expected before-state failure.
            var until = DateTime.UtcNow.AddSeconds(3);
            while (!both.IsCompleted && DateTime.UtcNow < until) { context.Pump(); await Task.Delay(1); }
            await both.WaitAsync(TimeSpan.FromSeconds(1));
        }
        if (!completed) throw new Exception("An asynchronous UI-originated send held the framing lock or its completion on the frozen context.");
        var bytes = stream.Sink.ToArray(); int offset = 0;
        foreach (string expected in new[] { "input", "pong" })
        {
            int length = BinaryPrimitives.ReadInt32BigEndian(bytes.AsSpan(offset, 4)); offset += 4;
            using var body = JsonDocument.Parse(bytes.AsMemory(offset, length)); offset += length;
            if (body.RootElement.GetProperty("Type").GetString() != expected) throw new Exception("Concurrent transport frame interleaving.");
        }
        if (offset != bytes.Length) throw new Exception("Trailing transport bytes.");
        Console.WriteLine("PASS asynchronous UI send releases framing lock without pumping; subsequent heartbeat frame intact");
    }
}

namespace Wsm.Launcher;

internal static class FailureCleanup
{
    internal static void Run(Action cleanup, Exception? primary)
    {
        try { cleanup(); }
        catch (Exception error) when (primary != null)
        {
            throw new IOException($"Operation failed ({primary.Message}) and cleanup failed ({error.Message}).",
                new AggregateException(primary, error));
        }
    }
}

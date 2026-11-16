using UnityEngine.Scripting;
namespace Rewired { [Preserve] public partial class Player { int value; public Player(int n){value=n;} [Preserve] public int id => value; } }

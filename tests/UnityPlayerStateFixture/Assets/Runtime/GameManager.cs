using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Scripting;
namespace VampireSurvivors.Data { [Preserve] public enum CharacterType { A=3,B=4 } [Preserve] public enum StageType { Test=7 } }
namespace VampireSurvivors.Objects { [Preserve] public class Stage { [Preserve] public VampireSurvivors.Data.StageType StageType => VampireSurvivors.Data.StageType.Test; } }
namespace VampireSurvivors.Objects.Characters {
 [Preserve] public class CharacterController : ArcadeSprite {
  public float hp,xp; public bool dead; public int level; public VampireSurvivors.Data.CharacterType kind; public Rewired.Player input;
  [Preserve] private VampireSurvivors.Objects.CharacterWeaponsManager _weaponsManager;
  [Preserve] private VampireSurvivors.Objects.CharacterAccessoriesManager _accessoriesManager;
  public void ConfigureManagers(VampireSurvivors.Objects.CharacterWeaponsManager weapons, VampireSurvivors.Objects.CharacterAccessoriesManager accessories) { _weaponsManager=weapons; _accessoriesManager=accessories; }
  [Preserve] public VampireSurvivors.Objects.CharacterWeaponsManager WeaponsManager => _weaponsManager;
  [Preserve] public float CurrentHp=>hp; [Preserve] public float Xp=>xp; [Preserve] public bool IsDead=>dead; [Preserve] public int Level=>level;
  [Preserve] public VampireSurvivors.Data.CharacterType CharacterType=>kind; [Preserve] public Rewired.Player PlayerInput=>input;
 }
}
namespace VampireSurvivors.Framework {
 [Preserve] public class GameManager : MonoBehaviour {
  public bool running=true;
  [Preserve] public bool IsGameRunning=>running;
  public List<VampireSurvivors.Objects.Characters.CharacterController> players=new List<VampireSurvivors.Objects.Characters.CharacterController>(); public VampireSurvivors.Objects.Stage stage;
  [Preserve] public List<VampireSurvivors.Objects.Characters.CharacterController> MainPlayers=>players;
  [Preserve] public VampireSurvivors.Objects.Stage Stage=>stage;
 }
}

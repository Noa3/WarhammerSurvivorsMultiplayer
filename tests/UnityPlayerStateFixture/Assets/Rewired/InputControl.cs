using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine.Scripting;

// Original test doubles for the public metadata contract. These are not the
// vendor input implementation and cannot establish target-game input timing.
namespace Rewired
{
    [Preserve] public enum ControllerType { Keyboard = 0, Mouse = 1, Joystick = 2, Custom = 20 }
    [Preserve] public enum ControllerElementType { Axis = 0, Button = 1, CompoundElement = 100 }
    [Preserve] public enum AxisRange { Full = 0, Positive = 1, Negative = 2 }
    [Preserve] public enum Pole { Positive = 0, Negative = 1 }
    [Preserve] public enum ElementAssignmentType { FullAxis = 0, SplitAxis = 1, Button = 2, KeyboardKey = 3 }
    [Preserve] public class InputCategory
    {
        readonly int value; readonly string label;
        public InputCategory(int id, string name) { value = id; label = name; }
        [Preserve] public int id => value;
        [Preserve] public string name => label;
    }
    [Preserve] public sealed class InputMapCategory : InputCategory
    { public InputMapCategory(int id, string name) : base(id, name) { } }
    [Preserve] public sealed class InputLayout
    {
        readonly int value; readonly string label;
        public InputLayout(int id, string name) { value = id; label = name; }
        [Preserve] public int id => value;
        [Preserve] public string name => label;
    }
    [Preserve] public sealed class ControllerElementIdentifier
    {
        readonly int value;
        public ControllerElementIdentifier(int id) { value = id; }
        [Preserve] public int id => value;
        [Preserve] public string name => "Axis " + value;
    }
    [Preserve] public class Controller
    {
        readonly ControllerType kind;
        [Preserve] public readonly int id;
        public Controller(int id, ControllerType type) { this.id = id; kind = type; }
        [Preserve] public ControllerType type => kind;
        [Preserve] public string tag { get; set; }
    }
    [Preserve] public class ControllerWithAxes : Controller
    {
        protected readonly List<ControllerElementIdentifier> identifiers;
        protected readonly Dictionary<int, float> values = new Dictionary<int, float>();
        public ControllerWithAxes(int id, ControllerType kind, int[] axes) : base(id, kind)
        { identifiers = axes.Select(x => new ControllerElementIdentifier(x)).ToList(); foreach (var x in axes) values.Add(x, 0); }
        [Preserve] public IList<ControllerElementIdentifier> AxisElementIdentifiers => identifiers;
        [Preserve] public float GetAxisById(int id) => values[id];
        [Preserve] public float GetAxisRawById(int id) => values[id];
        [Preserve] public float GetAxis(int index) => GetAxisById(identifiers[index].id);
    }
    [Preserve] public sealed class CustomController : ControllerWithAxes
    {
        readonly int source;
        public static bool FailWrites, FailZero, FailSource;
        public static int SourceReads;
        public CustomController(int id, int sourceId, int[] axes) : base(id, ControllerType.Custom, axes) { source = sourceId; }
        [Preserve] public int sourceControllerId { get { ++SourceReads; if (FailSource) throw new InvalidOperationException("Fixture source read failure"); return source; } }
        public void FixtureIdentifiers(params int[] ids) { identifiers.Clear(); identifiers.AddRange(ids.Select(x => new ControllerElementIdentifier(x))); }
        [Preserve] public void SetAxisValueById(int id, float value)
        {
            if (FailWrites || (FailZero && value == 0)) throw new InvalidOperationException("Fixture axis write failure");
            if (!values.ContainsKey(id)) throw new ArgumentOutOfRangeException(nameof(id));
            values[id] = value;
        }
        [Preserve] public void ClearAxisValueById(int id) { SetAxisValueById(id, 0); }
    }
    [Preserve] public struct ElementAssignment
    {
        public int elementIdentifierId, actionId;
        public bool invert;
        [Preserve] public static ElementAssignment FullAxisAssignment(int elementIdentifierId, int actionId, bool invert)
        { return new ElementAssignment { elementIdentifierId = elementIdentifierId, actionId = actionId, invert = invert }; }
    }
    [Preserve] public sealed class ActionElementMap
    {
        readonly ElementAssignment assignment;
        public ActionElementMap(ElementAssignment value) { assignment = value; }
        [Preserve] public int actionId => assignment.actionId;
        [Preserve] public int elementIdentifierId => assignment.elementIdentifierId;
        [Preserve] public ControllerElementType elementType => ControllerElementType.Axis;
        [Preserve] public AxisRange axisRange => AxisRange.Full;
        [Preserve] public Pole axisContribution => Pole.Positive;
        [Preserve] public bool invert => assignment.invert;
        [Preserve] public int id => assignment.actionId;
    }
    [Preserve] public class ControllerMap
    {
        static int nextId = 300;
        readonly int value, category, layout;
        readonly Controller owner;
        readonly List<ActionElementMap> assignments = new List<ActionElementMap>();
        public int? fixtureIdOverride;
        public bool fixtureAssigned, fixtureFailEnabledRead;
        public Controller fixtureOwnerOverride;
        public static bool FailCreate, FailElementCreate, Prepopulate, FailEnable;
        public ControllerMap(Controller controller, int categoryId, int layoutId)
        { owner = controller; category = categoryId; layout = layoutId; value = nextId++; }
        [Preserve] public int id => fixtureIdOverride ?? value;
        [Preserve] public int categoryId => category;
        [Preserve] public int layoutId => layout;
        [Preserve] public int controllerId => fixtureAssigned ? owner.id : -1;
        [Preserve] public Controller controller => fixtureAssigned ? fixtureOwnerOverride ?? owner : null;
        [Preserve] public ControllerType controllerType => owner.type;
        bool active;
        [Preserve] public bool enabled { get { if (fixtureFailEnabledRead) throw new InvalidOperationException("Fixture primary observation failure"); return active; } set { if (FailEnable) throw new InvalidOperationException("Fixture enable failure"); active = value; } }
        [Preserve] public IList<ActionElementMap> AllMaps => assignments;
        [Preserve] public IList<ActionElementMap> ElementMaps => assignments;
        [Preserve] public static ControllerMap Create(Controller controller, int categoryId, int layoutId)
        {
            if (FailCreate) return null;
            var map = new CustomControllerMap(controller, categoryId, layoutId);
            if (Prepopulate) map.CreateElementMap(ElementAssignment.FullAxisAssignment(2, 101, false));
            return map;
        }
        [Preserve] public bool CreateElementMap(ElementAssignment value)
        { if (FailElementCreate) return false; assignments.Add(new ActionElementMap(value)); return true; }
    }
    [Preserve] public sealed class CustomControllerMap : ControllerMap
    { public CustomControllerMap(Controller owner, int category, int layout) : base(owner, category, layout) { } }
    public partial class Player
    {
        [Preserve] public readonly ControllerHelper controllers = new ControllerHelper();
        [Preserve] public float GetAxis(string actionName)
        {
            var action = ReInput.mapping.GetActionId(actionName);
            float result = 0;
            foreach (var controller in controllers.Controllers)
                foreach (var map in controllers.maps.GetMaps(controller))
                    if (map.enabled)
                        foreach (var binding in map.AllMaps)
                            if (binding.actionId == action && controller is ControllerWithAxes axes)
                                result += axes.GetAxisById(binding.elementIdentifierId) * (binding.invert ? -1 : 1);
            return Math.Max(-1, Math.Min(1, result));
        }
        [Preserve] public sealed class ControllerHelper
        {
            public readonly List<Controller> assigned = new List<Controller>();
            [Preserve] public readonly MapHelper maps = new MapHelper();
            [Preserve] public IEnumerable<Controller> Controllers => assigned;
            [Preserve] public void AddController(Controller controller, bool removeFromOtherPlayers)
            { if (removeFromOtherPlayers) throw new InvalidOperationException("Fixture forbids foreign detach"); if (!assigned.Contains(controller)) assigned.Add(controller); }
            [Preserve] public void RemoveController(Controller controller) { assigned.Remove(controller); }
            [Preserve] public bool ContainsController(Controller controller) => assigned.Contains(controller);
            [Preserve] public sealed class MapHelper
            {
                readonly Dictionary<int, List<ControllerMap>> data = new Dictionary<int, List<ControllerMap>>();
                public static bool FailAdd, FailRemove, FailAfterEmptyAdd;
                [Preserve] public IList<ControllerMap> GetMaps(Controller controller)
                { if (!data.TryGetValue(controller.id, out var maps)) { maps = new List<ControllerMap>(); data.Add(controller.id, maps); } return maps; }
                [Preserve] public void AddMap(Controller controller, ControllerMap map, bool startEnabled)
                { if (FailAdd) throw new InvalidOperationException("Fixture map add failure"); map.enabled = startEnabled; map.fixtureAssigned = true; GetMaps(controller).Add(map); }
                [Preserve] public void AddEmptyMap(ControllerType controllerType,int controllerId,int categoryId,int layoutId)
                {
                    var controller=ReInput.controllers.GetCustomController(controllerId);
                    if(controllerType!=ControllerType.Custom||controller==null)throw new ArgumentException("Fixture custom controller absent");
                    var map=ControllerMap.Create(controller,categoryId,layoutId);
                    if(map==null)throw new InvalidOperationException("Fixture empty map creation failed");
                    AddMap(controller,map,false);
                    if(FailAfterEmptyAdd)throw new InvalidOperationException("Fixture failure after registered empty-map creation");
                }
                [Preserve] public void RemoveMap(ControllerType type, int controllerId, int mapId)
                { if (FailRemove) throw new InvalidOperationException("Fixture map remove failure"); if (data.TryGetValue(controllerId, out var maps)) maps.RemoveAll(m => m.id == mapId && m.controllerType == type); }
            }
        }
    }
    public static partial class ReInput
    {
        public static readonly PlayerHelper playerHelper = new PlayerHelper();
        public static readonly ControllerHelper controllerHelper = new ControllerHelper();
        public static int[] fixtureAxes = new[] { 2, 9 };
        [Preserve] public static PlayerHelper players => playerHelper;
        [Preserve] public static ControllerHelper controllers => controllerHelper;
        [Preserve] public sealed class PlayerHelper
        {
            public readonly Dictionary<int, Player> values = new Dictionary<int, Player>();
            [Preserve] public Player GetPlayer(int playerId) { return values.TryGetValue(playerId, out var p) ? p : null; }
        }
        [Preserve] public sealed class ControllerHelper
        {
            int nextId = 700;
            public readonly List<CustomController> values = new List<CustomController>();
            public static bool FailCreate, FailDestroy, FailAfterCreate, FailGetAfterCreate, FailGet;
            [Preserve] public int customControllerCount => values.Count;
            [Preserve] public CustomController[] GetCustomControllers() { if (FailGet) throw new InvalidOperationException("Fixture global query failure"); return values.ToArray(); }
            [Preserve] public CustomController GetCustomController(int controllerId) => values.Find(c => c.id == controllerId);
            [Preserve] public CustomController CreateCustomController(int sourceControllerId) => CreateCustomController(sourceControllerId, "");
            [Preserve] public CustomController CreateCustomController(int sourceControllerId, string tag)
            { if (FailCreate) return null; var c = new CustomController(nextId++, sourceControllerId, fixtureAxes); c.tag = tag; values.Add(c); if (FailAfterCreate) { if (FailGetAfterCreate) FailGet=true; throw new InvalidOperationException("Fixture failure after custom registration"); } return c; }
            [Preserve] public bool IsCustomControllerAssignedToPlayer(int customControllerId, int playerId)
            { var p = players.GetPlayer(playerId); return p != null && p.controllers.Controllers.Any(c => c.id == customControllerId && c.type == ControllerType.Custom); }
            [Preserve] public bool DestroyCustomController(CustomController customController)
            {
                if (FailDestroy || players.values.Values.Any(p => p.controllers.Controllers.Contains(customController))) return false;
                return values.Remove(customController);
            }
        }
        public sealed partial class MappingHelper
        {
            public readonly List<InputMapCategory> categories = new List<InputMapCategory> { new InputMapCategory(17, "Gameplay"), new InputMapCategory(19, "Menu") };
            public readonly List<InputLayout> layouts = new List<InputLayout> { new InputLayout(23, "CustomDefault") };
            [Preserve] public IList<InputMapCategory> MapCategories => categories;
            [Preserve] public IList<InputLayout> CustomControllerLayouts => layouts;
            [Preserve] public InputMapCategory GetMapCategory(int id) => categories.Find(c => c.id == id);
            [Preserve] public InputLayout GetCustomControllerLayout(int id) => layouts.Find(l => l.id == id);
        }
    }
}

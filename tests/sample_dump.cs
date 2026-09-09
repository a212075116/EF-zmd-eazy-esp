// Il2CppDumper dump.cs 样例（本地自测 find_offsets.py 用，非游戏真实内容）
// Namespace: Game.Logic
public class MonsterController : BaseActor // TypeSize: 0x1A8
{
    // Fields
    private System.Int32 _monsterType; // 0x58
    internal Il2CppString monsterName; // 0x48
    public UnityEngine.Vector3 worldPosition; // 0x60
    private System.Single _bodyHeight; // 0xA8
    public static System.Collections.Generic.List<MonsterController> s_instances; // 0x20
    public System.Void TakeDamage(System.Int32 dmg) { }
}

// Namespace: Game.World
public class DropItemManager : ObjectManager // TypeSize: 0x90
{
    // Fields
    public UnityEngine.Vector3 lootPos; // 0x70
    private static System.Collections.Generic.List<DropItem> _allDrops; // 0x10
    public System.Boolean isActive; // 0x30
}

// Namespace: Camera
public class RtCamera : MonoBehaviour // TypeSize: 0x200
{
    // Fields
    public System.Single fieldOfView; // 0x4C
    private UnityEngine.Vector3 m_CamWorldPos; // 0x90
}

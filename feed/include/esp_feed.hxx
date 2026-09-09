// ===========================================================================
//  esp_feed.hxx -- F4 live ESP feed for endfield-esp overlay (header-only)
//
//  60 Hz walk of Beyond.Gameplay.Core.EntityManager via the il2cpp runtime
//  API. Every field offset is resolved AT RUNTIME BY NAME (field_get_offset)
//  -> version-proof, same strategy as the dumper itself. No hardcoded offsets,
//  no ReadProcessMemory anywhere. Publishes a seqlock-protected snapshot into
//  named section "Local\EndfieldEsp"; the external overlay maps it with
//  OpenFileMapping (kernel handle-rights stripping does not apply to
//  pagefile-backed sections we own).
//
//  WIRE FORMAT mirrors endfield-esp/config/shm_proto.hpp -- keep in sync!
// ===========================================================================
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "../include/il2cpp_api.hxx"
#include "../include/utils.hxx"

namespace espfeed {

// ------------------------------- wire format -------------------------------
static const uint32_t MAGIC = 0x31534645u;   // "EFS1"
static const uint32_t VER   = 1;
static const int      MAXE  = 2048;

// one-shot walk census: armed by F4 re-arm, prints one audit then goes quiet
static int g_census = 0, cDead = 0, cNod = 0, cPos = 0, cNull = 0, cNpc = 0, cChar = 0;
static int cZero = 0, cFar = 0;
static int g_dsd = 0, g_nsd = 0, g_csd = 0, g_zsd = 0, g_gsd = 0, g_fw = 0, g_ps = 2;
static int cNear = 0; static char g_near[40][96]; static int g_nearN = 0;
static char g_cklass[10][60]; static int g_ckn[10]; static int g_ckN = 0;

struct Header {
    uint32_t      magic, ver;
    volatile LONG seq;            // seqlock: odd = writer inside
    LONG          count;          // valid Rec rows (LONG: MSVC interlocked overloads want long*)
    volatile LONG alive;          // feed thread heartbeat
    int32_t       camOk;
    float         camPos[3], camFwd[3], camRight[3], camUp[3];
    float         fov;            // vertical degrees
    float         charPos[3];     // EntityManager.m_mainCharPos
    uint32_t      pad0;
    ULONGLONG     tick;           // ms, writer GetTickCount64
};
struct Rec {
    ULONGLONG     id;             // BaseEntityData.id
    int32_t       type;           // 0 other | 1 enemy | 2 npc | 3 character
    float         pos[3];         // world, Unity y-up
    char          name[48];       // UTF-8 display name (BaseTemplateData.name, fallback templateId)
};
struct Block { Header h; Rec ents[MAXE]; };

// ------------------------------ symbol table -------------------------------
struct TblEntry { char nm[40]; void* obj; void* mTV; };
static std::vector<TblEntry> g_tbls;
static char g_tblNames[700] = {0};

struct Res {
    void*     mgrCls = nullptr; void* nodeCls = nullptr; void* bedCls = nullptr;
    void*     enemyCls = nullptr; void* npcCls = nullptr; void* charCls = nullptr;
    void*     fSManager = nullptr;
    uintptr_t nodes = ~0ull, nodeData = ~0ull, nodeDead = ~0ull;
    uintptr_t bedPos = ~0ull, bedTpl = ~0ull, bedId = ~0ull, mgrCharPos = ~0ull;
    uintptr_t bedTmpl = ~0ull, btnName = ~0ull;   // data->templateData->name (display name)
    uintptr_t collList = ~0ull;               // from live collection klass
    void*     i18nCls = nullptr, *mI18nTry = nullptr;    // Beyond.I18n.I18nUtils.TryGetText(key,out)
    void*     fInitMap = nullptr;                        // field: s_initTextMap
    bool      dictPeeked = false;
    void*     npcTbl = nullptr, *mNpcTVT = nullptr;      // Tables.s_npcTable + TryGetValue
    void*     strCls = nullptr;                          // System.String klass
    int       rowDbg = 4;                                // layout-evidence budget
    void*     etTbl = nullptr, *mEtCnt = nullptr, *mEtKey = nullptr, *mEtVal = nullptr;
    void*     itTbl = nullptr, *mItCnt = nullptr, *mItKey = nullptr, *mItVal = nullptr;
    void*     mEtTV = nullptr;
    void *    nTbl[21], *nCnt[21], *nKey[21], *nVal[21];  // name-table registry
    void*     mItTV = nullptr;                            // i18n table TryGetValue(long,out string)
    void*     beanCls = nullptr, *mBeanGS = nullptr;      // Bean.GetString(string,out int&)
    void*     mBeanGL = nullptr;                          // Bean.GetLong(string,out int&)
    void*     mBeanGS1 = nullptr;                         // Bean.GetString(int rawOffset)
    void*     camMain = nullptr, *camXform = nullptr, *camFov = nullptr;
    void*     tPos = nullptr, *tFwd = nullptr, *tRight = nullptr, *tUp = nullptr;
    bool      ok = false;
};
static Res  R;
static int g_espq = 0;   // 1 = full diagnostics, 0 = silent release build
static int g_forceQ = 0;
static bool LoudBoot(const char* s) {   // always-loud boot/flip lines even when silent
    if (strncmp(s, "[esp-feed] ", 11)) return false;
    static const char* kLoud[] = { "build=", "offs:", "spark", "i18n=", "i18n ", "camera",
        "gcreg", "diag", "OFF", "resolve", "CreateFile", "MapView", "EntityManager", "il2cpp" };
    for (int i = 0; i < 14; i++)
        if (!strncmp(s + 11, kLoud[i], strlen(kLoud[i]))) return true;
    return false;
}
static void QLog(const char* s) {
    if (g_espq || g_forceQ || LoudBoot(s)) Log(s);
}
static const char* const kNtField[21] = {
    "s_enemyTemplateDisplayInfoTable", "s_enemyDisplayInfoTable",
    "s_gameMechanicTable", "s_worldGameMechanicsDisplayInfoTable",
    "s_itemTable", "s_itemGatherTextTable",
    "s_factoryBuildingTable", "s_doodadGeneralTable",
    "s_doodadTreeTable", "s_npcTable",
    "s_interactiveAttributeDataTable", "s_interactiveMarkDataTable",
    "s_usableItemChestTable", "s_collectionTable",
    "s_interactiveFacWrapperTable", "s_interactiveMissionDataTable",
    "s_npcGroupTable", "s_gameplayAndEnvironmentalNpc",
    "s_mapMarkTempTable", "s_instructionBook", "s_gameMechanicCategoryTable" };
static const char* const kNtShort[21] = { "disE", "enyD", "gm", "wgm", "item", "gath",
    "facB", "ddG", "ddT", "npcT", "intAt", "intMk", "uChs", "col", "intFac", "intMs",
    "npcG", "envNpc", "mkT", "insB", "catT" };
static void* g_collKlass = nullptr;
static void FillName(void* data, Rec& r);   // defined below WalkEntities

// ------------------------------ SEH helpers --------------------------------
// (small functions only -> no C2712: no destructible locals around __try)
static bool rd(const void* p, void* dst, int n) {
    bool ok = false;
    if (p) { __try { memcpy(dst, p, n); ok = true; } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    return ok;
}
static void* rp(const void* p) {
    void* v = nullptr;
    if (!rd(p, &v, 8)) return nullptr;
    return (uintptr_t)v > 0x10000 ? v : nullptr;
}
static void* GetStatic(void* field) {
    void* v = nullptr;
    if (field && api::field_static_get_value) {
        __try { api::field_static_get_value(field, &v); } __except (EXCEPTION_EXECUTE_HANDLER) { v = nullptr; }
    }
    return (uintptr_t)v > 0x10000 ? v : nullptr;
}
static void* BoxVal(void* cls, void* src) {
    void* v = nullptr;
    if (cls && src && api::value_box) {
        __try { v = api::value_box(cls, src); } __except (EXCEPTION_EXECUTE_HANDLER) { v = nullptr; }
    }
    return v;
}
struct I18nCall { void* res; void* out; void* ex; };
static I18nCall I18nTry(void* m, void* keyStr) {
    I18nCall d;
    d.res = nullptr; d.out = nullptr; d.ex = nullptr;
    void* outSlot = nullptr;
    void* argv[2];
    argv[0] = keyStr;              // string key
    argv[1] = &outSlot;            // out string value utf8-comment utf8-comment
    if (m && keyStr && api::runtime_invoke) {
        __try {
            d.res = api::runtime_invoke(m, nullptr, argv, &d.ex);   // static: obj=null
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            d.res = nullptr; d.ex = (void*)(intptr_t)-1;
        }
    }
    d.out = outSlot;
    return d;
}
static void ExName(const void* ex, char* out, int n) {   // exception class name, or SEH / -
    out[0] = 0;
    if (!ex || ex == (void*)(intptr_t)-1 || !api::class_get_name) { snprintf(out, n, ex ? "SEH" : "-"); return; }
    __try {
        void* k = *(void* const*)ex;                     // object header slot0 = Il2CppClass*
        const char* nmz = api::class_get_namespace ? api::class_get_namespace(k) : "";
        snprintf(out, n, "%s.%s", nmz, api::class_get_name(k));
    } __except (EXCEPTION_EXECUTE_HANDLER) { snprintf(out, n, "?"); }
}
struct Call2 { void* res; void* out; void* ex; };
static Call2 InvObj2(void* m, void* obj, void* a0) {      // {T key, out V value} on instance
    Call2 d; d.res = nullptr; d.out = nullptr; d.ex = nullptr;
    void* slot = nullptr;
    void* av[2]; av[0] = a0; av[1] = &slot;
    if (m && obj && a0 && api::runtime_invoke) {
        __try { d.res = api::runtime_invoke(m, obj, av, &d.ex); }
        __except (EXCEPTION_EXECUTE_HANDLER) { d.res = nullptr; d.ex = (void*)(intptr_t)-1; }
    }
    d.out = slot;
    return d;
}
static void* Inv(void* m, void* o) {
    void* r = nullptr;
    if (m && api::runtime_invoke) {
        void* ex = nullptr;
        __try { r = api::runtime_invoke(m, o, nullptr, &ex); } __except (EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
        if (ex) r = nullptr;
    }
    return r;
}
static bool UnboxF3(void* boxed, float* out) {
    if (!boxed || !api::object_unbox) return false;
    bool ok = false;
    __try { float* p = (float*)api::object_unbox(boxed);
            if (p) { out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; ok = true; } }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}
static bool UnboxF1(void* boxed, float* out) {
    if (!boxed || !api::object_unbox) return false;
    bool ok = false;
    __try { float* p = (float*)api::object_unbox(boxed); if (p) { *out = *p; ok = true; } }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}
static void* InvInt(void* m, void* o, int32_t i) {        // instance call, one int arg
    void* r = nullptr;
    if (m && o && api::runtime_invoke) {
        void* av[1]; av[0] = &i; void* ex = nullptr;
        __try { r = api::runtime_invoke(m, o, av, &ex); }
        __except (EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
        if (ex) r = nullptr;
    }
    return r;
}
static bool UnboxI32(void* boxed, int32_t& out) {
    if (!boxed || !api::object_unbox) return false;
    bool ok = false;
    __try { int32_t* q = (int32_t*)api::object_unbox(boxed); if (q) { out = *q; ok = true; } }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}
static bool UnboxI64(void* boxed, long long& out) {
    if (!boxed || !api::object_unbox) return false;
    bool ok = false;
    __try { long long* q = (long long*)api::object_unbox(boxed); if (q) { out = *q; ok = true; } }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}
// Resolve one named Beyond.Cfg spark table + its official accessors (dump-proven names)
static void ResolveSparkTable(void* tables, const char* fname,
    void** ptbl, void** pc, void** pk, void** pv, void** ptv) {
    *ptbl = *pc = *pk = *pv = nullptr;
    if (ptv) *ptv = nullptr;
    if (!tables || !api::class_get_field_from_name || !api::class_get_method_from_name)
        return;
    void* fld = nullptr;
    __try { fld = api::class_get_field_from_name(tables, fname); }
    __except (EXCEPTION_EXECUTE_HANDLER) { fld = nullptr; }
    void* o = fld ? GetStatic(fld) : nullptr;
    if (!o) return;
    void* tk = *(void* const*)o;
    void* mC = nullptr; void* mK = nullptr; void* mV = nullptr;
    for (void* c = tk; c; ) {
        if (!mC) { mC = api::class_get_method_from_name(c, "get_count", 0);
                   if (!mC) mC = api::class_get_method_from_name(c, "get_Count", 0); }
        if (!mK) mK = api::class_get_method_from_name(c, "GetKeyByIndex", 1);
        if (!mV) mV = api::class_get_method_from_name(c, "GetValueByIndex", 1);
        if (ptv && !*ptv) *ptv = api::class_get_method_from_name(c, "TryGetValue", 2);
        if (mK && mV) break;
        if (!api::class_get_parent) break;
        void* p2 = api::class_get_parent(c);
        if (!p2 || p2 == c) break;
        c = p2;
    }
    if (mK && mV) { *ptbl = o; *pc = mC; *pk = mK; *pv = mV; }
}
static void ReadUtf16(void* str, char* out, int cap) {
    out[0] = 0;
    if (!str || !api::string_length_fn || !api::string_chars) return;
    __try {
        int len = api::string_length_fn(str);
        wchar_t* ch = api::string_chars(str);
        if (ch && len > 0) {
            if (len > 60) len = 60;
            int sz = WideCharToMultiByte(CP_UTF8, 0, ch, len, out, cap - 1, nullptr, nullptr);
            if (sz > 0) out[sz < cap ? sz : cap - 1] = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static int  GcPause( )  { int s = -1; if (api::gc_dont_gc_ptr) { __try { s = *api::gc_dont_gc_ptr; *api::gc_dont_gc_ptr = s + 1; } __except (EXCEPTION_EXECUTE_HANDLER) { s = -1; } } return s; }
static void GcResume(int s) { if (s >= 0 && api::gc_dont_gc_ptr) { __try { *api::gc_dont_gc_ptr = s; } __except (EXCEPTION_EXECUTE_HANDLER) {} } }

// ------------------------------ runtime lookup ------------------------------
static void* FindClass(const char* ns, const char* nm) {
    if (!api::get_domain || !api::get_assemblies || !api::assembly_get_image || !api::class_from_name)
        return nullptr;
    void* domain = api::get_domain();
    if (!domain) return nullptr;
    size_t cnt = 0; void** a = api::get_assemblies(domain, &cnt);
    if (!a) return nullptr;
    for (size_t i = 0; i < cnt; i++) {
        void* img = api::assembly_get_image(a[i]);
        if (!img) continue;
        void* k = api::class_from_name(img, ns, nm);
        if (k) return k;
    }
    return nullptr;
}
static void* FindNested(void* outer, const char* nm) {
    if (!outer || !api::class_get_nested_types) return nullptr;
    void* it = nullptr; void* t;
    while ((t = api::class_get_nested_types(outer, &it))) {
        const char* s = api::class_get_name ? api::class_get_name(t) : nullptr;
        if (s && strcmp(s, nm) == 0) return t;
    }
    return nullptr;
}
static void* FindField(void* k, const char* nm) {
    if (!k || !api::class_get_fields) return nullptr;
    void* it = nullptr; void* f;
    while ((f = api::class_get_fields(k, &it))) {
        const char* s = api::field_get_name ? api::field_get_name(f) : nullptr;
        if (s && strcmp(s, nm) == 0) return f;
    }
    return nullptr;
}
static uintptr_t OffOf(void* k, const char* nm) {
    void* f = FindField(k, nm);
    if (f) return (uintptr_t)api::field_get_offset(f);
    return ~0ull;
}
static void* FindMethod(void* k, const char* nm, int argc) {
    for (void* c = k; c && api::class_get_method_from_name; ) {
        void* m = api::class_get_method_from_name(c, nm, argc);
        if (m) return m;
        if (!api::class_get_parent) break;
        void* p = api::class_get_parent(c);
        if (p == c) break;
        c = p;
    }
    return nullptr;
}

static bool Resolve() {
    memset(&R, 0, sizeof R);
    g_collKlass = nullptr;
    static const char* NS = "Beyond.Gameplay.Core";
    R.mgrCls   = FindClass(NS, "EntityManager");
    if (!R.mgrCls) { Log("[esp-feed] EntityManager class not found"); return false; }
    R.nodeCls  = FindNested(R.mgrCls, "EntityNode");
    R.bedCls   = FindClass(NS, "BaseEntityData");
    R.enemyCls = FindClass(NS, "EnemyInfo");
    R.npcCls   = FindClass(NS, "NpcInfo");
    R.charCls  = FindClass(NS, "CharacterInfo");
    R.fSManager = FindField(R.mgrCls, "s_manager");
    R.nodes      = OffOf(R.mgrCls, "m_nodes");
    R.mgrCharPos = OffOf(R.mgrCls, "m_mainCharPos");
    if (R.nodeCls) { R.nodeData = OffOf(R.nodeCls, "<data>k__BackingField");
                     R.nodeDead = OffOf(R.nodeCls, "m_isDead"); }
    if (R.bedCls)  { R.bedPos = OffOf(R.bedCls, "<position>k__BackingField");
                     R.bedTpl = OffOf(R.bedCls, "<templateId>k__BackingField");
                     R.bedId  = OffOf(R.bedCls, "<id>k__BackingField");
                     R.bedTmpl = OffOf(R.bedCls, "<templateData>k__BackingField"); }
    { void* btn = FindClass("Beyond.Gameplay", "BaseTemplateData");
      if (btn) R.btnName = OffOf(btn, "name"); }

    char buf[512];
    snprintf(buf, sizeof buf,
        "[esp-feed] offs: s_mgr=%s nodes=0x%llX nd.data=0x%llX nd.dead=0x%llX tmpl=0x%llX nm=0x%llX "
        "pos=0x%llX tpl=0x%llX char=0x%llX",
        R.fSManager ? "ok" : "MISSING", (unsigned long long)R.nodes,
        (unsigned long long)R.nodeData, (unsigned long long)R.nodeDead,
        (unsigned long long)R.bedTmpl, (unsigned long long)R.btnName,
        (unsigned long long)R.bedPos, (unsigned long long)R.bedTpl,
        (unsigned long long)R.mgrCharPos);
    QLog(buf);

    R.i18nCls = FindClass("Beyond.I18n", "I18nUtils");
    if (R.i18nCls && api::class_get_field_from_name)
        R.fInitMap = api::class_get_field_from_name(R.i18nCls, "s_initTextMap");
    if (R.i18nCls) R.mI18nTry = FindMethod(R.i18nCls, "TryGetText", 2);
    if (R.i18nCls && api::runtime_class_init) {                 // run static ctor first; avoids AV on lazy-init null tables
        __try { api::runtime_class_init(R.i18nCls); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    {
        R.strCls = FindClass("System", "String");
        g_tbls.clear();                                     // NO bulk sweep anymore
        void* tables = FindClass("Beyond.Cfg", "Tables");
        ResolveSparkTable(tables, "s_enemyTemplateTable",
                          &R.etTbl, &R.mEtCnt, &R.mEtKey, &R.mEtVal, &R.mEtTV);
        ResolveSparkTable(tables, "s_i18nTextTable",
                          &R.itTbl, &R.mItCnt, &R.mItKey, &R.mItVal, &R.mItTV);
        for (int i = 0; i < 21; i++)
            ResolveSparkTable(tables, kNtField[i],
                              &R.nTbl[i], &R.nCnt[i], &R.nKey[i], &R.nVal[i], nullptr);
        R.beanCls = FindClass("Beyond.SparkBuffer.Runtime", "Bean");
        if (R.beanCls) {
            R.mBeanGS = FindMethod(R.beanCls, "GetString", 2);
            R.mBeanGL = FindMethod(R.beanCls, "GetLong", 2);
            R.mBeanGS1 = FindMethod(R.beanCls, "GetString", 1);
        }
        snprintf(buf, sizeof buf, "[esp-feed] spark tv=%s bean=%s gs=%s gl=%s itv=%s gs1=%s",
                 R.mEtTV ? "ok" : "miss", R.beanCls ? "ok" : "miss",
                 R.mBeanGS ? "ok" : "miss", R.mBeanGL ? "ok" : "miss",
                 R.mItTV ? "ok" : "miss", R.mBeanGS1 ? "ok" : "miss");
        { int nb = (int)strlen(buf);
          for (int i = 0; i < 21 && nb < (int)sizeof buf - 24; i++) {
              int l = snprintf(buf + nb, sizeof buf - nb, " %s=%s",
                  kNtShort[i], R.nTbl[i] ? "ok" : "-");
              if (l <= 0) break; nb += l;
          } }
        QLog(buf);
    }
    snprintf(buf, sizeof buf, "[esp-feed] i18n=%s get=%s cinit=%s spark et=%s it=%s",
             R.i18nCls ? "ok" : "miss", R.mI18nTry ? "ok" : "miss",
             api::runtime_class_init ? "yes" : "noexport",
             R.etTbl ? "ok" : "miss", R.itTbl ? "ok" : "miss");
    QLog(buf);
    if (g_tblNames[0]) { g_tblNames[sizeof g_tblNames - 1] = 0; QLog(g_tblNames); }

    bool core = R.fSManager && R.nodes != ~0ull && R.nodeCls && R.nodeData != ~0ull
             && R.nodeDead != ~0ull && R.bedCls && R.bedPos != ~0ull;
    void* clsCam = FindClass("UnityEngine", "Camera");
    void* clsTr  = FindClass("UnityEngine", "Transform");
    if (clsCam && clsTr) {
        R.camMain  = FindMethod(clsCam, "get_main", 0);
        R.camXform = FindMethod(clsCam, "get_transform", 0);
        R.camFov   = FindMethod(clsCam, "get_fieldOfView", 0);
        R.tPos     = FindMethod(clsTr, "get_position", 0);
        R.tFwd     = FindMethod(clsTr, "get_forward", 0);
        R.tRight   = FindMethod(clsTr, "get_right", 0);
        R.tUp      = FindMethod(clsTr, "get_up", 0);
    }
    bool cam = R.camMain && R.camXform && R.tPos && R.tFwd && R.tRight && R.tUp;
    Log(cam ? "[esp-feed] camera hooks ok" : "[esp-feed] CAMERA hooks incomplete");
    R.ok = core && cam;
    return R.ok;
}

// -------------------------------- camera -----------------------------------
struct CamData { bool ok; float pos[3], f[3], r[3], u[3], fov; };
static CamData GrabCam() {
    CamData c{}; c.ok = false; c.fov = 47.f;
    void* cam = Inv(R.camMain, nullptr);      if (!cam) return c;
    void* tr  = Inv(R.camXform, cam);         if (!tr)  return c;
    if (!UnboxF3(Inv(R.tPos, tr), c.pos))   return c;
    if (!UnboxF3(Inv(R.tFwd, tr), c.f))     return c;
    if (!UnboxF3(Inv(R.tRight, tr), c.r))   return c;
    if (!UnboxF3(Inv(R.tUp, tr), c.u))      return c;
    UnboxF1(Inv(R.camFov, cam), &c.fov);
    if (!(c.fov > 1.f && c.fov < 179.f)) c.fov = 47.f;
    c.ok = true;
    return c;
}

static void CamCensus() {   // one-shot: list every active camera to spot the REAL render cam
    void* clsCam = FindClass("UnityEngine", "Camera");
    if (!clsCam) { Log("[esp-feed] cams: Camera cls miss"); return; }
    void* mAll = FindMethod(clsCam, "get_allActiveCameras", 0);
    void* mName = FindMethod(clsCam, "get_name", 0);
    if (!mAll || !mName) { Log("[esp-feed] cams: method miss all/name"); return; }
    void* arr = nullptr;
    __try { arr = Inv(mAll, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { arr = nullptr; }
    if (!arr) { Log("[esp-feed] cams: null arr"); return; }
    int32_t cnt = 0; rd((char*)arr + 0x18, &cnt, 4);
    char hm[80]; snprintf(hm, sizeof hm, "[esp-feed] cams=%d", cnt); Log(hm);
    if (cnt > 6) cnt = 6;
    for (int i = 0; i < cnt; i++) {
        void* cam = nullptr; rd((char*)arr + 0x20 + (size_t)i * 8, &cam, 8);
        if (!cam) continue;
        void* nmS = nullptr;
        __try { nmS = Inv(mName, cam); } __except (EXCEPTION_EXECUTE_HANDLER) { nmS = nullptr; }
        char nm[40]; nm[0] = 0; if (nmS) ReadUtf16(nmS, nm, 40);
        void* tr = nullptr;
        __try { tr = R.camXform ? Inv(R.camXform, cam) : nullptr; }
        __except (EXCEPTION_EXECUTE_HANDLER) { tr = nullptr; }
        float pw[3] = {0,0,0}, fw[3] = {0,0,0}, fv = 0.f;
        if (tr) { UnboxF3(Inv(R.tPos, tr), pw); UnboxF3(Inv(R.tFwd, tr), fw); }
        if (R.camFov) UnboxF1(Inv(R.camFov, cam), &fv);
        char dm[240];
        snprintf(dm, sizeof dm, "[esp-feed] cam#%d '%s' p(%.1f,%.1f,%.1f) f(%.2f,%.2f,%.2f) fov=%.1f",
            i, nm[0] ? nm : "?", pw[0], pw[1], pw[2], fw[0], fw[1], fw[2], fv);
        Log(dm);
    }
}

static void EscU16(void* strObj, char* out, int n) {   // utf16 string -> ascii-escaped preview
    out[0] = 0;
    if (!strObj) return;
    int len = 0; rd((char*)strObj + 0x10, &len, 4);
    if (len <= 0 || len > 4096) return;
    int o = 0, shown = 0;
    for (int i = 0; i < len && shown < 24; i++) {
        uint16_t ch = 0; if (!rd((char*)strObj + 0x14 + i * 2, &ch, 2)) break;
        shown++;
        const char* piece; char tmp[8];
        if (ch >= 32 && ch < 127 && ch != '\'') { char c[2]{ (char)ch, 0 }; piece = c; }
        else { snprintf(tmp, sizeof tmp, "\\u%04X", ch); piece = tmp; }
        int l = (int)strlen(piece);
        if (o + l >= n - 1) break;
        memcpy(out + o, piece, l); o += l;
    }
    out[o] = 0;
}
static void PeekTextMap() {                            // one-shot: real-offset dict peek
    char msg[256];
    void* stm = R.fInitMap ? GetStatic(R.fInitMap) : nullptr;
    void* dict = stm ? rp((char*)stm + 0x10) : nullptr;           // SimpleTextMap.m_textMap
    if (!dict) { QLog("[i18n-dict] stm/dict null"); return; }
    void* dk = *(void* const*)dict;                               // instance klass
    int offEnt = -1, offCnt = -1;
    char fl[420]; fl[0] = 0; int fo = 0;
    if (dk && api::class_get_fields && api::field_get_name && api::field_get_offset) {
        void* it = nullptr; void* fd;
        for (int i = 0; i < 16 && (fd = api::class_get_fields(dk, &it)); i++) {
            const char* fn = api::field_get_name(fd); int off = api::field_get_offset(fd);
            if (!fn) continue;
            if (!strcmp(fn, "_entries")) offEnt = off;
            if (!strcmp(fn, "_count"))    offCnt = off;
            int l = snprintf(fl + fo, (size_t)(sizeof fl - fo), " %s@0x%X", fn, (unsigned)off);
            if (l > 0 && fo + l < (int)sizeof fl) fo += l;
        }
    }
    QLog(fl[0] ? fl : "[i18n-dict] field iter failed");
    snprintf(msg, sizeof msg, "[i18n-dict] offEnt=0x%X offCnt=0x%X", (unsigned)offEnt, (unsigned)offCnt);
    QLog(msg);
    if (offEnt < 0 || offCnt < 0) return;
    void* ent = rp((char*)dict + offEnt);
    int cnt = 0; rd((char*)dict + offCnt, &cnt, 4);
    if (!ent) { QLog("[i18n-dict] ent null"); return; }
    char* data = (char*)ent + 0x20;                  // inline Entry[] elements (NO deref!)
    snprintf(msg, sizeof msg, "[i18n-dict] cnt=%d ent=%p", cnt, ent);
    QLog(msg);
    if (cnt <= 0) return;
    int show = cnt > 10 ? 10 : cnt;
    for (int i = 0; i < show; i++) {                 // Entry{hc@0 next@4 key@8 val@0x10} stride 0x18
        void* kp = rp(data + (size_t)i * 0x18 + 0x8);
        void* vp = rp(data + (size_t)i * 0x18 + 0x10);
        char k[96], v[128];
        EscU16(kp, k, sizeof k); EscU16(vp, v, sizeof v);
        snprintf(msg, sizeof msg, "[i18n-dict] k='%s' v='%s'", k, v);
        QLog(msg);
    }
}

static void RowKlassTag(void* obj, char* tag, int n) {      // ns.Name of instance klass
    tag[0] = 0;
    if (!obj) return;
    void* k = rp(obj);
    if (!k || !api::class_get_name) return;
    __try {
        const char* nmz = api::class_get_name(k);
        const char* ns = api::class_get_namespace ? api::class_get_namespace(k) : "";
        snprintf(tag, n, "%s.%s", (ns && *ns) ? ns : "?", nmz ? nmz : "?");
    } __except (EXCEPTION_EXECUTE_HANDLER) { tag[0] = 0; }
}
static void PosScan(void* data, const char* who) {   // census helper: find coord-looking Vector3s
    char raw[0x148];
    if (!rd(data, raw, 0x148)) return;
    int shown = 0;
    for (int off = 0x10; off + 12 <= 0x148 && shown < 6; off += 4) {
        float v[3]; memcpy(v, raw + off, 12);
        bool ok = true;
        for (int j = 0; j < 3; j++)
            if (!(std::fabs(v[j]) < 1e5f)) { ok = false; break; }
        if (!ok) continue;
        float man = std::fabs(v[0]) + std::fabs(v[1]) + std::fabs(v[2]);
        if (man < 1.5f || (v[1] > -1e3f && v[1] < -50.f)) continue;   // reject tiny/absurd height
        if (v[0] == v[1] && v[1] == v[2]) continue;
        shown++;
        char pm[180];
        snprintf(pm, sizeof pm, "[esp-feed] poff %s @0x%X (%.1f,%.1f,%.1f)%s",
            who, off, v[0], v[1], v[2], off == (int)R.bedPos ? " <-cur" : "");
        Log(pm);
    }
}

static void KlassTag(void* k, char* tag, int n) {            // same, takes klass directly
    tag[0] = 0;
    if (!k || !api::class_get_name) return;
    __try {
        const char* nmz = api::class_get_name(k);
        const char* ns = api::class_get_namespace ? api::class_get_namespace(k) : "";
        snprintf(tag, n, "%s.%s", (ns && *ns) ? ns : "?", nmz ? nmz : "?");
    } __except (EXCEPTION_EXECUTE_HANDLER) { tag[0] = 0; }
}
static void DumpRowFields(void* row, char* out, int cap) {  // ALL instance fields
    out[0] = 0;
    void* rk = rp(row);
    if (!rk || !api::class_get_fields) return;
    int o = 0;
    void* c = rk;
    for (int lvl = 0; c && lvl < 3 && o < cap - 80; lvl++) {
        void* it = nullptr; void* fd;
        __try {
            for (int i = 0; i < 64 && (fd = api::class_get_fields(c, &it)); i++) {
                if (!api::field_get_name(fd) || (api::field_get_flags(fd) & 0x10)) continue;
                const char* fn = api::field_get_name(fd);
                int off = api::field_get_offset(fd);
                void* ft = api::field_get_type ? api::field_get_type(fd) : nullptr;
                const char* tn = ft && api::type_get_name ? (const char*)api::type_get_name(ft) : "?";
                char val[56]; val[0] = 0;
                if (off > 0 && off <= 0x400 && tn) {
                    if (strstr(tn, "String")) {
                        void* sp = rp((char*)row + off);
                        char sv[40]; EscU16(sp, sv, sizeof sv);
                        snprintf(val, sizeof val, "='%s'", sv);
                    } else if (!strcmp(tn, "System.Int64")) {
                        long long iv = 0; rd((char*)row + off, &iv, 8);
                        snprintf(val, sizeof val, "=%lld", iv);
                    } else if (!strcmp(tn, "System.Int32") || !strcmp(tn, "System.Boolean")) {
                        int32_t iv = 0; rd((char*)row + off, &iv, 4);
                        snprintf(val, sizeof val, "=%d", (int)iv);
                    }
                }
                int l = snprintf(out + o, (size_t)(cap - o), " %s:%s@0x%X%s",
                    fn, tn, (unsigned)off, val);
                if (l > 0 && o + l < cap) o += l; else break;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        c = api::class_get_parent ? api::class_get_parent(c) : nullptr;
    }
}
struct ColInfo { char nm[40]; int tp; };
static ColInfo g_cols[64];
static int g_colsN = -1;
static char g_beanNm[48] = {0};
static void* g_nameG = nullptr;  static char g_nameGn[40] = {0};   // String get_*name*
static void* g_keyG  = nullptr;  static char g_keyGn[40]  = {0};   // Int64 get_*Key*
static void* BeanStrAt(void* rowBeanPtr, void* keyStr, int* poff) {  // GetString(col, out off)
    void* r = nullptr;
    if (poff) *poff = -1;
    if (R.mBeanGS && rowBeanPtr && keyStr && api::runtime_invoke) {
        int off = 0; void* argv[2]; argv[0] = keyStr; argv[1] = &off; void* ex = nullptr;
        __try { r = api::runtime_invoke(R.mBeanGS, rowBeanPtr, argv, &ex); }
        __except (EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
        if (ex) r = nullptr; else if (poff) *poff = off;
    }
    return r;
}
static void* BeanLongAt(void* beanP, void* keyStr) {
    void* r = nullptr;
    if (R.mBeanGL && beanP && keyStr && api::runtime_invoke) {
        int off = 0; void* argv[2]; argv[0] = keyStr; argv[1] = &off; void* ex = nullptr;
        __try { r = api::runtime_invoke(R.mBeanGL, beanP, argv, &ex); }
        __except (EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
        if (ex) r = nullptr;
    }
    return r;
}
static void* StrNew(const char* utf8) {
    void* v = nullptr;
    if (utf8 && api::string_new) {
        __try { v = api::string_new(utf8); } __except (EXCEPTION_EXECUTE_HANDLER) { v = nullptr; }
    }
    return v;
}
static void RowProbeGetters(void* k, void* row, char* out, int cap, bool call) {
    out[0] = 0;
    int o = 0, calls = 0, listed = 0;
    if (!k || !api::class_get_methods || !api::method_get_name) return;
    void* it = nullptr; void* m;
    __try {
        for (int i = 0; i < 400 && (m = api::class_get_methods(k, &it)); i++) {
            const char* mn = api::method_get_name(m);
            if (!mn) continue;
            uint32_t ac = api::method_get_param_count ? api::method_get_param_count(m) : 9;
            void* rt = api::method_get_return_type ? api::method_get_return_type(m) : nullptr;
            const char* tn = rt && api::type_get_name ? (const char*)api::type_get_name(rt) : "?";
            bool isStr = tn && strstr(tn, "String") != nullptr;
            bool isL64 = tn && strstr(tn, "Int64") != nullptr;
            if (listed < 30) {
                int l = snprintf(out + o, (size_t)(cap - o), " %s/%u:%.12s", mn, ac, tn ? tn : "?");
                if (l > 0 && o + l < cap) { o += l; listed++; } else break;
            }
            if (!call || ac != 0 || (!isStr && !isL64)) continue;
            void* v = Inv(m, row);
            if (!v) continue;
            int l2;
            if (isStr) {
                char s[40]; EscU16(v, s, 40);
                l2 = snprintf(out + o, (size_t)(cap - o), " >>%s='%s'", mn, s);
                if (!g_nameG && (strstr(mn, "ame") || strstr(mn, "Name"))) {
                    char chk[64]; chk[0] = 0; ReadUtf16(v, chk, 64);
                    if (chk[0]) { g_nameG = m; strncpy(g_nameGn, mn, 39); }
                }
            } else {
                long long iv = 0; UnboxI64(v, iv);
                l2 = snprintf(out + o, (size_t)(cap - o), " >>%s=%lld", mn, iv);
                if (!g_keyG && iv && (strstr(mn, "Key") || strstr(mn, "key"))) {
                    g_keyG = m; strncpy(g_keyGn, mn, 39);
                }
            }
            if (l2 > 0 && o + l2 < cap) o += l2; else break;
            if (++calls >= 20) break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// -------- enemy rows via the PROVEN official enumeration (no TryGetValue) --------
struct RowEnt { char tid[40]; void* row; };
static RowEnt g_rows[128];
static int    g_rowsN = 0;
static void CacheEnemyRows() {
    if (g_rowsN > 0 || !R.etTbl || !R.mEtKey || !R.mEtVal) return;
    int32_t cnt = 0;
    if (R.mEtCnt) UnboxI32(Inv(R.mEtCnt, R.etTbl), cnt);
    if (cnt > 128) cnt = 128;
    for (int32_t i = 0; i < cnt; i++) {
        void* k = InvInt(R.mEtKey, R.etTbl, i);
        void* v = InvInt(R.mEtVal, R.etTbl, i);
        if (!k || !v || rp(k) != R.strCls) continue;
        char ks[40]; ks[0] = 0; ReadUtf16(k, ks, 40);
        if (!ks[0]) continue;
        RowEnt& e = g_rows[g_rowsN];
        memset(&e, 0, sizeof e);
        strncpy(e.tid, ks, 39);
        e.row = v;
        g_rowsN++;
    }
}
static void WalkRowMethods(void* row) {         // parent chain: list + call getters
    char msg[1900]; char acc[1600]; acc[0] = 0; int ao = 0;
    void* k = rp(row);
    for (int lvl = 0; k && lvl < 6; lvl++) {
        char tag[64]; KlassTag(k, tag, sizeof tag);
        char seg[700]; seg[0] = 0;
        RowProbeGetters(k, row, seg, sizeof seg, true);
        int l = snprintf(acc + ao, (size_t)(sizeof acc - ao), " [%s]%s", tag, seg);
        if (l > 0 && ao + l < (int)sizeof acc) ao += l; else break;
        void* pk = nullptr;
        __try { pk = api::class_get_parent ? api::class_get_parent(k) : nullptr; }
        __except (EXCEPTION_EXECUTE_HANDLER) { pk = nullptr; }
        if (pk == k) break;
        k = pk;
    }
    snprintf(msg, sizeof msg, "[mchain] %s", acc);
    QLog(msg);
}
static void DumpBeanSchema(void* row, ColInfo* cols, int* pcolN, const char* label) {
    *pcolN = 0;
    char msg[1200];
    char tag[64]; tag[0] = 0;
    void* cand = rp((char*)row + 0x10);
    RowKlassTag(cand, tag, sizeof tag);
    void* dict = nullptr;
    if (strstr(tag, "Dictionary")) dict = cand;
    else if (strstr(tag, "BeanType")) {
        void* d2 = rp((char*)cand + 0x10);
        char t2[64]; t2[0] = 0; RowKlassTag(d2, t2, sizeof t2);
        snprintf(msg, sizeof msg, "[schema-probe] %s bt dict-> %s", label, t2);
        QLog(msg);
        if (strstr(t2, "Dictionary")) dict = d2;
    } else {
        void* d3 = rp(cand);
        char t3[64]; t3[0] = 0; RowKlassTag(d3, t3, sizeof t3);
        snprintf(msg, sizeof msg, "[schema-probe] %s +0x10->%s inner->%s", label, tag, t3);
        QLog(msg);
        if (strstr(t3, "Dictionary")) dict = d3;
    }
    snprintf(msg, sizeof msg, "[schema-probe] %s row+0x10 -> %s dict=%s", label, tag, dict ? "y" : "n");
    QLog(msg);
    if (!dict) return;
    void* dk = rp(dict);
    int offEnt = -1, offCnt = -1;
    if (dk && api::class_get_fields) {
        void* it = nullptr; void* fd;
        __try {
            for (int i = 0; i < 16 && (fd = api::class_get_fields(dk, &it)); i++) {
                const char* fn = api::field_get_name(fd);
                if (!fn) continue;
                if (!strcmp(fn, "_entries")) offEnt = api::field_get_offset(fd);
                if (!strcmp(fn, "_count"))   offCnt = api::field_get_offset(fd);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (offEnt < 0 || offCnt < 0) return;
    void* ent = rp((char*)dict + offEnt);
    int cnt = 0; rd((char*)dict + offCnt, &cnt, 4);
    if (!ent || cnt <= 0 || cnt > 4096) {
        snprintf(msg, sizeof msg, "[schema-step] ent=%s cnt=%d", ent ? "y" : "n", cnt);
        QLog(msg); return;
    }
    char acc[900]; acc[0] = 0; int ao = 0, shown = 0;
    char* data = (char*)ent + 0x20;
    for (int i = 0; i < cnt && *pcolN < 64; i++) {
        void* kp = rp(data + (size_t)i * 0x18 + 0x8);
        void* fv = rp(data + (size_t)i * 0x18 + 0x10);
        if (!kp || !fv || rp(kp) != R.strCls) continue;
        char cn[48]; cn[0] = 0; ReadUtf16(kp, cn, 48);
        if (!cn[0]) continue;
        char ft[64]; ft[0] = 0; RowKlassTag(fv, ft, sizeof ft);
        if (!strstr(ft, "Field")) continue;              // validate: values are Fields
        int coff = 0, ty = -1;
        rd((char*)fv + 0x18, &coff, 4);
        rd((char*)fv + 0x20, &ty, 4);
        if (*pcolN >= 64) break;
        ColInfo& c = cols[*pcolN];
        memset(&c, 0, sizeof c);
        strncpy(c.nm, cn, 39);
        c.tp = ty;
        (*pcolN)++;
        if (shown < 40) {
            int l = snprintf(acc + ao, (size_t)(sizeof acc - ao), " %s(t%d@%d)", cn, ty, coff);
            if (l > 0 && ao + l < (int)sizeof acc) { ao += l; shown++; }
        }
    }
    snprintf(msg, sizeof msg, "[schema] %s cols=%d/%d:%s", label, *pcolN, cnt, acc);
    QLog(msg);
}
static int CacheRows(void* tbl, void* mCnt, void* mKey, void* mVal,
    RowEnt* arr, int cap) {
    int n = 0;
    if (!tbl || !mKey || !mVal) return 0;
    int32_t cnt = 0;
    if (mCnt) UnboxI32(Inv(mCnt, tbl), cnt);
    if (cnt > cap) cnt = cap;
    for (int32_t i = 0; i < cnt; i++) {
        void* k = InvInt(mKey, tbl, i);
        void* v = InvInt(mVal, tbl, i);
        if (!k || !v || rp(k) != R.strCls) continue;
        char ks[40]; ks[0] = 0; ReadUtf16(k, ks, 40);
        if (!ks[0]) continue;
        RowEnt& e = arr[n];
        memset(&e, 0, sizeof e);
        strncpy(e.tid, ks, 39);
        e.row = v;
        n++;
    }
    return n;
}
struct Ntc { RowEnt rows[4096]; int rowsN; ColInfo cols[64]; int colsN; };
static Ntc g_ntc[21];
struct NameMemo { char tid[40]; char nm[64]; int ok; };
static NameMemo g_memo[512]; static int g_memoN = 0;
static int g_tref = 0;   // [tkey] budget

static int StrQuality(void* s) {                    // 0 garbage/none  1 ascii  2 clean CJK
    if (!s || rp(s) != R.strCls) return 0;
    int len = 0; rd((char*)s + 0x10, &len, 4);
    if (len <= 0 || len > 256) return 0;
    bool ascii = true, cjk = false;
    for (int i = 0; i < len; i++) {
        uint16_t u = 0; if (!rd((char*)s + 0x14 + i * 2, &u, 2)) return 0;
        if (u >= 0xD800 && u <= 0xDFFF) return 0;   // surrogate => binary reinterp
        if (u == 0xFFFD || u == 0xFFFE || u == 0xFFFF) return 0;  // repl/nonchar
        if (u >= 128) ascii = false;
        if (u >= 0x2E80) cjk = true;
    }
    return cjk ? 2 : (ascii ? 1 : 0);
}
static void* InvOff1(void* m, void* obj, int32_t v) {   // argc=1 int32 by-value instance call
    void* r = nullptr;
    if (m && obj && api::runtime_invoke) {
        void* argv[1]; argv[0] = &v; void* ex = nullptr;
        __try { r = api::runtime_invoke(m, obj, argv, &ex); }
        __except (EXCEPTION_EXECUTE_HANDLER) { r = nullptr; }
        if (ex) r = nullptr;
    }
    return r;
}
static int BufStrUtf8(void* ptr, char* out, int cap) {  // len-prefixed/asciiz UTF-8
    out[0] = 0;
    if (!ptr) return 0;
    uint8_t b0 = 0; if (!rd(ptr, &b0, 1)) return 0;
    int len = -1, start = 0;
    if (b0 >= 0xE0 && b0 <= 0xEF) { len = b0; start = 1; }
    else if (b0 >= 0x20 && b0 < 0x80) {
        int i = 0; uint8_t b = 0;
        for (; i < 64; i++) { if (!rd((char*)ptr + i, &b, 1) || b == 0) break; }
        if (i > 1 && i < 64) { len = i; start = 0; }
    }
    if (len <= 0 || len > 64) return 0;
    int o = 0;
    for (int i = 0; i < len; i++) {
        uint8_t b = 0;
        if (!rd((char*)ptr + start + i, &b, 1)) return 0;
        if (start == 0 && i >= 1 && (b < 0x20 || b == 0x7F)) return 0;
        if (o + 4 >= cap) break;
        if (b < 0x80) out[o++] = (char)b;
        else if (b < 0xE0) { out[o++] = (char)(0xC0 | (b >> 6)); out[o++] = (char)(0x80 | (b & 0x3F)); }
        else { out[o++] = (char)(0xE0 | (b >> 12)); out[o++] = (char)(0x80 | ((b >> 6) & 0x3F));
               out[o++] = (char)(0x80 | (b & 0x3F)); }
    }
    out[o] = 0;
    return o;
}
static int BufStrU16(void* ptr, char* out, int cap) {   // {int len} + UTF-16LE
    out[0] = 0;
    if (!ptr) return 0;
    int32_t n = 0; rd(ptr, &n, 4);
    if (n <= 0 || n > 48) return 0;
    bool bad = false; int o = 0;
    for (int i = 0; i < n; i++) {
        uint16_t u = 0;
        if (!rd((char*)ptr + 4 + i * 2, &u, 2) || u == 0 || u == 0xFFFD ||
            (u >= 0xD800 && u <= 0xDFFF)) { bad = true; break; }
        if (o + 4 >= cap) break;
        if (u < 0x80) out[o++] = (char)u;
        else if (u < 0x800) { out[o++] = (char)(0xC0 | (u >> 6)); out[o++] = (char)(0x80 | (u & 0x3F)); }
        else { out[o++] = (char)(0xE0 | (u >> 12)); out[o++] = (char)(0x80 | ((u >> 6) & 0x3F));
               out[o++] = (char)(0x80 | (u & 0x3F)); }
    }
    out[o] = 0;
    return (bad || o == 0) ? 0 : o;
}
static bool LangKeyTry(const char* ascii, char* disp, int cap) {
    void* sk = (ascii && *ascii) ? StrNew(ascii) : nullptr;
    I18nCall d = I18nTry(R.mI18nTry, sk);
    void* t2 = d.out;
    if (!d.ex && t2 && rp(t2) == R.strCls) {
        char nm[64]; nm[0] = 0; ReadUtf16(t2, nm, 64);
        if (nm[0]) { strncpy(disp, nm, (size_t)cap - 1); disp[cap - 1] = 0; return true; }
    }
    return false;
}
static bool TryTblNames(int t, void* row, char* disp, int cap, char* why, int wcap) {
    Ntc& tc = g_ntc[t];
    char* beanP = (char*)row + 0x10;
    void* root = rp(beanP + 8);
    void* reader = root ? rp((char*)root + 0x10) : nullptr;
    char* buf = reader ? (char*)rp((char*)reader + 0x20) : nullptr;
    for (int pass = 0; pass < 2; pass++) {
        for (int ci = 0; ci < tc.colsN && ci < 48; ci++) {
            char lcn[48]; int j = 0;
            for (; tc.cols[ci].nm[j] && j < 47; j++) {
                char ch = tc.cols[ci].nm[j];
                lcn[j] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
            }
            lcn[j] = 0;
            bool textish = (tc.cols[ci].tp == 184693291);
            if (!textish) continue;                              // Text cols only
            bool nameish = strstr(lcn, "name") || strstr(lcn, "title") ||
                (strstr(lcn, "label") && !strstr(lcn, "icon"));
            if ((pass == 0) != nameish) continue;
            void* key = StrNew(tc.cols[ci].nm);
            if (!key) continue;
            int32_t foff = 0;
            void* v = BeanStrAt(beanP, key, &foff);
            int q = StrQuality(v);
            if (buf && foff >= 0) {                        // Text ref -> u64 i18n key
                int32_t ref = 0, dOff = 0;
                rd(beanP + 0x10, &dOff, 4);
                long long keyv = 0;
                if (rd(buf + dOff + foff, &ref, 4) && ref > 0 && ref < 0x40000000) {
                    rd(buf + ref, &keyv, 8);
                    if (keyv && R.mItTV) {
                        Call2 c2 = InvObj2(R.mItTV, R.itTbl, &keyv);
                        void* txt = c2.out;
                        int qt = StrQuality(txt);
                        if (qt == 2) {
                            char sv[64]; sv[0] = 0; ReadUtf16(txt, sv, 64);
                            if (sv[0] && !strchr(sv, '<') && !strchr(sv, '@')) {
                                snprintf(why, wcap, "%s.%s i18nkey", kNtShort[t], tc.cols[ci].nm);
                                strncpy(disp, sv, (size_t)cap - 1); disp[cap - 1] = 0; return true;
                            }
                        }
                        if (g_tref < 12) {
                            g_tref++;
                            char pv[80]; pv[0] = 0;
                            if (txt && rp(txt) == R.strCls) EscU16(txt, pv, sizeof pv);
                            char msg[220];
                            snprintf(msg, sizeof msg, "[tkey] %s %s ref=%d key=%lld q=%d v='%s'",
                                kNtShort[t], tc.cols[ci].nm, ref, keyv, qt, pv);
                            QLog(msg);
                        }
                    }
                    if (wcap && !why[0])
                        snprintf(why, wcap, "%s %s key=%lld miss", kNtShort[t], tc.cols[ci].nm, keyv);
                }
                if (wcap && !why[0])
                    snprintf(why, wcap, "%s %s noref(foff=%d)", kNtShort[t], tc.cols[ci].nm, foff);
            }
            if (q == 2) {
                char sv[64]; sv[0] = 0; ReadUtf16(v, sv, 64);
                if (sv[0]) {
                    snprintf(why, wcap, "%s.%s direct", kNtShort[t], tc.cols[ci].nm);
                    strncpy(disp, sv, (size_t)cap - 1); disp[cap - 1] = 0; return true;
                }
            }
            if (q == 1) {
                char sv[64]; sv[0] = 0; ReadUtf16(v, sv, 64);
                if (LangKeyTry(sv, disp, cap)) {
                    snprintf(why, wcap, "%s.%s langkey", kNtShort[t], tc.cols[ci].nm);
                    return true;
                }
                if (wcap && !why[0])
                    snprintf(why, wcap, "%s.%s key='%s' tk-miss", kNtShort[t], tc.cols[ci].nm, sv);
            }
            if (!nameish && R.mItTV) {                      // long -> i18n
                void* lb = BeanLongAt(beanP, StrNew(tc.cols[ci].nm));
                long long lk = 0;
                if (lb && UnboxI64(lb, lk) && lk) {
                    Call2 c2 = InvObj2(R.mItTV, R.itTbl, &lk);
                    void* txt = c2.out;
                    if (txt && rp(txt) == R.strCls) {
                        char nm[64]; nm[0] = 0; ReadUtf16(txt, nm, 64);
                        if (nm[0]) {
                            snprintf(why, wcap, "%s.%s i18n=%lld", kNtShort[t], tc.cols[ci].nm, lk);
                            strncpy(disp, nm, (size_t)cap - 1); disp[cap - 1] = 0; return true;
                        }
                    }
                }
            }
        }
    }
    if (wcap && !why[0]) snprintf(why, wcap, "all-miss");
    return false;
}
static int g_rcdump = 0;
static void RowColsDump(int t, void* row) {
    Ntc& tc = g_ntc[t];
    char* beanP = (char*)row + 0x10;
    void* root = rp(beanP + 8);
    void* reader = root ? rp((char*)root + 0x10) : nullptr;
    char* buf = reader ? (char*)rp((char*)reader + 0x20) : nullptr;
    int32_t dOff = 0; rd(beanP + 0x10, &dOff, 4);
    for (int ci = 0; ci < tc.colsN && ci < 6; ci++) {
        void* key = StrNew(tc.cols[ci].nm);
        if (!key) continue;
        int32_t foff = -1;
        void* v = BeanStrAt(beanP, key, &foff);
        int q = StrQuality(v);
        char pv[48]; pv[0] = 0;
        if (q > 0 && v) EscU16(v, pv, sizeof pv);
        int32_t ref = 0; long long kv = 0;
        if (buf && foff >= 0 && rd(buf + dOff + foff, &ref, 4) &&
            ref > 0 && ref < 0x40000000)
            rd(buf + ref, &kv, 8);
        char msg[220];
        snprintf(msg, sizeof msg, "[rowcols] %s %s foff=%d ref=%d key=%lld q=%d v='%s'",
            kNtShort[t], tc.cols[ci].nm, foff, ref, kv, q, pv);
        QLog(msg);
    }
}
static int g_tplD = 0;
static int g_jLoud = 10;
static int g_etDbg = 48;
static char g_tplTid[16][48];
static float g_cp[3] = { 0, 0, 0 };
static void TplDump(void* data, const char* tid) {   // where does pickup-UI text live?
    void* td = rp((char*)data + R.bedTmpl);
    if (!td) return;
    char cls[80]; cls[0] = 0; KlassTag(rp(td), cls, sizeof cls);
    char msg[220];
    snprintf(msg, sizeof msg, "[esp-feed] tpl '%s' td='%s'", tid, cls);
    QLog(msg);
    __try {
        void* kl = rp(td);
        int shown = 0;
        for (int lvl = 0; kl && lvl < 6 && shown < 14; lvl++) {
            char ltag[60]; ltag[0] = 0; KlassTag(kl, ltag, sizeof ltag);
            void* it = nullptr; void* fd; int cntL = 0;
            for (int i = 0; i < 80 && (fd = api::class_get_fields(kl, &it)) != nullptr; i++) {
                const char* fn = api::field_get_name(fd);
                long off = api::field_get_offset(fd);
                if (!fn || !fn[0] || off < 0x10 || off > 0x600) continue;
                cntL++;
                void* p = rp((char*)td + off);
                if (p && rp(p) == R.strCls) {
                    char sv[80]; sv[0] = 0; ReadUtf16(p, sv, 80);
                    if (sv[0]) {
                        snprintf(msg, sizeof msg, "[esp-feed] tpl .%s='%s'", fn, sv);
                        QLog(msg);
                        if (++shown >= 14) break;
                    }
                }
            }
            snprintf(msg, sizeof msg, "[esp-feed] tpl L%d cls='%s' fields=%d str=%d", lvl, ltag, cntL, shown);
            QLog(msg);
            if (!api::class_get_parent) break;
            void* pk = api::class_get_parent(kl);
            if (!pk || pk == kl) break;
            kl = pk;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { Log("[esp-feed] tpl faulted"); }
}
struct TidAlias { const char* tid; const char* nm; };
struct TidPrefix { const char* pre; const char* nm; };
static const TidPrefix g_palias[] = {   // env fauna / civilian NPCs have no cfg display name
    { "npc_obj_",       "\xE7\x8E\xAF\xE5\xA2\x83\xE7\x94\x9F\xE7\x89\xA9" },
    { "int_trchest_",   "\xE7\x89\xA9\xE8\xB5\x84\xE7\xAE\xB1" },
    { "int_doodad_",    "\xE9\x87\x87\xE9\x9B\x86\xE7\x89\xA9" },
    { "int_collection_", "\xE6\x94\xB6\xE9\x9B\x86\xE5\x93\x81" },
    { "int_drop_",      "\xE6\x8E\x89\xE8\x90\xBD\xE7\x89\xA9" },
    { "npc_tpl_",       "NPC" },
    { "npc_lady_",      "NPC" },
    { "npc_girl_",      "NPC" },
    { "npc_boy_",       "NPC" },
    { "npc_gentleman_", "NPC" },
    { "npc_strongm_",   "NPC" },
    { "npc_kids_",      "NPC" },
    { "npc_spl_",       "NPC" },
    { "int_system_world_energy",  "\xE8\x83\xBD\xE9\x87\x8F\xE6\xB7\xA4\xE7\xA7\xAF\xE7\x82\xB9" },
};
static const TidAlias g_alias[] = {
    { "int_trchest_common_normal", "\xE6\x99\xAE\xE9\x80\x9A\xE7\x89\xA9\xE8\xB5\x84\xE7\xAE\xB1" },
    { "int_trchest_common",        "\xE7\x89\xA9\xE8\xB5\x84\xE7\xAE\xB1" },
    { "int_trchest_lock",          "\xE4\xB8\x8A\xE9\x94\x81\xE7\x89\xA9\xE8\xB5\x84\xE7\xAE\xB1" },
    { "int_erosion_sludge_core",       "\xE8\x83\xBD\xE9\x87\x8F\xE6\xB7\xA4\xE7\xA7\xAF\xE7\x82\xB9" },
    { "int_Hyper_erosion_sludge_core", "\xE9\x87\x8D\xE5\xBA\xA6\xE8\x83\xBD\xE9\x87\x8F\xE6\xB7\xA4\xE7\xA7\xAF\xE7\x82\xB9" },
};
static bool EtName(void* tidStr, char* disp, int cap, char* dbg, int dbgcap) {
    dbg[0] = 0;
    if (!tidStr || rp(tidStr) != R.strCls) return false;
    char tid[48]; tid[0] = 0; ReadUtf16(tidStr, tid, 48);
    if (!tid[0]) return false;
    for (int mi = 0; mi < g_memoN; mi++)
        if (!strcmp(g_memo[mi].tid, tid)) {
            if (g_memo[mi].ok) {
                strncpy(disp, g_memo[mi].nm, (size_t)cap - 1); disp[cap - 1] = 0;
                snprintf(dbg, dbgcap, "memo"); return true;
            }
            snprintf(dbg, dbgcap, "memo-miss"); return false;
        }
    int fT = -1, fI = -1;
    for (int t = 0; t < 21 && fT < 0; t++)
        for (int i = 0; i < g_ntc[t].rowsN; i++)
            if (!strcmp(g_ntc[t].rows[i].tid, tid)) { fT = t; fI = i; break; }
    bool hit = false;
    if (fT >= 0) {
        char why[96]; why[0] = 0;
        hit = TryTblNames(fT, g_ntc[fT].rows[fI].row, disp, cap, why, sizeof why);
        if (why[0]) snprintf(dbg, dbgcap, "%s", why);
        if (!hit && g_rcdump < 4) { g_rcdump++; RowColsDump(fT, g_ntc[fT].rows[fI].row); }
    } else if (!strncmp(tid, "npc_", 4) && strlen(tid) < 39) {   // _gNN group join
        for (int gg = 1; gg <= 5 && !hit; gg++) {
            char t2[48];
            snprintf(t2, sizeof t2, "%s_g%02d", tid, gg);
            for (int i = 0; i < g_ntc[16].rowsN; i++)
                if (!strcmp(g_ntc[16].rows[i].tid, t2)) {
                    char why2[96]; why2[0] = 0;
                    hit = TryTblNames(16, g_ntc[16].rows[i].row, disp, cap, why2, sizeof why2);
                    snprintf(dbg, dbgcap, "g%02d %s", gg, why2[0] ? why2 : (hit ? "ok" : "miss"));
                    break;
                }
        }
    }
    static bool g_inJoin = false;
    if (!hit && fT >= 0 && !g_inJoin) {          // itemId 2-hop join (real cfg name)
        Ntc& jc = g_ntc[fT];
        void* jrow = g_ntc[fT].rows[fI].row;
        for (int ci = 0; ci < jc.colsN; ci++) {
            char lcn[48]; int j = 0;
            for (; jc.cols[ci].nm[j] && j < 47; j++) {
                char ch = jc.cols[ci].nm[j];
                lcn[j] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
            }
            lcn[j] = 0;
            if (!strstr(lcn, "itemid")) continue;
            void* jk = StrNew(jc.cols[ci].nm);
            if (!jk) continue;
            int32_t jfo = 0;
            void* v = BeanStrAt((char*)jrow + 0x10, jk, &jfo);
            if (!v || rp(v) != R.strCls || StrQuality(v) != 1) continue;
            char iv[48]; iv[0] = 0; ReadUtf16(v, iv, 48);
            if (!iv[0] || strncmp(iv, "item_", 5)) continue;
            g_inJoin = true;
            char jdbg[80];
            bool jok = EtName(v, disp, cap, jdbg, sizeof jdbg);
            g_inJoin = false;
            if (g_jLoud > 0) {
                g_jLoud--;
                char jm[200];
                snprintf(jm, sizeof jm, "[esp-feed] join %s.%s='%s' -> %s",
                    kNtShort[fT], jc.cols[ci].nm, iv, jok ? disp : "(fail)");
                QLog(jm);
            }
            if (jok) snprintf(dbg, dbgcap, "join %s", jdbg);
            hit = jok;
            break;
        }
    }
    if (!hit && fT < 0 && !strncmp(tid, "int_doodad_", 11)) {   // ddG twin: picks_<X>
        char cand[48]; snprintf(cand, sizeof cand, "picks_%s", tid + 11);
        char cand2[48]; strncpy(cand2, cand, 47); cand2[47] = 0;
        char* us = strrchr(cand2, '_');
        bool digs = false;
        if (us) {
            digs = (us[1] != 0);
            for (char* p2 = us + 1; *p2; p2++) if (*p2 < '0' || *p2 > '9') digs = false;
        }
        if (digs) *us = 0; else cand2[0] = 0;
        char* cands[2] = { cand, cand2 };
        static const int pickT[2] = { 7, 8 };
        for (int cq = 0; cq < 2 && !hit; cq++) {
            if (!cands[cq][0]) continue;
            for (int q = 0; q < 2 && !hit; q++)
                for (int i = 0; i < g_ntc[pickT[q]].rowsN && !hit; i++)
                    if (!strcmp(g_ntc[pickT[q]].rows[i].tid, cands[cq])) {
                        char why3[96]; why3[0] = 0;
                        hit = TryTblNames(pickT[q], g_ntc[pickT[q]].rows[i].row, disp, cap, why3, sizeof why3);
                        snprintf(dbg, dbgcap, "twin %s", why3[0] ? why3 : (hit ? "ok" : "miss"));
                        if (g_jLoud > 0) { g_jLoud--;
                            char jm[200];
                            snprintf(jm, sizeof jm, "[esp-feed] twin '%s'[%s:%s] -> %s",
                                tid, kNtShort[pickT[q]], cands[cq], hit ? disp : "(fail)");
                            QLog(jm); }
                    }
        }
    }
    if (!hit && fT == 11) {                       // markId join -> mapMarkTemp display name
        Ntc& mc = g_ntc[11];
        void* mrow = mc.rows[fI].row;
        for (int ci = 0; ci < mc.colsN; ci++) {
            char lcn[48]; int jj = 0;
            for (; mc.cols[ci].nm[jj] && jj < 47; jj++) {
                char ch = mc.cols[ci].nm[jj];
                lcn[jj] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
            }
            lcn[jj] = 0;
            if (!strstr(lcn, "markid")) continue;
            void* jk = StrNew(mc.cols[ci].nm);
            if (!jk) continue;
            int32_t mfo = 0;
            void* mv = BeanStrAt((char*)mrow + 0x10, jk, &mfo);
            if (!mv || rp(mv) != R.strCls || StrQuality(mv) != 1) continue;
            char mvs[48]; mvs[0] = 0; if (mv) ReadUtf16(mv, mvs, 48);
            {   // one-time loud look at what markId actually holds
                static bool g_mkSeen = false;
                if (!g_mkSeen) { g_mkSeen = true; char mk[120];
                    snprintf(mk, sizeof mk, "[esp-feed] mkjoin '%s' markId='%s' str=%d", tid, mvs[0] ? mvs : "(null)", mv ? 1 : 0);
                    QLog(mk); if (g_espq) { g_forceQ = 1; RowColsDump(11, mrow); g_forceQ = 0; } }
            }
            if (!mvs[0]) continue;
            char mkc[56]; snprintf(mkc, sizeof mkc, "mark_%s", mvs);
            for (int cand = 0; cand < 2 && !hit; cand++) {
                const char* want = cand ? mkc : mvs;
                for (int i = 0; i < g_ntc[18].rowsN; i++)
                    if (!strcmp(g_ntc[18].rows[i].tid, want)) {
                        char why4[96]; why4[0] = 0;
                        hit = TryTblNames(18, g_ntc[18].rows[i].row, disp, cap, why4, sizeof why4);
                        snprintf(dbg, dbgcap, "mk %s %s", want, why4[0] ? why4 : (hit ? "ok" : "miss"));
                        break;
                    }
            }
            if (!hit) {                       // LIKE pass: keys often carry _N suffixes
                size_t ml = strlen(mvs);
                static int g_mkl = 6;
                for (int i = 0; i < g_ntc[18].rowsN; i++) {
                    const char* kt = g_ntc[18].rows[i].tid;
                    if (strncmp(kt, mvs, ml) || (kt[ml] != 0 && kt[ml] != '_')) continue;
                    if (g_mkl > 0) { g_mkl--; char em[170];
                        snprintf(em, sizeof em, "[esp-feed] mklike '%s' ~ '%s'", mvs, kt); QLog(em); }
                    char why4[96]; why4[0] = 0;
                    hit = TryTblNames(18, g_ntc[18].rows[i].row, disp, cap, why4, sizeof why4);
                    snprintf(dbg, dbgcap, "mk~ %s %s", kt, why4[0] ? why4 : (hit ? "ok" : "miss"));
                    if (hit) break;
                }
            }
            if (!hit) {                       // value join: mkT.markInfoId ~ markId
                Ntc& kc = g_ntc[18];
                static int g_mkvl = 10;
                for (int ci = 0; ci < kc.colsN && !hit; ci++) {
                    char lcn[48]; int jj = 0;
                    for (; kc.cols[ci].nm[jj] && jj < 47; jj++) {
                        char ch = kc.cols[ci].nm[jj];
                        lcn[jj] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
                    }
                    lcn[jj] = 0;
                    if (!strstr(lcn, "markinfoid")) continue;
                    void* jk2 = StrNew(kc.cols[ci].nm);
                    if (!jk2) break;
                    size_t ml = strlen(mvs);
                    for (int i = 0; i < kc.rowsN && !hit; i++) {
                        int32_t fo = 0;
                        void* v = BeanStrAt((char*)kc.rows[i].row + 0x10, jk2, &fo);
                        if (!v || rp(v) != R.strCls || StrQuality(v) != 1) continue;
                        char iv[64]; iv[0] = 0; ReadUtf16(v, iv, 64);
                        if (!iv[0] || strncmp(iv, mvs, ml) || (iv[ml] != 0 && iv[ml] != '_')) continue;
                        if (g_mkvl > 0) { g_mkvl--; char em[200];
                            snprintf(em, sizeof em, "[esp-feed] mkval '%s' = mkT[%d].'%s'", mvs, i, kc.rows[i].tid);
                            QLog(em); }
                        char why4[96]; why4[0] = 0;
                        hit = TryTblNames(18, kc.rows[i].row, disp, cap, why4, sizeof why4);
                        snprintf(dbg, dbgcap, "mkv %s", why4[0] ? why4 : (hit ? "ok" : "miss"));
                    }
                    break;
                }
            }
            if (!hit) {                       // recon: show what similar mkT keys look like
                char tok[24]; int ti = 0;
                const char* q = strchr(mvs, '_'); q = q ? q + 1 : mvs;   // skip "mark_"
                while (*q && ti < 23) tok[ti++] = *q++;
                tok[ti] = 0;
                static int g_mkr = 6;
                for (int i = 0; i < g_ntc[18].rowsN && g_mkr > 0; i++)
                    if (strstr(g_ntc[18].rows[i].tid, tok)) {
                        g_mkr--;
                        char em[170];
                        snprintf(em, sizeof em, "[esp-feed] mkre '%s' ~ '%s'", tok, g_ntc[18].rows[i].tid);
                        QLog(em);
                    }
            }
            break;
        }
    }
    if (!hit && !strcmp(tid, "int_system_world_energy_point")) {   // real name via gm category row
        static int g_ecatL = 4;
        for (int i = 0; i < g_ntc[20].rowsN; i++)
            if (!strcmp(g_ntc[20].rows[i].tid, "world_energy_point")) {
                char why5[96]; why5[0] = 0;
                hit = TryTblNames(20, g_ntc[20].rows[i].row, disp, cap, why5, sizeof why5);
                snprintf(dbg, dbgcap, "ecat %s", why5[0] ? why5 : (hit ? "ok" : "miss"));
                if (g_ecatL > 0) { g_ecatL--; char em[160];
                    snprintf(em, sizeof em, "[esp-feed] ecat world_energy_point -> %s", hit ? disp : "(fail)");
                    QLog(em); }
                break;
            }
    }
    if (!hit && fT == 13 && g_rcdump < 3) {      // chest row: loud col dump
        g_rcdump++; g_forceQ = 1; RowColsDump(fT, g_ntc[fT].rows[fI].row); g_forceQ = 0;
    }
    if (!hit) {                                // alias fallback (display labels)
        for (int ai = 0; ai < (int)(sizeof g_alias / sizeof g_alias[0]); ai++)
            if (!strcmp(tid, g_alias[ai].tid)) {
                strncpy(disp, g_alias[ai].nm, (size_t)cap - 1); disp[cap - 1] = 0;
                snprintf(dbg, dbgcap, "alias");
                hit = true;
                break;
            }
    }
    if (!hit)
        for (int pi = 0; pi < (int)(sizeof g_palias / sizeof g_palias[0]); pi++) {
            size_t pl = strlen(g_palias[pi].pre);
            if (!strncmp(tid, g_palias[pi].pre, pl)) {
                strncpy(disp, g_palias[pi].nm, (size_t)cap - 1); disp[cap - 1] = 0;
                snprintf(dbg, dbgcap, "palias");
                hit = true;
                break;
            }
        }
    if (!hit && !dbg[0]) snprintf(dbg, dbgcap, fT >= 0 ? "row-noname" : "notid");
    if (g_memoN < 512) {                           // memo both outcomes
        NameMemo& mm = g_memo[g_memoN];
        memset(&mm, 0, sizeof mm);
        strncpy(mm.tid, tid, 39);
        mm.ok = hit ? 1 : 0;
        if (hit) strncpy(mm.nm, disp, 63);
        g_memoN++;
    }
    return hit;
}

static void ProbeSparkTables() {         // one-shot diagnostics on the two named tables
    char msg[1900];
    int32_t cnt = 0;
    if (R.etTbl && R.mEtCnt) UnboxI32(Inv(R.mEtCnt, R.etTbl), cnt);
    snprintf(msg, sizeof msg, "[spark] enemy tbl=%s cnt=%d key=%s val=%s",
        R.etTbl ? "y" : "n", cnt, R.mEtKey ? "y" : "n", R.mEtVal ? "y" : "n");
    QLog(msg);
    { char tm[1400];
      if (R.etTbl) { tm[0] = 0; RowProbeGetters(rp(R.etTbl), R.etTbl, tm, sizeof tm, false);
                     snprintf(msg, sizeof msg, "[tbl-mth] enemy %s", tm); QLog(msg); }
      if (R.itTbl) { tm[0] = 0; RowProbeGetters(rp(R.itTbl), R.itTbl, tm, sizeof tm, false);
                     snprintf(msg, sizeof msg, "[tbl-mth] i18n %s", tm); QLog(msg); } }
    for (int32_t i = 0; i < cnt && i < 6; i++) {
        void* k = InvInt(R.mEtKey, R.etTbl, i);
        void* v = InvInt(R.mEtVal, R.etTbl, i);
        char ks[64]; ks[0] = 0;
        if (k && R.strCls && rp(k) == R.strCls) ReadUtf16(k, ks, 64);
        char fk[512]; fk[0] = 0; char gk[1100]; gk[0] = 0; char tag[64]; tag[0] = 0;
        if (v) {
            RowKlassTag(v, tag, sizeof tag);
            RowProbeGetters(rp(v), v, gk, sizeof gk, false);
            DumpRowFields(v, fk, sizeof fk);
        }
        snprintf(msg, sizeof msg, "[et-probe] %d k='%s' vcls=%s MTH:%s FLD:%s",
            i, ks, tag, gk, fk);
        QLog(msg);
    }
    { char bm[1400]; bm[0] = 0;
      if (R.beanCls) RowProbeGetters(R.beanCls, nullptr, bm, sizeof bm, false);
      snprintf(msg, sizeof msg, "[bean-mth] %s", bm); QLog(msg); }
    CacheEnemyRows();
    if (g_rowsN > 0) {
        DumpBeanSchema(g_rows[0].row, g_cols, &g_colsN, "enemy");
        WalkRowMethods(g_rows[0].row);
    }
    for (int t = 0; t < 21; t++) {
        Ntc& tc = g_ntc[t];
        tc.rowsN = 0; tc.colsN = 0;
        if (!R.nTbl[t]) { snprintf(msg, sizeof msg, "[ntab] %s missing", kNtShort[t]); QLog(msg); continue; }
        int32_t tcnt = 0;
        if (R.nCnt[t]) UnboxI32(Inv(R.nCnt[t], R.nTbl[t]), tcnt);
        tc.rowsN = CacheRows(R.nTbl[t], R.nCnt[t], R.nKey[t], R.nVal[t], tc.rows, 4096);
        void* v0 = R.nVal[t] ? InvInt(R.nVal[t], R.nTbl[t], 0) : nullptr;
        void* k0 = R.nKey[t] ? InvInt(R.nKey[t], R.nTbl[t], 0) : nullptr;
        char ks[56]; ks[0] = 0;
        if (k0 && rp(k0) == R.strCls) ReadUtf16(k0, ks, 56);
        snprintf(msg, sizeof msg, "[ntab] %s cnt=%d cached=%d key0=%s",
            kNtShort[t], tcnt, tc.rowsN, ks);
        QLog(msg);
        if (v0) DumpBeanSchema(v0, tc.cols, &tc.colsN, kNtShort[t]);
    }
    for (int t = 0; t < 21; t++) {
        Ntc& tc = g_ntc[t];
        if (tc.rowsN <= 0) continue;
        char nm2[64]; nm2[0] = 0; char db[96]; db[0] = 0;
        void* tk = StrNew(tc.rows[0].tid);
        bool okc = tk && EtName(tk, nm2, 64, db, sizeof db);
        snprintf(msg, sizeof msg, "[et-name] %s(%s) -> %s [%s]",
            tc.rows[0].tid, kNtShort[t], okc ? nm2 : "(fail)", db);
        QLog(msg);
    }
    static const char* const kGim[] = { "int_switch_", "int_rope", "int_campfire",
        "int_bamboo", "int_snapshot", "int_narrative", "int_trigger", "int_empty",
        "int_system_world_energy", "int_xirang", "int_fixable", "int_leader_butterfly",
        "int_small_butterfly", "int_challenge_start", "int_water" };
    auto GimHit = [&](const char* s) {
        for (int gi = 0; gi < (int)(sizeof kGim / sizeof kGim[0]); gi++)
            if (!strncmp(s, kGim[gi], strlen(kGim[gi]))) return true;
        return false;
    };
    {   // [tabscan] census: every String-keyed Spark table in Beyond.Cfg.Tables
        void* tk = FindClass("Beyond.Cfg", "Tables");
        if (tk && api::class_get_fields && api::field_get_name && api::field_get_flags) {
            void* sit = nullptr; void* fd; int shown = 0, scanned = 0, findLeft = 40, deepLeft = 40, vLeft = 80;
            for (int i = 0; i < 1500 && (fd = api::class_get_fields(tk, &sit)); i++) {
                const char* fn = api::field_get_name(fd);
                if (!fn || fn[0] != 's' || fn[1] != '_') continue;
                if (!(api::field_get_flags(fd) & 0x10)) continue;
                scanned++;
                void* tbl = nullptr; void* mC = nullptr, *mK = nullptr, *mV = nullptr; (void)mV;
                ResolveSparkTable(tk, fn, &tbl, &mC, &mK, &mV, nullptr);
                if (!tbl) continue;
                int32_t tcnt = 0;
                if (mC) UnboxI32(Inv(mC, tbl), tcnt);
                if (tcnt <= 0 || tcnt > 20000) continue;
                void* k0 = mK ? InvInt(mK, tbl, 0) : nullptr;
                char ks[40]; ks[0] = 0;
                if (k0 && rp(k0) == R.strCls) ReadUtf16(k0, ks, 40);
                if (!ks[0]) continue;
                if (shown < 700) {
                    shown++;
                    char sm[160];
                    const char* mk = (!strncmp(ks, "int_tr", 6) || !strncmp(ks, "npc_", 4) ||
                        !strncmp(ks, "picks", 5) || !strncmp(ks, "world_", 6)) ? "!!" : "  ";
                    snprintf(sm, sizeof sm, "[tabscan]%s %s cnt=%d key0=%s", mk, fn, tcnt, ks);
                    QLog(sm);
                }
                if (mK && findLeft > 0) {          // needle hunt: home tables of strays
                    static const char* const needles[] = {
                        "int_doodad_grass_1", "int_doodad_insect_1", "int_collection_common",
                        "int_collection_item_2", "npc_obj_fowl_hs_01",
                        "npc_lady_riverbandits_a_17", "npc_tpl_boy_hsworker_a_06" };
                    int perTbl = 0;
                    int lim = tcnt < 64 ? 64 : (deepLeft > 0 ? tcnt : 64);  // big tables: full scan once
                    if (tcnt >= 64 && deepLeft > 0) { deepLeft--; }
                    for (int i = 0; i < tcnt && i < lim && findLeft > 0 && perTbl < 3; i++) {
                        void* kk = InvInt(mK, tbl, i);
                        char kv[40]; kv[0] = 0;
                        if (kk && rp(kk) == R.strCls) ReadUtf16(kk, kv, 40);
                        if (!kv[0]) continue;
                        bool m = !strncmp(kv, "int_trchest_", 12) || !strncmp(kv, "int_doodad_", 11)
                              || !strncmp(kv, "int_collection_", 15) || !strncmp(kv, "world_energy", 12)
                              || GimHit(kv);
                        if (!m) for (int ni = 0; ni < (int)(sizeof needles / sizeof needles[0]); ni++)
                            if (!strcmp(kv, needles[ni])) { m = true; break; }
                        if (m) {
                            findLeft--; perTbl++;
                            char fm[160];
                            snprintf(fm, sizeof fm, "[esp-feed] hit %s [%d]='%s' cnt=%d", fn, i, kv, tcnt);
                            QLog(fm);
                        }
                    }
                }
                if (mV && vLeft > 0) {            // VALUE-level: does anyone reference gimmick tids?
                    int vlim = tcnt < 400 ? tcnt : 400;
                    int vPer = 0;
                    for (int i = 0; i < vlim && vLeft > 0 && vPer < 3; i++) {
                        void* vv = InvInt(mV, tbl, i);
                        if (!vv) continue;
                        if (rp(vv) == R.strCls) {
                            char vs[40]; vs[0] = 0; ReadUtf16(vv, vs, 40);
                            if (vs[0] && GimHit(vs)) {
                                vLeft--; vPer++;
                                char fm[180];
                                snprintf(fm, sizeof fm, "[esp-feed] vhit %s [%d] v='%s' cnt=%d", fn, i, vs, tcnt);
                                QLog(fm);
                            }
                        }
                    }
                }
            }
            char sm[120];
            snprintf(sm, sizeof sm, "[tabscan] done scanned=%d shown=%d", scanned, shown);
            QLog(sm);
        }
    }
    cnt = 0;
    if (R.itTbl && R.mItCnt) UnboxI32(Inv(R.mItCnt, R.itTbl), cnt);
    snprintf(msg, sizeof msg, "[spark] i18n tbl=%s cnt=%d", R.itTbl ? "y" : "n", cnt);
    QLog(msg);
    for (int32_t i = 0; i < cnt && i < 6; i++) {
        void* k = InvInt(R.mItKey, R.itTbl, i);
        void* v = InvInt(R.mItVal, R.itTbl, i);
        long long kk = 0; if (k) UnboxI64(k, kk);
        char vs[96]; vs[0] = 0;
        if (v && R.strCls && rp(v) == R.strCls) EscU16(v, vs, 96);
        snprintf(msg, sizeof msg, "[i18n-probe] %d k=%lld v=%s", i, kk, vs);
        QLog(msg);
    }
    {   // marker-bridge census: where does each intMk.markId actually live?
        int mkci = -1; void* jkM = nullptr;
        {
            Ntc& kc = g_ntc[18];
            for (int ci = 0; ci < kc.colsN; ci++) {
                char lcn[48]; int jj = 0;
                for (; kc.cols[ci].nm[jj] && jj < 47; jj++) {
                    char ch = kc.cols[ci].nm[jj];
                    lcn[jj] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
                }
                lcn[jj] = 0;
                if (strstr(lcn, "markinfoid")) { mkci = ci; jkM = StrNew(kc.cols[ci].nm); break; }
            }
        }
        for (int i = 0; i < g_ntc[11].rowsN && i < 12; i++) {
            void* row = g_ntc[11].rows[i].row;
            void* jk = StrNew((char*)"markId");
            int32_t fo = 0;
            void* v = jk ? BeanStrAt((char*)row + 0x10, jk, &fo) : nullptr;
            char mid[48]; mid[0] = 0;
            if (v && rp(v) == R.strCls && StrQuality(v) == 1) ReadUtf16(v, mid, 48);
            char em[280];
            snprintf(em, sizeof em, "[esp-feed] mkmk '%s' markId='%s' home=",
                g_ntc[11].rows[i].tid, mid[0] ? mid : "(null)");
            int hl = (int)strlen(em);
            if (!mid[0]) { snprintf(em + hl, sizeof em - hl, "(n/a) mv=-"); }
            else {
                bool any = false;
                for (int t = 0; t < 21 && !any; t++)
                    for (int j = 0; j < g_ntc[t].rowsN; j++)
                        if (!strcmp(g_ntc[t].rows[j].tid, mid)) {
                            snprintf(em + hl, sizeof em - hl, "%s[%d]", kNtShort[t], j);
                            any = true; break;
                        }
                if (!any) snprintf(em + hl, sizeof em - hl, "none");
            }
            if (mid[0] && mkci >= 0) {          // value join: mkT.markInfoId ~ markId
                size_t ml = strlen(mid); int found = -1;
                for (int j = 0; j < g_ntc[18].rowsN; j++) {
                    int32_t fo = 0;
                    void* v2 = BeanStrAt((char*)g_ntc[18].rows[j].row + 0x10, jkM, &fo);
                    if (!v2 || rp(v2) != R.strCls || StrQuality(v2) != 1) continue;
                    char iv[64]; iv[0] = 0; ReadUtf16(v2, iv, 64);
                    if (!strncmp(iv, mid, ml) && (iv[ml] == 0 || iv[ml] == '_')) { found = j; break; }
                }
                snprintf(em + strlen(em), sizeof em - strlen(em), found >= 0 ? " mv=mkT[%d]~'%s'" : " mv=none",
                    found >= 0 ? found : 0, found >= 0 ? g_ntc[18].rows[found].tid : "");
            } else snprintf(em + strlen(em), sizeof em - strlen(em), " mv=-");
            QLog(em);
        }
        if (mkci >= 0) {                        // column sanity samples
            for (int s2 = 0; s2 < 4; s2++) {
                int32_t fo = 0;
                void* v2 = BeanStrAt((char*)g_ntc[18].rows[s2].row + 0x10, jkM, &fo);
                char iv[64]; iv[0] = 0;
                if (v2 && rp(v2) == R.strCls && StrQuality(v2) == 1) ReadUtf16(v2, iv, 64);
                char em2[240];
                snprintf(em2, sizeof em2, "[esp-feed] mkiv[%d] '%s' markInfoId='%s'",
                    s2, g_ntc[18].rows[s2].tid, iv[0] ? iv : "(null)");
                QLog(em2);
            }
        }
    }
    g_rcdump = 0;   // startup probes burned the budget; real autopsy needs its own
}

// --------------------------------- walk ------------------------------------
static int WalkEntities(Block* b) {
    int n = 0;
    void* mgr = GetStatic(R.fSManager);
    if (!mgr) return -1;                              // menu / not in scene yet
    void* coll = rp((char*)mgr + R.nodes);
    void* list = nullptr;
    if (coll) {
        void* ck = rp(coll);                          // Il2CppObject._klass
        if (ck) {
            if (ck != g_collKlass) { g_collKlass = ck; R.collList = OffOf(ck, "m_list"); }
            if (R.collList != ~0ull) list = rp((char*)coll + R.collList);
        }
    }
    if (!list) return 0;
    if (R.mgrCharPos != ~0ull) rd((char*)mgr + R.mgrCharPos, g_cp, 12);
    int32_t size = 0; rd((char*)list + 0x18, &size, 4);
    void* items = rp((char*)list + 0x10);
    if (!items || size <= 0) return 0;
    if (size > MAXE) size = MAXE;
    for (int i = 0; i < size && n < MAXE; i++) {
        void* slot = (char*)items + 0x20 + (size_t)i * 16;   // List<ValueTuple<K,V>>: 8+8
        void* node = rp((char*)slot + 8);
        if (!node) { if (g_census) cNull++; continue; }
        void* data = rp((char*)node + R.nodeData);
        bool dead = false;
        if (!rd((char*)node + R.nodeDead, &dead, 1) || dead) {
            if (g_census) {
                cDead++;
                if (dead && g_dsd > 0 && data) { g_dsd--;      // autopsy swallowed nodes
                    char dk[60]; dk[0] = 0; KlassTag(rp(data), dk, sizeof dk);
                    void* t2 = (R.bedTpl != ~0ull) ? rp((char*)data + R.bedTpl) : nullptr;
                    char dt[48]; dt[0] = 0; if (t2) ReadUtf16(t2, dt, 48);
                    char dm[170]; snprintf(dm, sizeof dm, "[esp-feed] dead '%s' '%s'",
                        dk[0] ? dk : "?", dt[0] ? dt : "?");
                    Log(dm);
                }
            }
            continue;
        }
        if (!data) { if (g_census) cNod++; continue; }
        float p[3];
        if (!rd((char*)data + R.bedPos, p, 12) ||
            !(std::fabs(p[0]) < 1e6f && std::fabs(p[1]) < 1e6f && std::fabs(p[2]) < 1e6f)) {
            if (g_census) cPos++;
            continue;
        }
        Rec& r = b->ents[n];
        void* k = rp(data);
        int tp = 0;
        {   // inherit-chain classify: NpcInfo/EnemyInfo subclasses must not fall gray
            void* kk = k;
            for (int lvl = 0; kk && lvl < 6; lvl++) {
                if (kk == R.enemyCls) { tp = 1; break; }
                if (kk == R.npcCls)   { tp = 2; break; }
                if (kk == R.charCls)  { tp = 3; break; }
                kk = api::class_get_parent ? api::class_get_parent(kk) : nullptr;
            }
        }
        if (tp == 3) {                      // character-class NPC must render as NPC,
            void* t4 = (R.bedTpl != ~0ull) ? rp((char*)data + R.bedTpl) : nullptr;   // not vanish
            char ct[48]; ct[0] = 0; if (t4) ReadUtf16(t4, ct, 48);
            if (!strncmp(ct, "npc_", 4)) tp = 2;
            if (g_census) { cChar++;
                if (g_csd > 0) { g_csd--; char dm[160];
                    snprintf(dm, sizeof dm, "[esp-feed] charw '%s'", ct[0] ? ct : "?"); Log(dm); }
            }
        }
        if (!tp) {                          // tid-prefix fallback
            void* ts = (R.bedTpl != ~0ull) ? rp((char*)data + R.bedTpl) : nullptr;
            char tb[48]; tb[0] = 0; if (ts) ReadUtf16(ts, tb, 48);
            if (!strncmp(tb, "npc_", 4)) tp = 2;
            else if (!strncmp(tb, "eny_", 4)) tp = 1;
            if (tp == 2 && g_espq) {
                static int g_npf = 3;
                if (g_npf > 0) { g_npf--; char gm[140];
                    snprintf(gm, sizeof gm, "[esp-feed] npcfall '%s'", tb); QLog(gm); }
            }
        }
        if (!tp && g_espq) {                // who is still gray? (diag only)
            char kt[60]; kt[0] = 0; KlassTag(k, kt, sizeof kt);
            static std::unordered_map<std::string, int> g_gseen;
            int& cg = g_gseen[kt[0] ? kt : "(null)"];
            if (cg < 2 && (int)g_gseen.size() < 14) {
                cg++; char gm[130];
                snprintf(gm, sizeof gm, "[esp-feed] gray '%s' x%d", kt[0] ? kt : "(null)", cg);
                QLog(gm);
            } else cg++;
        }
        r.type = tp;
        if (g_census) {
            if (tp == 2) { cNpc++;
                if (g_nsd > 0) { g_nsd--;
                    void* t3 = (R.bedTpl != ~0ull) ? rp((char*)data + R.bedTpl) : nullptr;
                    char nw[48]; nw[0] = 0; if (t3) ReadUtf16(t3, nw, 48);
                    char dm[160]; snprintf(dm, sizeof dm, "[esp-feed] npcw '%s'", nw[0] ? nw : "?");
                    Log(dm);
                }
            }
            float man = std::fabs(p[0]) + std::fabs(p[1]) + std::fabs(p[2]);
            if (man < 1.5f) { cZero++;          // origin-pos suspect (transform-only entity?)
                if (g_zsd > 0) { g_zsd--;
                    void* tz = (R.bedTpl != ~0ull) ? rp((char*)data + R.bedTpl) : nullptr;
                    char zt[48]; zt[0] = 0; if (tz) ReadUtf16(tz, zt, 48);
                    char ztag[60]; ztag[0] = 0; KlassTag(k, ztag, sizeof ztag);
                    char dm2[180]; snprintf(dm2, sizeof dm2, "[esp-feed] zerow '%s' '%s'",
                        ztag[0] ? ztag : "?", zt[0] ? zt : "?");
                    Log(dm2);
                }
            }
            {   float fx = p[0] - g_cp[0], fy = p[1] - g_cp[1], fz = p[2] - g_cp[2];
                float d2f = fx * fx + fy * fy + fz * fz;
                if (d2f < 625.f) cNear++;   // counted here, captured post-FillName
                if (d2f > 250000.f) {
                    cFar++;                                   // > 500 m from hero
                    if (tp == 2 && g_fw > 0) {                // far NPC: autopsy coords
                        g_fw--;
                        void* tf = (R.bedTpl != ~0ull) ? rp((char*)data + R.bedTpl) : nullptr;
                        char ft[48]; ft[0] = 0; if (tf) ReadUtf16(tf, ft, 48);
                        char dm2[200];
                        snprintf(dm2, sizeof dm2, "[esp-feed] farw '%s' d=%.0fm (%.1f,%.1f,%.1f)",
                            ft[0] ? ft : "?", (float)std::sqrt(d2f), p[0], p[1], p[2]);
                        Log(dm2);
                        if (g_ps > 0) { g_ps--; PosScan(data, ft[0] ? ft : "?"); }
                    }
                }
            }
            if (tp == 0 && g_gsd > 0) {         // gray tid samples
                g_gsd--;
                void* tg = (R.bedTpl != ~0ull) ? rp((char*)data + R.bedTpl) : nullptr;
                char gt[48]; gt[0] = 0; if (tg) ReadUtf16(tg, gt, 48);
                char dm2[160]; snprintf(dm2, sizeof dm2, "[esp-feed] grik '%s'", gt[0] ? gt : "?");
                Log(dm2);
            }
            if (tp == 0) { char kt[60]; kt[0] = 0; KlassTag(k, kt, sizeof kt);
                int s3 = 0; for (; s3 < g_ckN; s3++) if (!strncmp(g_cklass[s3], kt, 59)) break;
                if (s3 < g_ckN) g_ckn[s3]++;
                else if (g_ckN < 10) {
                    strncpy(g_cklass[g_ckN], kt[0] ? kt : "(null)", 59);
                    g_cklass[g_ckN][59] = 0; g_ckn[g_ckN] = 1; g_ckN++;
                }
            }
        }
        r.pos[0] = p[0]; r.pos[1] = p[1]; r.pos[2] = p[2];
        r.id = 0;
        if (R.bedId != ~0ull) rd((char*)data + R.bedId, &r.id, 8);
        r.name[0] = 0;
        FillName(data, r);
        if (g_census) {
            float nx = r.pos[0] - g_cp[0], ny = r.pos[1] - g_cp[1], nz = r.pos[2] - g_cp[2];
            float dn = nx * nx + ny * ny + nz * nz;
            if (dn < 625.f && g_nearN < 40) {                  // EXACT wire record GUI receives
                snprintf(g_near[g_nearN++], 95, "'%s' tp%d %.1fm",
                    r.name[0] ? r.name : "(empty)", r.type, (float)std::sqrt(dn));
            }
        }
        n++;
    }
    if (R.mgrCharPos != ~0ull) rd((char*)mgr + R.mgrCharPos, b->h.charPos, 12);
    if (g_census) {
        {   // static-layer recon: every List field on manager trio (instance + static)
            void* trio[3] = { R.mgrCls, R.nodeCls, R.bedCls };
            static const char* trioNm[3] = { "mgr", "node", "data" };
            int bL = 18;
            for (int t = 0; t < 3; t++) {
                void* cls = trio[t];
                if (!cls) continue;
                if (api::runtime_class_init) { __try { api::runtime_class_init(cls); }
                    __except (EXCEPTION_EXECUTE_HANDLER) {} }
                void* itf = nullptr;
                while (bL > 0) {
                    void* fd = api::class_get_fields ? api::class_get_fields(cls, &itf) : nullptr;
                    if (!fd) break;
                    const char* fnm = api::field_get_name(fd);
                    if (!fnm) continue;
                    void* ty = api::field_get_type ? api::field_get_type(fd) : nullptr;
                    const char* tn = (ty && api::type_get_name) ? api::type_get_name(ty) : nullptr;
                    if (!tn || !strstr(tn, "List")) continue;
                    uint32_t fl = api::field_get_flags ? (uint32_t)api::field_get_flags(fd) : 0;
                    bool isStatic = (fl & 0x10) != 0;
                    void* lst = nullptr;
                    if (isStatic) {
                        if (!api::field_static_get_value) continue;
                        __try { api::field_static_get_value(fd, &lst); }
                        __except (EXCEPTION_EXECUTE_HANDLER) { lst = nullptr; }
                    } else {
                        uintptr_t off = (uintptr_t)api::field_get_offset(fd);
                        if (!mgr || off < 0x10 || off > 0x400) continue;
                        lst = rp((char*)mgr + off);
                    }
                    int32_t cnt = -1;
                    if (lst) __try { cnt = *(int32_t*)((char*)lst + 0x18); }
                        __except (EXCEPTION_EXECUTE_HANDLER) { cnt = -1; }
                    if (cnt < 0 || cnt > 200000) continue;
                    bL--;
                    char dm2[170];
                    snprintf(dm2, sizeof dm2, "[esp-feed] %s.%s%s cnt=%d",
                        trioNm[t], fnm, isStatic ? "(S)" : "", cnt);
                    Log(dm2);
                }
            }
            CamCensus();
            static const char* kGuess[] = { "StaticInfo", "StaticEntityInfo", "SceneNpcInfo",
                "NpcStaticInfo", "PlacementInfo", "StaticObjectInfo", "SceneObjectInfo",
                "InteractableInfo", "StaticInteractiveInfo", "DecoInfo", "MarkerInfo", "PropInfo" };
            for (int gg = 0; gg < 12; gg++) {
                void* fc = FindClass("Beyond.Gameplay.Core", kGuess[gg]);
                if (!fc) fc = FindClass("Beyond.Gameplay", kGuess[gg]);
                if (fc) { char dm2[140];
                    snprintf(dm2, sizeof dm2, "[esp-feed] clsf '%s' FOUND", kGuess[gg]); Log(dm2); }
            }
        }
        char dm[220];
        snprintf(dm, sizeof dm, "[esp-feed] census it=%d wr=%d npc=%d char=%d dead=%d nod=%d pos=%d nul=%d zero=%d far=%d near=%d",
            (int)size, n, cNpc, cChar, cDead, cNod, cPos, cNull, cZero, cFar, cNear);
        Log(dm);
        for (int q2 = 0; q2 < g_nearN; q2++) {
            snprintf(dm, sizeof dm, "[esp-feed] nearw %s", g_near[q2]);
            Log(dm);
        }
        for (int z = 0; z < g_ckN; z++) {
            snprintf(dm, sizeof dm, "[esp-feed] grak '%s' x%d", g_cklass[z], g_ckn[z]);
            Log(dm);
        }
        g_census = 0;
    }
    return n;
}

// display-name resolver: templateId -> BaseTemplateData.name, cached per tid
static std::unordered_map<std::string, std::string> g_nameCache;
static int g_dbgLeft = 150;
static bool TryRows(void* keyStr, char* dispBuf, int cap, const char* tag) {
    for (size_t ti = 0; ti < g_tbls.size(); ti++) {
        Call2 rw = InvObj2(g_tbls[ti].mTV, g_tbls[ti].obj, keyStr);
        void* row = (!rw.ex && rw.res) ? rw.out : nullptr;
        if (!row) continue;
        bool dbg = (R.rowDbg > 0);
        if (dbg) { R.rowDbg--;
            char dm[220]; snprintf(dm, sizeof dm, "[row-dbg] %s tbl=%s row=%p", tag, g_tbls[ti].nm, row);
            QLog(dm); }
        for (int off = 0x10; off < 0x150; off += 8) {
            void* q = rp((char*)row + off);
            if (!q || !R.strCls || rp(q) != R.strCls) continue;
            char sv[64]; sv[0] = 0; ReadUtf16(q, sv, 64);
            if (!sv[0]) continue;
            if (dbg) { char dm[220]; snprintf(dm, sizeof dm, "[row-fld] @0x%X '%s'", off, sv); QLog(dm); }
            bool hasCJK = false;
            for (char* p2 = sv; *p2; p2++) if ((unsigned char)*p2 > 127) { hasCJK = true; break; }
            if (hasCJK) { strncpy(dispBuf, sv, (size_t)cap - 1); dispBuf[cap - 1] = 0; return true; }
            I18nCall d2 = I18nTry(R.mI18nTry, q);
            if (!d2.ex && d2.out) {
                char nm2[48]; nm2[0] = 0; ReadUtf16(d2.out, nm2, 48);
                if (nm2[0]) { strncpy(dispBuf, nm2, (size_t)cap - 1); dispBuf[cap - 1] = 0;
                              if (dbg) { char dm[220]; snprintf(dm, sizeof dm, "[row-key] @0x%X -> %s", off, nm2); QLog(dm); }
                              return true; }
            }
        }
        break;
    }
    return false;
}
static void FillName(void* data, Rec& r) {
    if (R.bedTpl == ~0ull) return;
    if (!R.dictPeeked) { R.dictPeeked = true; PeekTextMap(); ProbeSparkTables(); }
    void* ts = rp((char*)data + R.bedTpl);
    if (!ts) return;
    char tid[48]; tid[0] = 0; ReadUtf16(ts, tid, 48);
    if (!tid[0]) return;
    auto it = g_nameCache.find(tid);
    if (it == g_nameCache.end()) {
        char dispBuf[48]; std::strcpy(dispBuf, tid);
        char tplName[48]; tplName[0] = 0;
        if (R.bedTmpl != ~0ull && R.btnName != ~0ull) {
            void* td = rp((char*)data + R.bedTmpl);
            void* ns = td ? rp((char*)td + R.btnName) : nullptr;
            if (ns) ReadUtf16(ns, tplName, 48);
        }
        if (tplName[0]) std::strcpy(dispBuf, tplName);
        char exn[96]; exn[0] = 0;
        bool lkOk = false;
        if (R.mI18nTry && tplName[0] && std::strcmp(tplName, tid) != 0) {
            bool cjk = false;                          // shot 0: tplName itself
            for (const char* q = tplName; *q; q++) if ((unsigned char)*q > 127) { cjk = true; break; }
            if (cjk) lkOk = true;                      // already displayable
            else if (LangKeyTry(tplName, dispBuf, 48)) lkOk = true;
        }
        if (!lkOk && R.mI18nTry) {
            I18nCall d = I18nTry(R.mI18nTry, ts);                      // shot 1: templateId
            ExName(d.ex, exn, sizeof exn);
            if (!d.ex && d.out) {
                char nm[48]; nm[0] = 0; ReadUtf16(d.out, nm, 48);
                if (nm[0]) { std::strcpy(dispBuf, nm); lkOk = true; }
            }
            if (!lkOk) {                                               // shot 2: templateData.id
                void* td2 = rp((char*)data + R.bedTmpl);
                void* idStr = td2 ? rp((char*)td2 + 0x10) : nullptr;
                if (idStr && R.strCls && rp(idStr) == R.strCls) {
                    I18nCall e = I18nTry(R.mI18nTry, idStr);
                    if (!e.ex && e.out) {
                        char nm2[48]; nm2[0] = 0; ReadUtf16(e.out, nm2, 48);
                        if (nm2[0]) { std::strcpy(dispBuf, nm2); lkOk = true; }
                    }
                }
            }
            if (!lkOk) lkOk = TryRows(ts, dispBuf, 48, "key=tid");     // shot 3: string-key cfg sweep
            if (!lkOk) {                                               // shot 4: level entityDataIdKey
                void* lev = rp((char*)data + 0xD0);
                void* edk = lev ? rp((char*)lev + 0x30) : nullptr;
                if (edk && R.strCls && rp(edk) == R.strCls) {
                    char ek[64]; ek[0] = 0; ReadUtf16(edk, ek, 64);
                    if (ek[0] && strstr(tid, "eny") && R.rowDbg > 0)
                        snprintf(exn + 30, 60, " edk=%s", ek);
                    if (ek[0]) {
                        I18nCall d3 = I18nTry(R.mI18nTry, edk);
                        if (!d3.ex && d3.out) {
                            char nm3[48]; nm3[0] = 0; ReadUtf16(d3.out, nm3, 48);
                            if (nm3[0]) { std::strcpy(dispBuf, nm3); lkOk = true; }
                        }
                        if (!lkOk) lkOk = TryRows(edk, dispBuf, 48, "key=edk");
                    }
                }
            }
            if (!lkOk) {                       // shot 5: enemy cfg table
                char edbg[80]; edbg[0] = 0;
                char edisp[48]; edisp[0] = 0;
                if (EtName(ts, edisp, 48, edbg, sizeof edbg) && edisp[0]) {
                    std::strcpy(dispBuf, edisp); lkOk = true;
                }
                if (g_etDbg > 0 && !strstr(edbg, "memo")) {
                    g_etDbg--;
                    char dm[220]; snprintf(dm, sizeof dm, "[et-shot] %s -> %s [%s]",
                        tid, lkOk ? dispBuf : "(fail)", edbg);
                    QLog(dm);
                }
            }
            if (!lkOk && exn[0] == 0) std::strcpy(exn, "miss");
            if (!lkOk && exn[0] == 45) std::strcpy(exn, "miss");
        }
        bool combatTid = strstr(tid, "eny") || strstr(tid, "enemy") || strstr(tid, "boss")
                          || strstr(tid, "chest") || strstr(tid, "inter") || strstr(tid, "item");
        if (g_dbgLeft > 0) {   (void)combatTid;
            g_dbgLeft--;
            char msg[280];
            snprintf(msg, sizeof msg, "[esp-name] tid=%s tplName=%s langKey=%s(%s)",
                tid, tplName[0] ? tplName : "(empty)", lkOk ? dispBuf : "(fail)",
                exn[0] ? exn : "-");
            QLog(msg);
        }
        std::string disp = dispBuf;
        if (g_nameCache.size() < 4096) it = g_nameCache.emplace(tid, disp).first;
        else { memcpy(r.name, tid, 48); return; }
    }
    strncpy(r.name, it->second.c_str(), 47);
    {   // whatever appears within 15 m, echo its box text once
        static char g_b15[24][48]; static int g_b15N = 0;
        float dx = r.pos[0] - g_cp[0], dy = r.pos[1] - g_cp[1], dz = r.pos[2] - g_cp[2];
        if (dx * dx + dy * dy + dz * dz < 225.f && g_b15N < 24) {
            bool dup = false;
            for (int i = 0; i < g_b15N; i++) if (!strcmp(g_b15[i], tid)) { dup = true; break; }
            if (!dup) {
                strncpy(g_b15[g_b15N], tid, 47); g_b15[g_b15N][47] = 0; g_b15N++;
                char m[160]; snprintf(m, sizeof m, "[esp-feed] box15 '%s' -> '%s'",
                    tid, it->second.c_str());
                QLog(m);
            }
        }
    }
    {   // energy-point families: one box echo each (raw or resolved)
        static char g_boxTid[6][48]; static int g_boxN = 0;
        if (!strncmp(tid, "world_energy", 12)) {
            bool dup = false;
            for (int i = 0; i < g_boxN; i++) if (!strcmp(g_boxTid[i], tid)) { dup = true; break; }
            if (!dup && g_boxN < 6) {
                strncpy(g_boxTid[g_boxN], tid, 47); g_boxTid[g_boxN][47] = 0; g_boxN++;
                char m[160]; snprintf(m, sizeof m, "[esp-feed] box '%s' -> '%s'",
                    tid, it->second == tid ? "(RAW)" : it->second.c_str());
                QLog(m);
            }
        }
    }
    bool wantFam = !strncmp(tid, "int_doodad_", 11) || !strncmp(tid, "int_collection_", 15) ||
        !strncmp(tid, "int_trchest_", 12) || !strncmp(tid, "picks_", 6) ||
        !strncmp(tid, "int_drop_", 9) || !strncmp(tid, "world_energy", 12) ||
        !strncmp(tid, "int_system_world_energy", 21);
    if (it->second == tid && g_tplD < 16 && wantFam) {    // unresolved: autopsy when NEARBY
        float dx = r.pos[0] - g_cp[0], dy = r.pos[1] - g_cp[1], dz = r.pos[2] - g_cp[2];
        float d2 = dx * dx + dy * dy + dz * dz;   // 30 m radius
        if (d2 < 900.f && d2 > 0.01f) {
            bool dup = false;
            for (int i = 0; i < g_tplD; i++) if (!strcmp(g_tplTid[i], tid)) { dup = true; break; }
            if (!dup) {
                strncpy(g_tplTid[g_tplD], tid, 47); g_tplTid[g_tplD][47] = 0; g_tplD++;
                char jd[48]; jd[0] = 0; char jdbg[80]; jdbg[0] = 0;
                bool ok2 = EtName(ts, jd, 48, jdbg, sizeof jdbg);
                char m[200];
                snprintf(m, sizeof m, "[esp-feed] near '%s' d=%.0fm -> %s [%s]",
                    tid, (float)std::sqrt(d2), ok2 ? jd : "(fail)", jdbg[0] ? jdbg : "-");
                QLog(m);
                TplDump(data, tid);
            }
        }
    }
}

// --------------------------------- thread ----------------------------------
static volatile LONG g_on = 0;
static volatile LONG g_run = 0;
static HANDLE g_map = nullptr;
static Block* g_b = nullptr;

#ifndef _MSC_VER
inline int GetCurrentThreadStackLimits(ULONG_PTR*, ULONG_PTR*) { return 0; }
#endif
static DWORD WINAPI Loop(LPVOID) {
    {   // attach this thread to IL2CPP + Boehm GC: kills teleport-load races
        int gc = GcPause();
        if (api::thread_attach && api::get_domain) {
            void* dom = nullptr;
            __try { dom = api::get_domain(); } __except (EXCEPTION_EXECUTE_HANDLER) { dom = nullptr; }
            __try { if (dom) api::thread_attach(dom); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        typedef int (__fastcall * gcreg_t)(void*);
        gcreg_t reg = api::gc_register_my_thread ? (gcreg_t)api::gc_register_my_thread : nullptr;
        const char* via = "api";
        if (!reg) {
            HMODULE ga = GetModuleHandleA("GameAssembly.dll");
            reg = ga ? (gcreg_t)GetProcAddress(ga, "GC_register_my_thread") : nullptr;
            via = "manual";
        }
        int rc = -9;
        if (reg) {
            struct SB { void* mem_base; void* reg_base; } sb;
            ULONG_PTR low = 0, high = 0;
            GetCurrentThreadStackLimits(&low, &high);
            sb.mem_base = (void*)high; sb.reg_base = nullptr;
            __try { rc = reg(&sb); }
            __except (EXCEPTION_EXECUTE_HANDLER) { rc = -1; }
        }
        char m[96]; snprintf(m, sizeof m, "[esp-feed] gcreg rc=%d via=%s", rc, reg ? via : "none");
        Log(m);
        GcResume(gc);
    }
    InterlockedExchange(&g_run, 1);
    Block* b = g_b;
    InterlockedExchange(&b->h.alive, 1);
    int miss = 0;
    while (g_on) {
        int gc = GcPause();
        int n = WalkEntities(b);
        CamData cd = GrabCam();
        GcResume(gc);
        InterlockedIncrement(&b->h.seq);
        if (n >= 0) b->h.count = n; else miss++;
        b->h.camOk = cd.ok ? 1 : 0;
        if (cd.ok) {
            memcpy(b->h.camPos, cd.pos, 12); memcpy(b->h.camFwd, cd.f, 12);
            memcpy(b->h.camRight, cd.r, 12); memcpy(b->h.camUp, cd.u, 12);
            b->h.fov = cd.fov;
        }
        b->h.tick = GetTickCount64();
        InterlockedIncrement(&b->h.seq);
        if (miss > 600) { Log("[esp-feed] EntityManager still null (menu?)"); miss = 0; }
        Sleep(16);
    }
    InterlockedExchange(&b->h.count, 0);
    InterlockedExchange(&b->h.alive, 0);
    InterlockedExchange(&g_run, 0);
    return 0;
}

static void Stop() {
    if (!g_on) return;
    g_on = 0;
    for (int i = 0; i < 40 && g_run; i++) Sleep(50);
}

inline void Toggle() {
    if (g_on) { Stop(); Log("[esp-feed] OFF"); return; }
    if (!api::initialized) { Log("[esp-feed] il2cpp api not ready"); return; }
    g_nameCache.clear(); g_memoN = 0; g_rcdump = 0; g_tplD = 0;
    g_dbgLeft = 150; g_etDbg = 48; g_jLoud = 10; g_tref = 0;
    {   // external diag switch: <game dir>\esp_feed.cfg  line "diag=1" -> full console
        FILE* cf = fopen("esp_feed.cfg", "r");
        if (cf) {
            int dq = g_espq; char ln[80];
            while (fgets(ln, sizeof ln, cf)) {
                if (!strncmp(ln, "diag=", 5)) { g_espq = atoi(ln + 5) ? 1 : 0; break; }
            }
            fclose(cf);
            if (g_espq != dq) Log(g_espq ? "[esp-feed] diag=ON (cfg)" : "[esp-feed] diag=OFF (cfg)");
        }
    }
    g_census = 1; cDead = cNod = cPos = cNull = cNpc = 0;
    g_dsd = 6; g_nsd = 6; g_csd = 4; g_zsd = 4; g_gsd = 4; g_ckN = 0; g_fw = 4;
    g_ps = 2;
    cZero = cFar = cNear = 0; g_nearN = 0;
    Log("[esp-feed] diag re-armed: caches cleared, budgets refilled");
    if (!g_b) {
        g_map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                   (DWORD)sizeof(Block), "Local\\EndfieldEsp");
        if (!g_map) { Log("[esp-feed] CreateFileMapping failed (err=... see doc)"); return; }
        void* v = MapViewOfFile(g_map, FILE_MAP_WRITE, 0, 0, 0);
        if (!v) { CloseHandle(g_map); g_map = nullptr; Log("[esp-feed] MapViewOfFile failed"); return; }
        g_b = (Block*)v;
    }
    if (!Resolve()) { Log("[esp-feed] resolve FAILED - feed not started"); return; }
    memset((void*)g_b, 0, sizeof(Block));
    g_b->h.magic = MAGIC; g_b->h.ver = VER;
    InterlockedExchange(&g_on, 1);
    CreateThread(nullptr, 0, Loop, nullptr, 0, nullptr);
    Log("[esp-feed] build=stable59 ON 60Hz -> mapping 'Local\\EndfieldEsp' (start endfield_esp.exe)");
}

} // namespace espfeed

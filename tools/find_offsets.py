#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
find_offsets.py —— 从 Il2CppDumper 的 dump.cs 挖掘候选偏移
用法:  python find_offsets.py <Il2CppDumper输出目录> [项目目录=.]
产出:  <项目>/config/offsets.suggested.hpp + 终端候选报告

能力边界:
  * 实例字段 offset: dump.cs 注释即真实值, 直接采用
  * static 字段: dump.cs 的 `// 0xN` 是 static bucket 索引, 非运行时地址
    → 标 TODO, 用 probe.exe chain/list 在活进程上验证
"""
import os, re, sys

CLASS_RE = re.compile(r"^\s*(?:public|private|internal|protected)[\w ]*?\b(class|struct)\s+([A-Za-z_]\w*)")
FIELD_RE = re.compile(r"^\s*(.*?[\w>\]])\s+(\w+);\s*//\s*(0x[0-9A-Fa-f]+)\s*$")

CLS_KW    = re.compile(r"manager|controll|entity|actor|monster|enemy|character|unit|world|stage|scene|drop|item|collect|object", re.I)
POS_RE    = re.compile(r"pos|position|world|loc|trans", re.I)
TYPE_RE   = re.compile(r"type|kind|categor", re.I)
NAME_RE = re.compile(r"name|title", re.I)
HEIGHT_RE = re.compile(r"height|tall|scale", re.I)

def parse_dump_cs(path):
    classes, cur = [], None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = CLASS_RE.match(line)
            if m:
                cur = {"name": m.group(2), "fields": []}
                classes.append(cur)
                continue
            if cur is None or "(" in line:
                continue
            m = FIELD_RE.match(line)
            if not m:
                continue
            decl, name, off = m.group(1), m.group(2), int(m.group(3), 16)
            toks = decl.split()
            ftype = toks[-1] if toks else ""
            cur["fields"].append({
                "type": ftype, "name": name, "off": off,
                "static": any(t == "static" for t in toks),
            })
    return classes

def score(cls):
    s, hits = 0, {}
    if CLS_KW.search(cls["name"]): s += 2
    lists = [f for f in cls["fields"] if f["static"] and "<" in f["type"]
             and re.search(r"List|Collection|array", f["type"])]
    if lists: s += 4; hits["static_lists"] = lists
    inst = [f for f in cls["fields"] if not f["static"]]
    pos  = [f for f in inst if f["type"].endswith("Vector3") and POS_RE.search(f["name"])]
    tp   = [f for f in inst if f["type"] in ("System.Int32","System.UInt32","System.Byte") and TYPE_RE.search(f["name"])]
    nm   = [f for f in inst if f["type"].endswith("String") and NAME_RE.search(f["name"])]
    hgt  = [f for f in inst if f["type"].endswith("Single") and HEIGHT_RE.search(f["name"])]
    for key, lst in (("pos", pos), ("type", tp), ("name", nm), ("height", hgt)):
        if lst: s += 2; hits[key] = lst[0]
    return s + min(len(pos), 3), hits

def main():
    if len(sys.argv) < 2: sys.exit(__doc__)
    d = sys.argv[1]
    proj = sys.argv[2] if len(sys.argv) > 2 else "."
    cs = d if os.path.isfile(d) else os.path.join(d, "dump.cs")
    if not os.path.exists(cs): sys.exit(f"not found: {cs}")
    classes = parse_dump_cs(cs)
    cand = [c for c in classes if CLS_KW.search(c["name"])]
    if not cand: sys.exit("无匹配候选类——扩大 CLS_KW 后重试")
    ranked = sorted(((score(c), c) for c in cand), key=lambda x: -x[0][0])[:12]

    print(f"=== 候选类 Top{len(ranked)} (共解析 {len(classes)} 类) ===")
    for (s, h), c in ranked:
        print(f"\n[{s:>3}] {c['name']}  fields={len(c['fields'])}")
        for f in h.get("static_lists", []):
            print(f"      static集合  {f['type']} {f['name']}  // dump.cs={hex(f['off'])} 是bucket索引,需运行时验证")
        for k in ("pos", "height", "type", "name"):
            if k in h:
                f = h[k]
                print(f"      {k:<10}+{hex(f['off']):>7}  {f['type']} {f['name']}")

    (s, h), best = ranked[0]
    out = [f"// 由 find_offsets.py 生成 —— 实例字段为实测 dump 值; static 需运行时验证",
           "#pragma once", "#include <cstdint>", "namespace off {",
           f"// 最佳候选: {best['name']} (score={s})", ""]
    def emit(k, v, c): out.append(f"inline constexpr uintptr_t {k} = {v};  // {c}")
    g = lambda k, a: h[k][a]
    emit("Ent_Pos",    hex(g("pos","off"))    if "pos"    in h else "0x60", g("pos","name")    if "pos"    in h else "TODO")
    emit("Ent_Height", hex(g("height","off")) if "height" in h else "0",    g("height","name") if "height" in h else "未找到→用 defHeight")
    emit("Ent_TypeId", hex(g("type","off"))   if "type"   in h else "0",    g("type","name")   if "type"   in h else "未找到")
    emit("Ent_Name",   hex(g("name","off"))   if "name"   in h else "0",    g("name","name")   if "name"   in h else "未找到")
    if h.get("static_lists"):
        f = h["static_lists"][0]
        out += [f"// TODO static {f['type']} {f['name']}：CE 值扫描→指针扫描得 EntityList_RVA/Chain,",
                f"//      或 probe.exe chain <rva> <offs...> 逐跳验证后填入",
                f"inline constexpr uintptr_t EntityList_RVA = 0x0;  // 必须运行时确定",
                f"inline constexpr uintptr_t EntityList_Chain[] = {{0x0}};"]
    out += ["} // namespace off", ""]
    tgt = os.path.join(proj, "config", "offsets.suggested.hpp")
    os.makedirs(os.path.dirname(tgt), exist_ok=True)
    open(tgt, "w", encoding="utf-8").write("\n".join(out))
    print(f"\n>>> 已写出 {tgt}")
    print(">>> 视图矩阵: probe.exe mat A.txt →转视角→ mat B.txt → matdiff A B → 移动者即活跃矩阵 → sig 固化")

if __name__ == "__main__":
    main()

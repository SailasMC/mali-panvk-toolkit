#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_shim.py —— 从 NDK 的 vulkan 头文件解析出 MobileGL 需要的全部 loader 风格入口，
自动生成 C 转发垫片代码（shim_gen.inc）。

输入:
  /root/shimgen/vkund.txt        —— 125 个 MGL 未定义 vk* 符号（每行一个）
环境:
  /opt/android-ndk-r27c/.../sysroot/usr/include/vulkan/*.h
输出:
  /root/shimgen/shim_gen.inc
  /root/shimgen/gen_report.txt
"""
import os
import re
import sys
import collections

SYSROOT = "/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include"
HDRS = ["vulkan/vulkan_core.h", "vulkan/vulkan_android.h"]
WORK = "/root/shimgen"

# ---- 需要手写在 shim.c 里（带创建/销毁钩子或需要返回自身 thunk）的函数 ----
SPECIAL = {
    "vkCreateInstance", "vkDestroyInstance",
    "vkCreateDevice", "vkDestroyDevice",
    "vkGetInstanceProcAddr", "vkGetDeviceProcAddr",
}


def slurp(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        t = f.read()
    t = t.replace("\\\n", " ")                       # 续行
    t = re.sub(r"/\*.*?\*/", " ", t, flags=re.S)      # 块注释
    t = re.sub(r"//[^\n]*", " ", t)                   # 行注释
    return t


def collect_protos():
    text = "\n".join(slurp(os.path.join(SYSROOT, h)) for h in HDRS)
    proto = re.compile(r"VKAPI_ATTR\s+(.+?)\s+VKAPI_CALL\s+(vk\w+)\s*\((.*?)\)\s*;", re.S)
    out = {}
    for m in proto.finditer(text):
        out.setdefault(m.group(2), (m.group(1).strip(), m.group(3).strip()))
    return out


def split_args(s):
    if not s.strip():
        return []
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    return [a.strip() for a in out if a.strip()]


def pname(decl):
    d = re.sub(r"\[[^\]]*\]\s*$", "", decl).strip()          # 去掉数组后缀
    m = re.search(r"([A-Za-z_]\w*)\s*$", d)
    return m.group(1) if m else None


def ptype(decl):
    n = pname(decl)
    t = re.sub(r"\[[^\]]*\]\s*$", "", decl).strip()
    if n:
        t = re.sub(r"\b" + re.escape(n) + r"\b\s*$", "", t).strip()
    return t


def base_type(t):
    return re.sub(r"\s+", " ", t.replace("const", " ").replace("*", " ")).strip()


def classify(args):
    """按首参句柄类型决定用哪条解析路径"""
    if not args:
        return "instance_create"                       # 无首参 -> icd_gipa(NULL, name)
    bt = base_type(ptype(args[0]))
    if bt == "VkInstance":
        return "instance"
    if bt == "VkPhysicalDevice":
        return "physdev"
    if bt == "VkDevice":
        return "device"
    if bt in ("VkQueue", "VkCommandBuffer"):
        return "device_global"
    return "instance_create"                           # VkInstanceCreateInfo*/uint32_t*/const char* ...


FAIL_RET = {
    "void": None,
    "VkResult": "VK_ERROR_INITIALIZATION_FAILED",
    "VkBool32": "VK_FALSE",
    "uint32_t": "0",
    "uint64_t": "0",
    "VkDeviceSize": "0",
    "PFN_vkVoidFunction": "NULL",
}


def main():
    names = [l.strip() for l in open(os.path.join(WORK, "vkund.txt")) if l.strip()]
    protos = collect_protos()

    missing = [n for n in names if n not in protos]
    if missing:
        print("!! 头文件里找不到这些原型: %s" % ", ".join(missing), file=sys.stderr)
        return 1

    generic = [n for n in names if n not in SPECIAL]
    rets = collections.Counter()
    classes = collections.Counter()

    lines = []
    lines.append("/* ===== 本文件由 gen_shim.py 自动生成，请勿手改 ===== */\n")

    for n in generic:
        ret, args = protos[n]
        a = split_args(args)
        cls = classify(a)
        rets[ret] += 1
        classes[cls] += 1
        a0 = pname(a[0]) if a else None

        if cls == "instance_create":
            resolver = 'shim_res_icd_null("%s")' % n
            keyexpr = "(void *)(uintptr_t)1"
        elif cls == "instance":
            resolver = 'shim_res_instance(%s, "%s")' % (a0, n)
            keyexpr = "(void *)(uintptr_t)%s" % a0
        elif cls == "physdev":
            resolver = 'shim_res_physdev("%s")' % n
            keyexpr = "(void *)(uintptr_t)g_inst"
        elif cls == "device":
            resolver = 'shim_res_device(%s, "%s")' % (a0, n)
            keyexpr = "(void *)(uintptr_t)%s" % a0
        else:  # device_global
            resolver = 'shim_res_device_global("%s")' % n
            keyexpr = "(void *)(uintptr_t)g_dev"

        callargs = ", ".join(pname(x) for x in a)
        pfn = "PFN_%s" % n

        lines.append("/* ---- %s  [%s] ---- */" % (n, cls))
        lines.append("typedef %s (VKAPI_PTR *%s)(%s);" % (ret, pfn, args or "void"))
        lines.append("SHIM_EXPORT VKAPI_ATTR %s VKAPI_CALL %s(%s)" % (ret, n, args or "void"))
        lines.append("{")
        lines.append("    static void *sc_f, *sc_k;")
        lines.append("    void *fp = (sc_f && sc_k == %s) ? sc_f : NULL;" % keyexpr)
        lines.append("    if (!fp) { fp = %s; if (fp) { sc_f = fp; sc_k = %s; } }" % (resolver, keyexpr))
        fr = FAIL_RET.get(ret, "__UNKNOWN__")
        if ret == "void":
            lines.append('    if (!fp) { LOGI("MISSING %s", "%s"); return; }' % ("%s", n))
            lines.append("    ((%s)fp)(%s);" % (pfn, callargs))
        elif fr == "__UNKNOWN__":
            lines.append('    if (!fp) { LOGI("MISSING %s", "%s"); return (%s)0; }' % ("%s", n, ret))
            lines.append("    return ((%s)fp)(%s);" % (pfn, callargs))
        else:
            lines.append('    if (!fp) { LOGI("MISSING %s", "%s"); return %s; }' % ("%s", n, fr))
            lines.append("    return ((%s)fp)(%s);" % (pfn, callargs))
        lines.append("}\n")

    # 名称 -> thunk 表（供 vkGetInstanceProcAddr/vkGetDeviceProcAddr 返回我们自己的转发器）
    lines.append("typedef struct { const char *name; void *fn; } shim_entry_t;\n")
    lines.append("static const shim_entry_t shim_table[] = {")
    for n in names:
        lines.append('    { "%s", (void *)(uintptr_t)&%s },' % (n, n))
    lines.append("    { NULL, NULL }")
    lines.append("};\n")

    with open(os.path.join(WORK, "shim_gen.inc"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    rep = []
    rep.append("总符号: %d   泛型转发: %d   手写特殊: %d" % (len(names), len(generic), len(names) - len(generic)))
    rep.append("返回类型分布: %s" % dict(rets))
    rep.append("解析路径分布: %s" % dict(classes))
    rep.append("特殊函数: %s" % ", ".join(sorted(SPECIAL)))
    unk = [r for r in rets if r not in FAIL_RET]
    rep.append("未知返回类型(用 (T)0 兜底): %s" % (unk or "无"))
    with open(os.path.join(WORK, "gen_report.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(rep) + "\n")
    print("\n".join(rep))
    return 0


if __name__ == "__main__":
    sys.exit(main())

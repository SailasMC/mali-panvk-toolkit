#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Mali kbase 免 root 能力探测 (Android / 无 root / 应用 uid)

用途：判断"能不能在普通 App 进程里直接驱动 Mali GPU"——
这是 FCL / ZL2 加载开源驱动 (Mesa PanVK / KRAID, kbase 后端) 的前置条件。

原理：
  Mali 的私有内核驱动 kbase 通过 /dev/mali0 暴露 ioctl。
  按 uAPI 规定，每个 fd 上的 **第一个** ioctl 必须是 VER_CHECK：
      CSF 世代 (arch >= 10, G6xx/G7xx/G9xx): _IOWR(0x80, 52, struct{u16 major; u16 minor;})
      JM  世代 (arch <= 9,  G3x..G7x)      : _IOWR(0x80,  0, 同上)
  传入自己支持的版本；内核把"它能接受的版本"写回。传入 minor 超过内核上限时，
  内核返回成功但把 minor 钳到自己的真实值 —— 这就是读出版本号的办法。
  握手成功前，其它所有 ioctl 一律返回 EPERM。

用法：  python3 kbase_probe.py
"""

import ctypes
import errno
import os
import re
import struct
import sys

DEV = "/dev/mali0"
IOCTL_TYPE = 0x80


def _ioc(direction, typ, nr, size):
    return (direction << 30) | (size << 16) | (typ << 8) | nr


VER_CHECK_CSF = _ioc(3, IOCTL_TYPE, 52, 4)   # _IOWR
VER_CHECK_JM = _ioc(3, IOCTL_TYPE, 0, 4)     # _IOWR
GET_GPUPROPS = _ioc(1, IOCTL_TYPE, 3, 16)    # _IOW

# KBASE_GPUPROP_* 常用键（数值来自 mali_kbase_ioctl.h）
PROP_NAMES = {
    1: "PRODUCT_ID", 2: "VERSION_STATUS", 3: "MINOR_REVISION", 4: "MAJOR_REVISION",
    6: "GPU_FREQ_KHZ_MAX", 12: "GPU_AVAILABLE_MEMORY_SIZE",
    13: "L2_LOG2_LINE_SIZE", 14: "L2_LOG2_CACHE_SIZE", 15: "L2_NUM_L2_SLICES",
    16: "TILER_BIN_SIZE_BYTES", 17: "TILER_MAX_ACTIVE_LEVELS",
    18: "MAX_THREADS", 19: "MAX_WORKGROUP_SIZE", 20: "MAX_BARRIER_SIZE",
    21: "MAX_REGISTERS", 22: "MAX_TASK_QUEUE",
    25: "RAW_SHADER_PRESENT", 26: "RAW_TILER_PRESENT", 34: "RAW_JS_PRESENT",
    33: "RAW_AS_PRESENT", 55: "RAW_GPU_ID", 59: "RAW_THREAD_FEATURES",
    60: "RAW_COHERENCY_MODE", 61: "COHERENCY_NUM_GROUPS", 63: "COHERENCY_COHERENCY",
}

_libc = ctypes.CDLL("libc.so", use_errno=True)


def _ioctl(fd, request, buf):
    ctypes.set_errno(0)
    ret = _libc.ioctl(fd, ctypes.c_ulong(request), buf)
    return ret, ctypes.get_errno()


def check_node(path):
    """能否打开内核节点（权限 + SELinux 一起验）。"""
    for label, flags in (("RDWR", os.O_RDWR), ("RDONLY", os.O_RDONLY)):
        try:
            fd = os.open(path, flags | os.O_CLOEXEC)
            return fd, label
        except OSError as exc:
            last = exc
    return None, "{} (errno={} {})".format(
        errno.errorcode.get(last.errno, last.errno), last.errno, last.strerror)


def handshake(nr, probe_major=1, probe_minor=99):
    """返回 (ok, 内核真实版本, 说明)。每个 fd 只做一次 VER_CHECK。"""
    fd = os.open(DEV, os.O_RDWR | os.O_CLOEXEC)
    try:
        buf = ctypes.create_string_buffer(4)
        buf[0:2] = probe_major.to_bytes(2, "little")
        buf[2:4] = probe_minor.to_bytes(2, "little")
        ret, err = _ioctl(fd, _ioc(3, IOCTL_TYPE, nr, 4), buf)
        major = int.from_bytes(buf[0:2], "little")
        minor = int.from_bytes(buf[2:4], "little")
        if ret == 0:
            return True, (major, minor), "内核接受并回传 {}.{}".format(major, minor)
        return False, None, "ret={} errno={}({})".format(
            ret, err, errno.errorcode.get(err, "-"))
    finally:
        os.close(fd)


def gpuprops():
    """握手后读 GPU 属性；要求"握手是该 fd 第一个 ioctl"。"""
    fd = os.open(DEV, os.O_RDWR | os.O_CLOEXEC)
    try:
        buf = ctypes.create_string_buffer(4)
        buf[0:2] = (1).to_bytes(2, "little")
        buf[2:4] = (0).to_bytes(2, "little")
        if _ioctl(fd, VER_CHECK_CSF, buf)[0] != 0:
            return None, "VER_CHECK 未通过"

        def call(ptr, size):
            req = ctypes.create_string_buffer(16)
            ctypes.memmove(req, ctypes.byref(ctypes.c_uint64(ptr)), 8)
            ctypes.memmove(ctypes.addressof(req) + 8,
                           ctypes.byref(ctypes.c_uint32(size)), 4)
            return _ioctl(fd, GET_GPUPROPS, req)

        need, err = call(0, 0)          # flags 必须为 0，返回值 = 所需字节数
        if need <= 0:
            return None, "GET_GPUPROPS 探测失败 errno={}({})".format(
                err, errno.errorcode.get(err, "-"))
        blob = ctypes.create_string_buffer(need + 64)
        got, err = call(ctypes.addressof(blob), need)
        if got < 0:
            return None, "GET_GPUPROPS 填充失败 errno={}({})".format(
                err, errno.errorcode.get(err, "-"))

        raw = blob.raw[:got]
        props, off = {}, 0
        while off + 4 <= len(raw):
            hdr = struct.unpack_from("<I", raw, off)[0]
            off += 4
            key, code = hdr >> 2, hdr & 3
            vsize = 1 << code
            if off + vsize > len(raw):
                break
            props[key] = int.from_bytes(raw[off:off + vsize], "little")
            off += vsize
        return props, "{} 字节 / {} 个属性".format(got, len(props))
    finally:
        os.close(fd)


def main():
    print("=" * 66)
    print(" Mali kbase 免 root 能力探测    uid={} ".format(os.getuid()))
    print("=" * 66)

    print("\n[1] 内核 GPU 节点可访问性")
    for path, note in (("/dev/mali0", "kbase（Mali 私有内核接口）"),
                       ("/dev/dri/card0", "DRM（panthor/Turnip 走这条）")):
        fd, how = check_node(path)
        if fd is not None:
            os.close(fd)
            print("  ✔ {:<16} 可打开 ({})   {}".format(path, how, note))
        else:
            print("  ✘ {:<16} {}".format(path, note))
            print("      {}".format(how))
    if not os.path.exists("/dev/dri/renderD128"):
        print("  ✘ /dev/dri/renderD128 不存在（这是 Adreno/Turnip 免 root 的通道，Mali 没有）")

    print("\n[2] kbase 版本握手")
    ok_csf, ver, msg = handshake(52)
    print("  CSF (nr=52): {}  {}".format("成功" if ok_csf else "失败", msg))
    if not ok_csf:
        ok_jm, vjm, msg2 = handshake(0)
        print("  JM  (nr=0) : {}  {}".format("成功" if ok_jm else "失败", msg2))
        if ok_jm:
            ver = vjm
    if not (ok_csf or ok_jm):
        print("\n  ⇒ 结论：应用进程无法与 kbase 握手，开源驱动（PanVK kbase 后端）这条路在当前 ROM 走不通。")
        return 1

    gen = "CSF（Valhall 5th gen / G6xx G7xx G9xx）" if ok_csf else "JM（Bifrost / Valhall 1-4 gen）"
    print("\n  内核 kbase uAPI 版本 = {}.{}   世代 = {}".format(ver[0], ver[1], gen))
    print("  ⇒ 握手号为 {}，任何用户态驱动都必须以此为首个 ioctl。".format(
        "CSF nr=52" if ok_csf else "JM nr=0"))

    print("\n[3] 读取 GPU 属性（握手成功 = 权限门槛已通过）")
    props, msg = gpuprops()
    if props is None:
        print("  失败：{}".format(msg))
    else:
        print("  {}".format(msg))
        for key in sorted(props):
            name = PROP_NAMES.get(key)
            if not name:
                continue
            val = props[key]
            extra = ""
            if name == "L2_LOG2_CACHE_SIZE":
                extra = "  → L2 = {} KB".format((1 << val) // 1024)
            elif name == "GPU_FREQ_KHZ_MAX":
                extra = "  → {:.3f} GHz".format(val / 1e6)
            elif name == "GPU_AVAILABLE_MEMORY_SIZE":
                extra = "  → {:.0f} MB".format(val / 1024 / 1024)
            elif name == "PRODUCT_ID":
                extra = "  → 0x{:x}".format(val)
            print("    {:<32} = {}{}".format(name, val, extra))
        if props.get(34) == 0:
            print("\n    RAW_JS_PRESENT = 0  →  确认是 CSF 世代（无 Job Slot，走队列）")
        if props.get(60) == 0:
            print("    RAW_COHERENCY_MODE = 0  →  非一致内存，驱动必须自己做 cache 维护")

    print("\n" + "=" * 66)
    print(" 结论：应用进程可在免 root 下直达 Mali 内核驱动，")
    print("       Mesa PanVK / KRAID 的 kbase 后端在原理上可用。")
    print("       仍需按本机 uAPI {}.{} + 厂商 gralloc/AFBC 行为做适配。".format(ver[0], ver[1]))
    print("=" * 66)
    return 0


if __name__ == "__main__":
    sys.exit(main())

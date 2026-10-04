/* =====================================================================
 * kbase_selftest.c
 *
 * Mali kbase 单文件自检程序（无 root / 普通 App 进程 / arm64）
 *
 * 目的：验证"普通 App 进程能否在免 root 下直接驱动 Mali GPU（kbase）"。
 *       这是把 Mesa PanVK / KRAID（kbase 后端）移植到本机的**最低前置条件**：
 *       能走完 打开节点 → uAPI 握手 → 读 GPU 属性 → 建立内核上下文 →
 *       申请 GPU 内存 → CPU 映射并读写校验 → 释放  这一整条链路。
 *
 * 目标设备：OPPO PHZ110 / MT6989（天玑9300）/ Immortalis-G720 MC12
 *           / 无 root / 内核 6.1.157 / kbase CSF uAPI = 1.21
 *
 * ── 事实与出处（本文件里每个魔数都标了来源）──────────────────────────
 *   [mk.h]  .tmp/mk.h   —— Mali kbase uAPI 头文件（ioctl 号 / 结构体 / 属性键）
 *   [kb.c]  .tmp/kb.c   —— Mesa（KRAID fork）kbase_kmod.c，真实 bring-up 顺序
 *   [probe] kbase_probe.py —— 本机已实测通过的 Python 版探测（顺序与编码旁证）
 *
 *   本文件**不 include** .tmp/mk.h：所有需要的宏与结构体都在下面自包含地
 *   重新定义（编译时只带这一个文件）。除 libc / linux(POSIX) 头文件外无依赖。
 *
 * ── 编译 ─────────────────────────────────────────────────────────────
 *   x86_64 语法自检：  gcc -fsyntax-only -std=c99 kbase_selftest.c
 *   Android arm64 交叉编译（NDK / 本机 clang 均可）：
 *     $CC --target=aarch64-linux-android24 -std=c99 -O2 -Wall \
 *         -o kbase_selftest kbase_selftest.c
 *   注意：文件里有若干 #warning 用于标注"待确认"项。若用 -Werror 编译，
 *         请加 -Wno-error=cpp（或先读一遍这些 #warning 并在报告里追认），
 *         也可 -DKBASE_SELFTEST_NO_WARNINGS 关掉它们。
 *   编译期断言（static assert）会校验每个 ioctl 常量；其中
 *   VER_CHECK_CSF=0xC0048034 与 GET_GPUPROPS=0x40108003 是本机 Python 实测值，
 *   MEM_ALLOC_EX=0xC040803B 对应 union 大小 64 字节（见 mk.h:189）。
 *
 * ── 已知的、故意的顺序偏差（务必先看）───────────────────────────────
 *   1) kb.c 把 SET_FLAGS 放在 GET_GPUPROPS **之前**（kb.c:1148 vs kb.c:1166）；
 *      本程序按任务书要求（步骤 3 属性、步骤 4 SET_FLAGS）先读属性。
 *      kbase_probe.py:99-117 已在本机实测：VER_CHECK 之后直接 GET_GPUPROPS
 *      可用，不需要先 SET_FLAGS。两条顺序都能过，此处遵循任务书。
 *   2) SAME_VA 内存的释放：kb.c:1658-1673 明确 —— SAME_VA 区域由 munmap()
 *      触发 free-on-close 整块销毁，**之后不得再调 MEM_FREE**（会命中已失效
 *      VA）；只有非 SAME_VA（zone 区域，如 EXEC_VA）才用 MEM_FREE。本程序照抄，
 *      并额外用一个"未 mmap 的 SAME_VA cookie"补测 MEM_FREE 本身可用性。
 *   3) kb.c:1156 把 tracking page 的 mmap 视为致命步骤；本程序把它降级为
 *      WARN（失败也继续），以便取得 MEM_ALLOC 的真实结果，便于诊断。
 * ===================================================================== */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* =====================================================================
 * 1. ioctl 编码宏（自包含版本）
 *
 * 来源：asm-generic/ioctl.h（Linux uAPI）的位域约定：
 *   NRBITS=8 TYPEBITS=8 SIZEBITS=14 DIRBITS=2
 *   NRSHIFT=0 TYPESHIFT=8 SIZESHIFT=16 DIRSHIFT=30
 *   IOC_NONE=0 IOC_WRITE=1 IOC_READ=2；_IOWR = WRITE|READ = 3
 * 这里不 include <linux/ioctl.h>，避免与 <sys/ioctl.h>（glibc 也定义 _IOWR）
 * 重定义冲突，同时保证在 NDK/bionic 与 glibc 上行为完全一致。
 * ===================================================================== */
#define KBASE_IOC_NRBITS    8
#define KBASE_IOC_TYPEBITS  8
#define KBASE_IOC_SIZEBITS  14
#define KBASE_IOC_DIRBITS   2

#define KBASE_IOC_NRSHIFT    0
#define KBASE_IOC_TYPESHIFT  8
#define KBASE_IOC_SIZESHIFT  16
#define KBASE_IOC_DIRSHIFT   30

#define KBASE_IOC_NONE  0u
#define KBASE_IOC_WRITE 1u
#define KBASE_IOC_READ  2u

/* 结果裁剪到 32 位宽：方向位 3 会让 32 位常量变成"负数"，若直接隐式提升成
 * 64 位的 unsigned long 会被符号扩展成 0xFFFFFFFFC0xxxxxx。kernel 的
 * sys_ioctl 把 cmd 截断为 unsigned int 所以实际不会错，但显式裁剪更干净。 */
#define KBASE_IOC(dir, type, nr, size) \
   ((unsigned long)(( ((unsigned long)(dir)  << KBASE_IOC_DIRSHIFT)  | \
                      ((unsigned long)(type) << KBASE_IOC_TYPESHIFT) | \
                      ((unsigned long)(nr)   << KBASE_IOC_NRSHIFT)   | \
                      ((unsigned long)(size) << KBASE_IOC_SIZESHIFT)) \
                    & 0xfffffffful))

/* ioctl type：mk.h:28  KBASE_IOCTL_TYPE 0x80 */
#define KBASE_IOCTL_TYPE 0x80

/* =====================================================================
 * 2. uAPI 结构体（逐字对照 mk.h；用 stdint 的定宽类型，布局与 __u8/__u16/
 *    __u32/__u64 完全一致）
 * ===================================================================== */

/* mk.h:35-38  struct kbase_ioctl_version_check { __u16 major; __u16 minor; } */
struct kbase_ioctl_version_check {
   uint16_t major;
   uint16_t minor;
};

/* mk.h:49-51  struct kbase_ioctl_set_flags { __u32 create_flags; } */
struct kbase_ioctl_set_flags {
   uint32_t create_flags;
};

/* mk.h:75-79  struct kbase_ioctl_get_gpuprops { __u64 buffer; __u32 size;
 *                                              __u32 flags; }            */
struct kbase_ioctl_get_gpuprops {
   uint64_t buffer;   /* 用户态缓冲区指针（内核 copy_to_user 到这里） */
   uint32_t size;     /* 缓冲区字节数；探测时给 0 */
   uint32_t flags;    /* kb.c:209 / probe.py:109 —— 必须为 0 */
};

/* mk.h:157-168  union kbase_ioctl_mem_alloc（旧接口，CSF uAPI < 1.9 / JM 用） */
union kbase_ioctl_mem_alloc {
   struct {
      uint64_t va_pages;
      uint64_t commit_pages;
      uint64_t extension;
      uint64_t flags;
   } in;
   struct {
      uint64_t flags;
      uint64_t gpu_va;   /* SAME_VA 时是 mmap cookie，否则是真 GPU VA */
   } out;
};

/* mk.h:175-188  union kbase_ioctl_mem_alloc_ex（CSF uAPI >= 1.9 用，本机走这条）
 * 前 4 个输入字段与输出覆盖层故意与旧接口一致（mk.h:172-174）。 */
union kbase_ioctl_mem_alloc_ex {
   struct {
      uint64_t va_pages;
      uint64_t commit_pages;
      uint64_t extension;
      uint64_t flags;
      uint64_t fixed_address;   /* 未请求 BASE_MEM_FIXED 时必须为 0 */
      uint64_t extra[3];        /* 保留尾巴 */
   } in;
   struct {
      uint64_t flags;
      uint64_t gpu_va;
   } out;
};

/* 待确认项：用 #warning 显式标注（见文件末尾/报告）。若用 -Werror 编译请加
 * -Wno-error=cpp，或定义 KBASE_SELFTEST_NO_WARNINGS 关掉这些提示。 */
#ifndef KBASE_SELFTEST_NO_WARNINGS
#warning "待确认: kbase_ioctl_mem_alloc_ex.in.extra[3] 的语义 (mk.h 未说明; 是否必须为 0 未在本机实测, 本程序填 0)"
#endif

/* mk.h:245-247  struct kbase_ioctl_mem_free { __u64 gpu_addr; } */
struct kbase_ioctl_mem_free {
   uint64_t gpu_addr;
};

/* ── 可选附加检查用（CSF 队列能力 / CSF USER 寄存器页）── */
/* mk.h:682-693 */
struct basep_cs_stream_control {
   uint32_t features;
   uint32_t padding;
};
struct basep_cs_group_control {
   uint32_t features;
   uint32_t stream_num;
   uint32_t suspend_size;
   uint32_t padding;
};
/* mk.h:695-710  union kbase_ioctl_cs_get_glb_iface */
union kbase_ioctl_cs_get_glb_iface {
   struct {
      uint32_t max_group_num;
      uint32_t max_total_stream_num;
      uint64_t groups_ptr;
      uint64_t streams_ptr;
   } in;
   struct {
      uint32_t glb_version;
      uint32_t features;
      uint32_t group_num;
      uint32_t prfcnt_size;
      uint32_t total_stream_num;
      uint32_t instr_features;
   } out;
};

/* =====================================================================
 * 3. ioctl 编号（全部来自 mk.h；方向位是关键，见各自的注释）
 * ===================================================================== */

/* mk.h:40-44 【本机实测通过】CSF 世代 VERSION_CHECK：nr=52 = 0x34，
 *   _IOWR(0x80,52,4) → 方向位 3（不是 1！），常量 0xC0048034。
 *   mk.h:16-20 警告：nr=0 在 CSF 上被保留并返回 -EPERM；且它必须是该 fd 上
 *   的第一个 ioctl，否则其它 ioctl 一律 EPERM。 */
#define KBASE_IOCTL_VERSION_CHECK_CSF \
   KBASE_IOC(KBASE_IOC_WRITE | KBASE_IOC_READ, KBASE_IOCTL_TYPE, 52, \
             sizeof(struct kbase_ioctl_version_check))

/* mk.h:39-41  JM 世代（Midgard/Bifrost/Valhall arch <= 9）VERSION_CHECK：nr=0,
 *   _IOWR(0x80,0,4) → 0xC0048000。 */
#define KBASE_IOCTL_VERSION_CHECK_JM \
   KBASE_IOC(KBASE_IOC_WRITE | KBASE_IOC_READ, KBASE_IOCTL_TYPE, 0, \
             sizeof(struct kbase_ioctl_version_check))

/* mk.h:56-57  SET_FLAGS：nr=1，_IOW(0x80,1,4) → 方向位 1，常量 0x40048001。
 *   kb.c:1148 用 .create_flags = 0 调用（mk.h:52 BASE_CONTEXT_CREATE_FLAG_NONE）。*/
#define KBASE_IOCTL_SET_FLAGS \
   KBASE_IOC(KBASE_IOC_WRITE, KBASE_IOCTL_TYPE, 1, \
             sizeof(struct kbase_ioctl_set_flags))

/* mk.h:80-81  GET_GPUPROPS：nr=3，_IOW(0x80,3,16) → 方向位 1，常量 0x40108003。
 *   probe.py:38 实测用的就是这个编码。 */
#define KBASE_IOCTL_GET_GPUPROPS \
   KBASE_IOC(KBASE_IOC_WRITE, KBASE_IOCTL_TYPE, 3, \
             sizeof(struct kbase_ioctl_get_gpuprops))

/* mk.h:169-170  MEM_ALLOC（旧）：nr=5，_IOWR(0x80,5,32) → 0xC0208005 */
#define KBASE_IOCTL_MEM_ALLOC \
   KBASE_IOC(KBASE_IOC_WRITE | KBASE_IOC_READ, KBASE_IOCTL_TYPE, 5, \
             sizeof(union kbase_ioctl_mem_alloc))

/* mk.h:189-190  MEM_ALLOC_EX：nr=59。注意 union 大小 = 64 字节
 *   （in = 5*8 + extra[3] = 40+24 = 64），所以 _IOWR(0x80,59,64) → 0xC040803B。
 *   CSF uAPI >= 1.9 用它（kb.c:1570-1582 的选择条件）。 */
#define KBASE_IOCTL_MEM_ALLOC_EX \
   KBASE_IOC(KBASE_IOC_WRITE | KBASE_IOC_READ, KBASE_IOCTL_TYPE, 59, \
             sizeof(union kbase_ioctl_mem_alloc_ex))

/* mk.h:248-249  MEM_FREE：nr=7，_IOW(0x80,7,8) → 0x40088007 */
#define KBASE_IOCTL_MEM_FREE \
   KBASE_IOC(KBASE_IOC_WRITE, KBASE_IOCTL_TYPE, 7, \
             sizeof(struct kbase_ioctl_mem_free))

/* mk.h:711-712  CS_GET_GLB_IFACE（CSF only）：nr=51，_IOWR(0x80,51,24) → 0xC0188033 */
#define KBASE_IOCTL_CS_GET_GLB_IFACE \
   KBASE_IOC(KBASE_IOC_WRITE | KBASE_IOC_READ, KBASE_IOCTL_TYPE, 51, \
             sizeof(union kbase_ioctl_cs_get_glb_iface))

/* =====================================================================
 * 4. 编译期断言：把"我算出来的编码"钉死在两个**本机实测确认过**的常量上。
 *    CSF VER_CHECK = 0xC0048034（probe.py:36  _ioc(3,0x80,52,4)）
 *    GET_GPUPROPS  = 0x40108003（probe.py:38  _ioc(1,0x80, 3,16)）
 *    其余常量与 mk.h 宏展开逐一对照（见上面注释里的数值）。
 * ===================================================================== */
#define KBASE_STATIC_ASSERT(cond, tag) \
   typedef char kbase_static_assert_##tag[(cond) ? 1 : -1]

KBASE_STATIC_ASSERT(sizeof(struct kbase_ioctl_version_check) == 4,  ver_check_is_4_bytes);
KBASE_STATIC_ASSERT(sizeof(struct kbase_ioctl_set_flags)     == 4,  set_flags_is_4_bytes);
KBASE_STATIC_ASSERT(sizeof(struct kbase_ioctl_get_gpuprops)  == 16, gpuprops_is_16_bytes);
KBASE_STATIC_ASSERT(sizeof(union  kbase_ioctl_mem_alloc)     == 32, mem_alloc_is_32_bytes);
KBASE_STATIC_ASSERT(sizeof(union  kbase_ioctl_mem_alloc_ex)  == 64, mem_alloc_ex_is_64_bytes);
KBASE_STATIC_ASSERT(sizeof(struct kbase_ioctl_mem_free)      == 8,  mem_free_is_8_bytes);

KBASE_STATIC_ASSERT(KBASE_IOCTL_VERSION_CHECK_CSF == 0xC0048034ul, ioc_ver_csf);
KBASE_STATIC_ASSERT(KBASE_IOCTL_VERSION_CHECK_JM  == 0xC0048000ul, ioc_ver_jm);
KBASE_STATIC_ASSERT(KBASE_IOCTL_SET_FLAGS         == 0x40048001ul, ioc_set_flags);
KBASE_STATIC_ASSERT(KBASE_IOCTL_GET_GPUPROPS      == 0x40108003ul, ioc_gpuprops);
KBASE_STATIC_ASSERT(KBASE_IOCTL_MEM_ALLOC         == 0xC0208005ul, ioc_mem_alloc);
KBASE_STATIC_ASSERT(KBASE_IOCTL_MEM_ALLOC_EX      == 0xC040803Bul, ioc_mem_alloc_ex);
KBASE_STATIC_ASSERT(KBASE_IOCTL_MEM_FREE          == 0x40088007ul, ioc_mem_free);
KBASE_STATIC_ASSERT(KBASE_IOCTL_CS_GET_GLB_IFACE  == 0xC0188033ul, ioc_cs_glb_iface);

/* =====================================================================
 * 5. GPU 属性键（mk.h:88-144 KBASE_GPUPROP_*）
 * ===================================================================== */
#define KBASE_GPUPROP_PRODUCT_ID           1   /* mk.h:88  */
#define KBASE_GPUPROP_VERSION_STATUS       2   /* mk.h:89  */
#define KBASE_GPUPROP_MINOR_REVISION       3   /* mk.h:90  */
#define KBASE_GPUPROP_MAJOR_REVISION       4   /* mk.h:91  */
#define KBASE_GPUPROP_GPU_FREQ_KHZ_MAX     6   /* mk.h:93  */
#define KBASE_GPUPROP_GPU_AVAILABLE_MEMORY_SIZE 12 /* mk.h:99 */
#define KBASE_GPUPROP_L2_LOG2_LINE_SIZE    13  /* mk.h:100 */
#define KBASE_GPUPROP_L2_LOG2_CACHE_SIZE   14  /* mk.h:101 */
#define KBASE_GPUPROP_L2_NUM_L2_SLICES     15  /* mk.h:102 */
#define KBASE_GPUPROP_MAX_THREADS          18  /* mk.h:105 */
#define KBASE_GPUPROP_RAW_JS_PRESENT       34  /* mk.h:121 */
#define KBASE_GPUPROP_RAW_GPU_ID           55  /* mk.h:128 */
#define KBASE_GPUPROP_RAW_COHERENCY_MODE   60  /* mk.h:133 */
#define KBASE_GPUPROP_COHERENCY_NUM_GROUPS 61  /* mk.h:134 */

/* =====================================================================
 * 6. 内存分配 flag（mk.h:193-212 base_mem_alloc_flags）
 *    kb.c:1373-1411 to_kbase_mem_flags() 在"无任何 kmod flag"时的取值 =
 *       PROT_GPU_RD | PROT_GPU_WR | PROT_CPU_RD | PROT_CPU_WR
 *       | COHERENT_LOCAL | SAME_VA
 * ===================================================================== */
#define KBASE_BASE_MEM_PROT_CPU_RD    ((uint64_t)1 << 0)   /* mk.h:193 */
#define KBASE_BASE_MEM_PROT_CPU_WR    ((uint64_t)1 << 1)   /* mk.h:194 */
#define KBASE_BASE_MEM_PROT_GPU_RD    ((uint64_t)1 << 2)   /* mk.h:195 */
#define KBASE_BASE_MEM_PROT_GPU_WR    ((uint64_t)1 << 3)   /* mk.h:196 */
#define KBASE_BASE_MEM_COHERENT_LOCAL ((uint64_t)1 << 11)  /* mk.h:201 */
#define KBASE_BASE_MEM_SAME_VA        ((uint64_t)1 << 13)  /* mk.h:203 */

#define KBASE_SELFTEST_ALLOC_FLAGS \
   (KBASE_BASE_MEM_PROT_GPU_RD | KBASE_BASE_MEM_PROT_GPU_WR | \
    KBASE_BASE_MEM_PROT_CPU_RD | KBASE_BASE_MEM_PROT_CPU_WR | \
    KBASE_BASE_MEM_COHERENT_LOCAL | KBASE_BASE_MEM_SAME_VA)

/* 特殊 mmap offset/"memory handle"（单位字节，4K 页基准；mk.h:214-219） */
#define KBASE_BASE_MEM_MAP_TRACKING_HANDLE       (3ull << 12)  /* mk.h:215, kb.c:1156 */
#define KBASE_BASEP_MEM_CSF_USER_REG_PAGE_HANDLE (47ull << 12) /* mk.h:216, kb.c:1253 */

/* 待确认（mmap 的 offset/prot 语义）：mk.h:149-155 与 kb.c:1613-1621 说明
 *   · 64 位客户端上非可执行分配被内核强制 SAME_VA；
 *   · SAME_VA 时 out.gpu_va 不是 GPU 地址而是"mmap cookie"，用
 *     mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, cookie) 建立映射，
 *     返回的 CPU 地址同时就是 GPU VA；
 *   · 非 SAME_VA（EXEC_VA 区域）时 out.gpu_va 是真 GPU VA，同时充当 mmap offset。
 * 本程序两种都按上面处理（运行时按 out.flags 的 SAME_VA 位分支打印），但
 * "cookie 的具体取值范围/是否必须 4K 对齐/内核是否接受任意 MAP_* 组合" 未实测。 */
#ifndef KBASE_SELFTEST_NO_WARNINGS
#warning "待确认: SAME_VA 下 mmap 的 offset=cookie 语义 (kb.c:1613-1621); prot 固定用 PROT_READ|PROT_WRITE + MAP_SHARED, 未验证其它组合"
#warning "待确认: MEM_FREE 对未 mmap 的 SAME_VA cookie 是否可用 (kb.c:1628 注释称可用, 本程序把它作为附加检查 5h, 失败仅 WARN)"
#endif

/* kbase 的 VA 页固定 4KiB（kb.c:1546 硬编码 4096） */
#define KBASE_PAGE_SIZE 4096ull

/* =====================================================================
 * 7. 输出小工具：每步 PASS/FAIL/WARN + errno 文本
 * ===================================================================== */
static unsigned g_step;   /* 已开始的主步骤数 */
static unsigned g_fail;   /* 必需步骤失败数（决定退出码） */
static unsigned g_warn;   /* 警告数（不影响退出码） */

/* 让编译器校验这几个包装函数的格式串（GNU/clang 扩展；非 GNU 编译器下为空） */
#if defined(__GNUC__) || defined(__clang__)
#  define KBASE_PRINTF_LIKE(fmt_idx, first_arg) \
      __attribute__((format(printf, fmt_idx, first_arg)))
#else
#  define KBASE_PRINTF_LIKE(fmt_idx, first_arg)
#endif

static void step_title(const char *fmt, ...) KBASE_PRINTF_LIKE(1, 2);
static void print_pass(const char *fmt, ...) KBASE_PRINTF_LIKE(1, 2);
static void print_warn(const char *fmt, ...) KBASE_PRINTF_LIKE(1, 2);
static int  print_fail(const char *fmt, ...) KBASE_PRINTF_LIKE(1, 2);
static void print_info(const char *fmt, ...) KBASE_PRINTF_LIKE(1, 2);

static void step_title(const char *fmt, ...)
{
   va_list ap;
   g_step++;
   printf("\n[%u] ", g_step);
   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
   printf("\n");
   fflush(stdout);
}

static void print_pass(const char *fmt, ...)
{
   va_list ap;
   printf("     PASS  ");
   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
   printf("\n");
   fflush(stdout);
}

/* 可选的附加检查：只记 WARN，绝不影响退出码 */
static void print_warn(const char *fmt, ...)
{
   va_list ap;
   g_warn++;
   printf("     WARN  ");
   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
   printf("\n");
   fflush(stdout);
}

/* 必需步骤失败：记 FAIL 并返回 -1，调用方负责 goto conclusion */
static int print_fail(const char *fmt, ...)
{
   va_list ap;
   g_fail++;
   printf("     FAIL  ");
   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
   printf("\n");
   fflush(stdout);
   return -1;
}

static void print_info(const char *fmt, ...)
{
   va_list ap;
   printf("           ");
   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
   printf("\n");
   fflush(stdout);
}

/* =====================================================================
 * 8. GET_GPUPROPS blob 解析
 *    编码（mk.h:83-87 / kb.c:164-172）：
 *      4 字节头 = (key << 2) | size_code   →  值长度 = 1 << size_code
 *      size_code 0=u8 1=u16 2=u32 3=u64（小端）
 * ===================================================================== */
struct kbase_prop_ent {
   uint32_t key;
   uint32_t size_code;
   uint64_t value;
};

#define KBASE_PROP_MAX_ENT 512

/* 解析整个 blob，返回条目数；*consumed 回传实际消费字节数 */
static uint32_t gpuprops_parse(const uint8_t *buf, size_t len,
                               struct kbase_prop_ent *tbl, uint32_t max_ent,
                               size_t *consumed)
{
   size_t off = 0;
   uint32_t n = 0;

   while (off + 4 <= len) {
      uint32_t hdr;
      uint32_t key, size_code, val_size;
      uint64_t val = 0;

      memcpy(&hdr, buf + off, 4);   /* 小端头；x86_64/arm64 都是小端 */
      off += 4;

      key       = hdr >> 2;
      size_code = hdr & 0x3u;
      val_size  = 1u << size_code;

      if (off + val_size > len)   /* 头声明了越界值：blob 截断，停止 */
         break;

      memcpy(&val, buf + off, val_size);  /* 小端：直接拷低位即可 */
      off += val_size;

      if (n < max_ent) {
         tbl[n].key       = key;
         tbl[n].size_code = size_code;
         tbl[n].value     = val;
      }
      n++;
   }

   *consumed = off;
   return (n < max_ent) ? n : max_ent;
}

static bool gpuprops_find(const struct kbase_prop_ent *tbl, uint32_t n,
                          uint32_t key, uint64_t *out)
{
   uint32_t i;
   for (i = 0; i < n; i++) {
      if (tbl[i].key == key) {
         if (out)
            *out = tbl[i].value;
         return true;
      }
   }
   return false;
}

/* 打印一条属性：存在则打印值 + 派生说明，缺失则明确标注 */
static void print_prop(const struct kbase_prop_ent *tbl, uint32_t n,
                       uint32_t key, const char *name, const char *derive)
{
   uint64_t v;
   if (gpuprops_find(tbl, n, key, &v))
      printf("           key=%-3u %-28s = %-12" PRIu64 " (0x%" PRIx64 ")%s\n",
             key, name, v, v, derive ? derive : "");
   else
      printf("           key=%-3u %-28s = <内核未上报>\n", key, name);
}

/* 确定性的读写测试模式（每字节都依赖下标，可检测漏写/错位） */
static uint8_t pattern_byte(size_t i)
{
   return (uint8_t)(((i * 131u) + 17u) ^ (i >> 8));
}

/* =====================================================================
 * main
 * ===================================================================== */
int main(void)
{
   int rc = 1;                      /* 默认失败；全通过才置 0 */

   const char *dev_node = "/dev/mali0";   /* 任务书给定；仅 kbase 这一条路 */
   int fd = -1;
   void *tracking = MAP_FAILED;

   int is_csf = 0;
   uint16_t uapi_major = 0, uapi_minor = 0;

   uint8_t *props_buf = NULL;
   int props_len = 0;
   struct kbase_prop_ent props[KBASE_PROP_MAX_ENT];
   uint32_t props_n = 0;
   size_t props_consumed = 0;

   uint64_t alloc_flags = 0, alloc_cookie = 0;
   int used_alloc_ex = 0;
   void *bo = MAP_FAILED;
   uint64_t bo_cookie_va = 0;
   uint64_t bo_size = 0;

   /* 能力记录（最终结论表用） */
   int cap_open = 0, cap_handshake = 0, cap_props = 0, cap_flags = 0;
   int cap_alloc = 0, cap_map = 0, cap_rw = 0, cap_free = 0;

   printf("======================================================================\n");
   printf(" Mali kbase 单文件自检  (uid=%u, pid=%d)\n", (unsigned)getuid(), (int)getpid());
   printf(" 目标：验证普通 App 进程能否在免 root 下驱动 Mali GPU（kbase CSF）\n");
   printf("======================================================================\n");
   printf("ioctl 编码自检（编译期已断言，这里把实际值打出来备查）:\n");
   printf("  VER_CHECK_CSF = 0x%08lX   (mk.h:43, _IOWR(0x80,52,4) 方向位=3)\n",
          KBASE_IOCTL_VERSION_CHECK_CSF);
   printf("  VER_CHECK_JM  = 0x%08lX   (mk.h:40, _IOWR(0x80, 0,4))\n",
          KBASE_IOCTL_VERSION_CHECK_JM);
   printf("  SET_FLAGS     = 0x%08lX   (mk.h:56)\n", KBASE_IOCTL_SET_FLAGS);
   printf("  GET_GPUPROPS  = 0x%08lX   (mk.h:80)\n", KBASE_IOCTL_GET_GPUPROPS);
   printf("  MEM_ALLOC_EX  = 0x%08lX   (mk.h:189)\n", KBASE_IOCTL_MEM_ALLOC_EX);
   printf("  MEM_ALLOC     = 0x%08lX   (mk.h:169)\n", KBASE_IOCTL_MEM_ALLOC);
   printf("  MEM_FREE      = 0x%08lX   (mk.h:248)\n", KBASE_IOCTL_MEM_FREE);

   /* ================================================================
    * 步骤 1 —— open("/dev/mali0", O_RDWR)
    *   /dev/dri/ 下的节点在本机是 EACCES，所以 kbase 是唯一通道。
    * ================================================================ */
   step_title("open(\"%s\", O_RDWR)  —— 打开 kbase 节点", dev_node);
   errno = 0;
   fd = open(dev_node, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      print_fail("open 失败: errno=%d (%s)", errno, strerror(errno));
      print_info("提示: 节点权限应为 0666；EACCES/EPERM 在无 root 下也可能是 SELinux 拒绝。");
      goto conclusion;
   }
   print_pass("open 成功，fd=%d", fd);
   cap_open = 1;

   /* ================================================================
    * 步骤 2 —— 版本握手（必须是该 fd 上的第一个 ioctl）
    *   CSF: _IOWR(0x80,52,4)，传 {1,21}；失败再试 JM: _IOWR(0x80,0,4)
    *   传 {1,21} 的语义（probe.py:15 + mk.h:32-38）：内核把 minor 钳到它
    *   实现的上限再写回 —— 所以回传值 = min(21, 内核真实 minor)。
    *     · 内核更老 → 回传真实 minor，可据此判断是否 >= 1.9（决定用不用 ALLOC_EX）
    *     · 内核更新 → 回传 21，仍然满足 >= 1.9
    *   （要读内核**真实** minor，得用另一个 fd 传个很大的 minor，如 probe.py 的 99；
    *     本机已确认真实值为 1.21。这里不再多开一个 fd。）
    * ================================================================ */
   step_title("版本握手 VERSION_CHECK —— CSF 先试，失败再试 JM");
   {
      struct kbase_ioctl_version_check ver;
      int err_csf = 0;

      memset(&ver, 0, sizeof(ver));
      ver.major = 1;      /* 任务书给定：CSF 走 1.x */
      ver.minor = 21;     /* 本机内核 CSF uAPI = 1.21 */

      errno = 0;
      if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK_CSF, &ver) == 0) {
         is_csf = 1;
         uapi_major = ver.major;
         uapi_minor = ver.minor;
         print_pass("CSF 握手成功 (nr=52)；内核回传 uAPI = %u.%u",
                    (unsigned)uapi_major, (unsigned)uapi_minor);
      } else {
         err_csf = errno;
         print_info("CSF 握手失败: errno=%d (%s)；mk.h:17-19 说明 CSF 世代下 nr=0 才会 EPERM，"
                    "此处 nr=52 失败说明可能不是 CSF 或节点受限，继续试 JM",
                    err_csf, strerror(err_csf));

         memset(&ver, 0, sizeof(ver));
         ver.major = 0;   /* kb.c:1122 传 0.0，让内核回传它实现的版本 */
         ver.minor = 0;
         errno = 0;
         if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK_JM, &ver) != 0) {
            int err_jm = errno;
            print_fail("JM 握手也失败: errno=%d (%s)", err_jm, strerror(err_jm));
            print_info("两个世代都握不上手 ⇒ 该进程无法使用 kbase（权限或 SELinux 层面被拦）。");
            goto conclusion;
         }
         is_csf = 0;
         uapi_major = ver.major;
         uapi_minor = ver.minor;
         print_pass("JM 握手成功 (nr=0)；内核回传 uAPI = %u.%u",
                    (unsigned)uapi_major, (unsigned)uapi_minor);
         if (uapi_major < 11)
            print_warn("kb.c:1130 要求 JM >= 11.0；本机报 %u.%u，kb.c 会直接拒绝这种老 JM",
                       (unsigned)uapi_major, (unsigned)uapi_minor);
      }

      print_info("世代判定 = %s", is_csf ? "CSF（Valhall 5th gen / arch >= 10）"
                                        : "JM（Bifrost / Valhall arch <= 9）");
      print_info("握手已建立 ⇒ 该 fd 上其它 ioctl 不再返回 EPERM（mk.h:19-20）");
   }
   cap_handshake = 1;

   /* ================================================================
    * 步骤 3 —— GET_GPUPROPS 两段式
    *   第一段：buffer=0,size=0,flags=0（kb.c:209；flags 必须为 0，probe.py:109），
    *           ioctl 返回值 = 所需字节数。
    *   第二段：按该长度填 buffer/size 再调一次（kb.c:227-230）。
    * ================================================================ */
   step_title("GET_GPUPROPS 两段式探测 + 属性 blob 解析");
   {
      struct kbase_ioctl_get_gpuprops req;
      int need;

      memset(&req, 0, sizeof(req));
      errno = 0;
      need = ioctl(fd, KBASE_IOCTL_GET_GPUPROPS, &req);
      if (need <= 0) {
         print_fail("第一段(探测长度)失败: ret=%d errno=%d (%s)", need, errno, strerror(errno));
         goto conclusion;
      }
      props_len = need;
      print_pass("第一段: 内核要求 %d 字节的属性 blob", props_len);

      props_buf = (uint8_t *)malloc((size_t)props_len);
      if (!props_buf) {
         print_fail("malloc(%d) 失败", props_len);
         goto conclusion;
      }

      memset(&req, 0, sizeof(req));
      req.buffer = (uint64_t)(uintptr_t)props_buf;
      req.size   = (uint32_t)props_len;
      req.flags  = 0;

      errno = 0;
      need = ioctl(fd, KBASE_IOCTL_GET_GPUPROPS, &req);
      if (need < 0) {
         print_fail("第二段(填充)失败: ret=%d errno=%d (%s)", need, errno, strerror(errno));
         goto conclusion;
      }
      print_pass("第二段: 填充成功，返回 %d 字节", need);
      if (need != props_len)
         print_warn("填充返回 %d != 探测 %d（内核允许，按 %d 解析）", need, props_len, need);
      props_len = need;

      props_n = gpuprops_parse(props_buf, (size_t)props_len,
                               props, KBASE_PROP_MAX_ENT, &props_consumed);
      print_pass("blob 解析: %u 条属性，消费 %zu / %d 字节%s",
                 props_n, props_consumed, props_len,
                 (props_consumed == (size_t)props_len) ? "（完全对齐）" : "（尾部有剩余/截断，见 WARN）");
      if (props_consumed != (size_t)props_len)
         print_warn("blob 未被完全消费（%zu/%d），属性表可能不完整",
                    props_consumed, props_len);
   }
   cap_props = 1;

   /* 任务书要求的 6 个关键属性（+ 少量旁证属性） */
   printf("\n  ── 关键属性（mk.h:88-144 的键值）──\n");
   {
      char derive[128];
      uint64_t v = 0;

      if (gpuprops_find(props, props_n, KBASE_GPUPROP_L2_LOG2_CACHE_SIZE, &v)) {
         snprintf(derive, sizeof(derive), "  → L2 cache = %" PRIu64 " KB",
                  (uint64_t)1 << v);
      } else {
         derive[0] = '\0';
      }
      print_prop(props, props_n, KBASE_GPUPROP_PRODUCT_ID,         "PRODUCT_ID", NULL);
      print_prop(props, props_n, KBASE_GPUPROP_L2_LOG2_CACHE_SIZE, "L2_LOG2_CACHE_SIZE", derive);
      print_prop(props, props_n, KBASE_GPUPROP_L2_NUM_L2_SLICES,   "L2_NUM_L2_SLICES", NULL);
      print_prop(props, props_n, KBASE_GPUPROP_RAW_JS_PRESENT,     "RAW_JS_PRESENT", NULL);
      print_prop(props, props_n, KBASE_GPUPROP_RAW_COHERENCY_MODE, "RAW_COHERENCY_MODE", NULL);

      if (gpuprops_find(props, props_n, KBASE_GPUPROP_GPU_FREQ_KHZ_MAX, &v)) {
         snprintf(derive, sizeof(derive), "  → %" PRIu64 ".%03" PRIu64 " GHz",
                  (uint64_t)(v / 1000000ull), (uint64_t)((v % 1000000ull) / 1000ull));
      } else {
         derive[0] = '\0';
      }
      print_prop(props, props_n, KBASE_GPUPROP_GPU_FREQ_KHZ_MAX, "GPU_FREQ_KHZ_MAX", derive);

      printf("\n  ── 旁证属性 ──\n");
      print_prop(props, props_n, KBASE_GPUPROP_RAW_GPU_ID,        "RAW_GPU_ID", NULL);
      print_prop(props, props_n, KBASE_GPUPROP_VERSION_STATUS,    "VERSION_STATUS", NULL);
      print_prop(props, props_n, KBASE_GPUPROP_MAJOR_REVISION,    "MAJOR_REVISION", NULL);
      print_prop(props, props_n, KBASE_GPUPROP_MINOR_REVISION,    "MINOR_REVISION", NULL);
      print_prop(props, props_n, KBASE_GPUPROP_L2_LOG2_LINE_SIZE, "L2_LOG2_LINE_SIZE", NULL);
      print_prop(props, props_n, KBASE_GPUPROP_MAX_THREADS,       "MAX_THREADS", NULL);
      print_prop(props, props_n, KBASE_GPUPROP_COHERENCY_NUM_GROUPS, "COHERENCY_NUM_GROUPS", NULL);

      if (gpuprops_find(props, props_n, KBASE_GPUPROP_GPU_AVAILABLE_MEMORY_SIZE, &v)) {
         snprintf(derive, sizeof(derive), "  → %" PRIu64 " MB",
                  (uint64_t)(v / 1024ull / 1024ull));
      } else {
         derive[0] = '\0';
      }
      print_prop(props, props_n, KBASE_GPUPROP_GPU_AVAILABLE_MEMORY_SIZE,
                 "GPU_AVAILABLE_MEMORY_SIZE", derive);
   }

   /* 世代交叉验证：CSF 世代没有 JM 的 job slot */
   {
      uint64_t js = 0, cm = 0;
      if (gpuprops_find(props, props_n, KBASE_GPUPROP_RAW_JS_PRESENT, &js)) {
         if (js == 0 && is_csf)
            print_pass("RAW_JS_PRESENT=0 与 CSF 握手一致 ⇒ 确认走 CSF 命令队列（无 job slot）");
         else if (js == 0 && !is_csf)
            print_warn("RAW_JS_PRESENT=0 但握手走的是 JM，世代判定可能有问题");
         else if (js != 0 && is_csf)
            print_warn("RAW_JS_PRESENT=%" PRIu64 " 非 0 但握手走 CSF，需人工核对", js);
      }
      if (gpuprops_find(props, props_n, KBASE_GPUPROP_RAW_COHERENCY_MODE, &cm) && cm == 0)
         print_warn("RAW_COHERENCY_MODE=0 ⇒ 非一致内存：真正跑 GPU 时"
                    "用户态/内核必须自己做 cache 维护（本自检只做 CPU 往返读写，不涉及）");
   }

   /* ================================================================
    * 步骤 4 —— SET_FLAGS（创建内核侧上下文 / 设置标志）
    *   kb.c:1148 用 create_flags = 0（mk.h:52 BASE_CONTEXT_CREATE_FLAG_NONE）。
    *   注：kb.c 把这一步放在 GET_GPUPROPS 之前；本程序按任务书顺序放在之后
    *   （两条顺序在本机都成立，见文件头"已知的顺序偏差"）。
    * ================================================================ */
   step_title("SET_FLAGS (KBASE_IOCTL_SET_FLAGS, nr=1) —— 创建 kbase 上下文");
   {
      struct kbase_ioctl_set_flags set_flags;
      set_flags.create_flags = 0;   /* mk.h:52 BASE_CONTEXT_CREATE_FLAG_NONE */

      errno = 0;
      if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &set_flags) != 0) {
         print_fail("SET_FLAGS 失败: errno=%d (%s)", errno, strerror(errno));
         goto conclusion;
      }
      print_pass("SET_FLAGS 成功 (create_flags=0x%08X = NONE)，内核上下文已建立",
                 set_flags.create_flags);
   }
   cap_flags = 1;

   /* ================================================================
    * 步骤 5 —— 内存分配 + 映射 + 读写校验 + 释放（重点）
    *   5a) mmap tracking page —— kb.c:1156-1162："内核在任何内存操作之前要求它"。
    *       这里降级为 WARN（文件头偏差 3），失败也继续，好拿到 MEM_ALLOC 的真实结果。
    *   5b) 5c) JIT_INIT / EXEC_INIT —— kb.c:1180-1199，都是 best-effort（失败仅 WARN）：
    *       · JIT_INIT 在 64 位客户端上负责从 SAME_VA 顶部划出 CUSTOM_VA 区，
    *         内核内部那些"tiler heap context/chunk"分配来自该区（kb.c:1172-1179）。
    *       · EXEC_INIT 只为 GPU 可执行（EXEC_VA zone）分配服务；CSF uAPI >= 1.9 下
    *         内核自动建区，这一步可能是 no-op。
    *   5d) MEM_ALLOC_EX（CSF uAPI >= 1.9，kb.c:1570-1591）否则 MEM_ALLOC（kb.c:1593-1605）。
    *   5e) mmap(cookie) 建立映射（kb.c:1621-1622）：SAME_VA 时 mmap 返回的 CPU 地址
    *       同时就是 GPU VA（mk.h:149-155 / kb.c:1613-1619）。
    *   5f) 写入已知模式再读回校验（本步骤的核心证据）。
    *   5g) 释放：SAME_VA → munmap（free-on-close，**不得**再 MEM_FREE，kb.c:1658-1673）；
    *       非 SAME_VA → munmap + MEM_FREE（kb.c:1666-1673）。
    *   5h) [附加] 用"未 mmap 的 SAME_VA cookie"补测 MEM_FREE 本身可用性
    *       （kb.c:1628 注释称其可用；失败只 WARN）。
    * ================================================================ */
   step_title("内存分配 + mmap + 读写校验 + 释放（kbase 内存链路）");

   /* --- 5a tracking page --- */
   errno = 0;
   tracking = mmap(NULL, (size_t)KBASE_PAGE_SIZE, PROT_NONE, MAP_SHARED, fd,
                   (off_t)KBASE_BASE_MEM_MAP_TRACKING_HANDLE);
   if (tracking == MAP_FAILED) {
      print_warn("mmap(BASE_MEM_MAP_TRACKING_HANDLE=0x%llX) 失败: errno=%d (%s)"
                 " —— kb.c:1156 视其为致命，这里继续以取得分配的真实结果",
                 (unsigned long long)KBASE_BASE_MEM_MAP_TRACKING_HANDLE,
                 errno, strerror(errno));
   } else {
      print_pass("5a tracking page 已映射 (offset=0x%llX, 4KiB, PROT_NONE, MAP_SHARED) → %p",
                 (unsigned long long)KBASE_BASE_MEM_MAP_TRACKING_HANDLE, tracking);
   }

   /* --- 5b JIT_INIT（best-effort，kb.c:1180-1188）--- */
   {
      /* mk.h:254-261 struct kbase_ioctl_mem_jit_init；kb.c:1180-1184 的取值 */
      struct kbase_ioctl_mem_jit_init {
         uint64_t va_pages;
         uint8_t  max_allocations;
         uint8_t  trim_level;
         uint8_t  group_id;
         uint8_t  padding[5];
         uint64_t phys_pages;
      } jit;
      /* mk.h:262-263 KBASE_IOCTL_MEM_JIT_INIT = _IOW(0x80,14,sizeof=24) */
      const unsigned long KBASE_IOCTL_MEM_JIT_INIT =
         KBASE_IOC(KBASE_IOC_WRITE, KBASE_IOCTL_TYPE, 14, sizeof(jit));

      memset(&jit, 0, sizeof(jit));
      jit.va_pages        = 1ull << 25;
      jit.max_allocations = 255;
      jit.phys_pages      = 1ull << 25;

      errno = 0;
      if (ioctl(fd, KBASE_IOCTL_MEM_JIT_INIT, &jit) != 0)
         print_warn("5b MEM_JIT_INIT(nr=14) 失败: errno=%d (%s) —— kb.c:1185 同样只告警"
                    "（tiler heap 会不可用，但普通 SAME_VA 分配不受影响）",
                    errno, strerror(errno));
      else
         print_pass("5b MEM_JIT_INIT 成功（JIT/CUSTOM_VA 区已建立）");
   }

   /* --- 5c EXEC_INIT（best-effort，kb.c:1195-1199）--- */
   {
      /* mk.h:343-347 struct kbase_ioctl_mem_exec_init { __u64 va_pages; } */
      struct kbase_ioctl_mem_exec_init {
         uint64_t va_pages;
      } exec;
      /* mk.h:346-347 KBASE_IOCTL_MEM_EXEC_INIT = _IOW(0x80,38,sizeof=8) → 0x40089826 */
      const unsigned long KBASE_IOCTL_MEM_EXEC_INIT =
         KBASE_IOC(KBASE_IOC_WRITE, KBASE_IOCTL_TYPE, 38, sizeof(exec));

      exec.va_pages = 0x100000;   /* kb.c:1195；4G 可执行 VA */

      errno = 0;
      if (ioctl(fd, KBASE_IOCTL_MEM_EXEC_INIT, &exec) != 0)
         print_warn("5c MEM_EXEC_INIT(nr=38) 失败: errno=%d (%s) —— kb.c:1196 同样只告警"
                    "（CSF uAPI >= 1.9 下该区由内核自动建立，可能是 no-op）",
                    errno, strerror(errno));
      else
         print_pass("5c MEM_EXEC_INIT 成功（EXEC_VA 区已初始化）");
   }

   /* --- 5d 分配 --- */
   bo_size = 16ull * KBASE_PAGE_SIZE;   /* 64 KiB，跨 16 页 */
   alloc_flags = KBASE_SELFTEST_ALLOC_FLAGS;
   used_alloc_ex = is_csf && (uapi_major > 1 || (uapi_major == 1 && uapi_minor >= 9));

   if (used_alloc_ex) {
      /* kb.c:1570-1591 */
      union kbase_ioctl_mem_alloc_ex req;
      memset(&req, 0, sizeof(req));
      req.in.va_pages     = bo_size / KBASE_PAGE_SIZE;
      req.in.commit_pages = req.in.va_pages;   /* kb.c:1558 commit_pages = va_pages */
      req.in.extension    = 0;                 /* 非 growable */
      req.in.flags        = alloc_flags;       /* kb.c:1560 to_kbase_mem_flags() */
      req.in.fixed_address = 0;                /* mk.h:174 未请求 BASE_MEM_FIXED 时必须为 0 */

      print_info("5d 用 MEM_ALLOC_EX (nr=59)：va_pages=%" PRIu64 " commit_pages=%" PRIu64
                 " extension=0 flags=0x%" PRIx64 " (uAPI %u.%u >= 1.9)",
                 req.in.va_pages, req.in.commit_pages, req.in.flags,
                 (unsigned)uapi_major, (unsigned)uapi_minor);
      errno = 0;
      if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC_EX, &req) != 0) {
         print_fail("MEM_ALLOC_EX 失败: errno=%d (%s)", errno, strerror(errno));
         goto conclusion;
      }
      alloc_flags = req.out.flags;
      alloc_cookie = req.out.gpu_va;
   } else {
      /* kb.c:1593-1605 */
      union kbase_ioctl_mem_alloc req;
      memset(&req, 0, sizeof(req));
      req.in.va_pages     = bo_size / KBASE_PAGE_SIZE;
      req.in.commit_pages = req.in.va_pages;
      req.in.extension    = 0;
      req.in.flags        = alloc_flags;

      print_info("5d 用旧 MEM_ALLOC (nr=5)：va_pages=%" PRIu64 " commit_pages=%" PRIu64
                 " extension=0 flags=0x%" PRIx64 " (%s uAPI %u.%u < 1.9)",
                 req.in.va_pages, req.in.commit_pages, req.in.flags,
                 is_csf ? "CSF" : "JM", (unsigned)uapi_major, (unsigned)uapi_minor);
      errno = 0;
      if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, &req) != 0) {
         print_fail("MEM_ALLOC 失败: errno=%d (%s)", errno, strerror(errno));
         goto conclusion;
      }
      alloc_flags = req.out.flags;
      alloc_cookie = req.out.gpu_va;
   }

   bo_cookie_va = alloc_cookie;
   {
      bool same_va = (alloc_flags & KBASE_BASE_MEM_SAME_VA) != 0;
      print_pass("5d 分配成功: %" PRIu64 " 字节 (%" PRIu64 " 页)；out.flags=0x%" PRIx64
                 "；out.gpu_va=0x%" PRIx64,
                 bo_size, (uint64_t)(bo_size / KBASE_PAGE_SIZE), alloc_flags, alloc_cookie);
      print_info("SAME_VA=%s ⇒ out.gpu_va 是 %s",
                 same_va ? "是 (bit13)" : "否",
                 same_va ? "mmap cookie（mmap 后 CPU 地址 == GPU VA，mk.h:149-155）"
                         : "真 GPU VA（同时充当 mmap offset，mk.h:153-155）");
      if (same_va == 0 && is_csf)
         print_warn("CSF 世代上非 SAME_VA 的分配少见（64 位客户端默认强制 SAME_VA，mk.h:149-152）");
      cap_alloc = 1;
   }

   /* --- 5e mmap --- */
   errno = 0;
   bo = mmap(NULL, (size_t)bo_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
             (off_t)bo_cookie_va);
   if (bo == MAP_FAILED) {
      print_fail("mmap(BO, size=%" PRIu64 ", offset=0x%" PRIx64 ") 失败: errno=%d (%s)",
                 bo_size, bo_cookie_va, errno, strerror(errno));
      /* kb.c:1628-1631 的错误路径：cookie 也能被 MEM_FREE 回收 */
      {
         struct kbase_ioctl_mem_free fr;
         fr.gpu_addr = bo_cookie_va;
         (void)ioctl(fd, KBASE_IOCTL_MEM_FREE, &fr);
         print_info("已按 kb.c:1629 对 cookie 调用 MEM_FREE 回收，避免泄漏");
      }
      goto conclusion;
   }
   print_pass("5e mmap 成功: %p (size=%" PRIu64 ", prot=RW, MAP_SHARED, offset=0x%" PRIx64 ")",
              bo, bo_size, bo_cookie_va);
   if ((alloc_flags & KBASE_BASE_MEM_SAME_VA) != 0)
      print_info("CPU VA == GPU VA = 0x%" PRIx64 "（SAME_VA 语义，kb.c:1614-1616）",
                 (uint64_t)(uintptr_t)bo);
   cap_map = 1;

   /* --- 5f 写入已知模式 + 读回校验 --- */
   {
      uint8_t *p = (uint8_t *)bo;
      size_t i;
      size_t first_bad = (size_t)-1;
      uint64_t sum_w = 0, sum_r = 0;

      for (i = 0; i < (size_t)bo_size; i++) {
         p[i] = pattern_byte(i);
         sum_w += p[i];
      }
      for (i = 0; i < (size_t)bo_size; i++) {
         uint8_t exp = pattern_byte(i);
         sum_r += p[i];
         if (p[i] != exp && first_bad == (size_t)-1)
            first_bad = i;
      }

      if (first_bad != (size_t)-1) {
         print_fail("5f 读写校验失败：首个不一致字节 offset=%zu (写入 0x%02X, 读回 0x%02X)",
                    first_bad, pattern_byte(first_bad), p[first_bad]);
         goto conclusion;
      }
      print_pass("5f 读写校验通过：写入并读回 %" PRIu64 " 字节全部一致"
                 "（模式 (i*131+17)^(i>>8)；校验和 0x%" PRIx64 "==0x%" PRIx64 "）",
                 bo_size, sum_w, sum_r);
      print_info("这证明：GPU 内存已真正被申请并可在用户态读写"
                 "（非一致内存 GPU 侧可见性还需 cache 维护，本自检不涉及）");
      cap_rw = 1;
   }

   /* --- 5g 释放 --- */
   {
      bool same_va = (alloc_flags & KBASE_BASE_MEM_SAME_VA) != 0;

      if (same_va) {
         /* kb.c:1658-1664 + 1669-1673：SAME_VA 由 munmap 整块销毁，不得再 MEM_FREE */
         errno = 0;
         if (munmap(bo, (size_t)bo_size) != 0) {
            print_fail("munmap(BO) 失败: errno=%d (%s)", errno, strerror(errno));
            goto conclusion;
         }
         bo = MAP_FAILED;
         print_pass("5g munmap 释放成功（SAME_VA=free-on-close，kb.c:1658-1664）");
         print_info("按 kb.c:1669-1673，SAME_VA 区域**不**再调用 MEM_FREE"
                    "（munmap 已销毁区域，再调会命中失效 VA）—— 这是对任务书"
                    "\"用 MEM_FREE 释放\"的有意偏离，语义以 kb.c 为准");
      } else {
         /* kb.c:1666-1673：先 munmap 掉 CPU 映射，再 MEM_FREE 销毁 zone 区域 */
         struct kbase_ioctl_mem_free fr;
         errno = 0;
         if (munmap(bo, (size_t)bo_size) != 0) {
            print_fail("munmap(BO) 失败: errno=%d (%s)", errno, strerror(errno));
            goto conclusion;
         }
         bo = MAP_FAILED;
         fr.gpu_addr = bo_cookie_va;
         errno = 0;
         if (ioctl(fd, KBASE_IOCTL_MEM_FREE, &fr) != 0) {
            print_fail("MEM_FREE(0x%" PRIx64 ") 失败: errno=%d (%s)",
                       bo_cookie_va, errno, strerror(errno));
            goto conclusion;
         }
         print_pass("5g munmap + MEM_FREE 成功（非 SAME_VA zone 区域，kb.c:1666-1673）");
      }
      cap_free = 1;
   }

   /* --- 5h [附加] 未 mmap 的 SAME_VA cookie 上补测 MEM_FREE --- */
   {
      union kbase_ioctl_mem_alloc_ex req;
      struct kbase_ioctl_mem_free fr;
      uint64_t ck;

      memset(&req, 0, sizeof(req));
      req.in.va_pages     = 1;
      req.in.commit_pages = 1;
      req.in.extension    = 0;
      req.in.flags        = KBASE_SELFTEST_ALLOC_FLAGS;

      errno = 0;
      if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC_EX, &req) != 0) {
         print_warn("5h[附加] 追加 1 页分配失败: errno=%d (%s)，跳过 MEM_FREE 补测",
                    errno, strerror(errno));
      } else {
         ck = req.out.gpu_va;   /* 未 mmap，仍是 pending cookie */
         fr.gpu_addr = ck;
         errno = 0;
         if (ioctl(fd, KBASE_IOCTL_MEM_FREE, &fr) != 0)
            print_warn("5h[附加] 对未 mmap 的 cookie 调 MEM_FREE(0x%" PRIx64 ") 失败: "
                       "errno=%d (%s)；kb.c:1628 注释称其应可用 —— 该路径待确认",
                       ck, errno, strerror(errno));
         else
            print_pass("5h[附加] MEM_FREE 对未 mmap 的 SAME_VA cookie 成功"
                       "(0x%" PRIx64 ") ⇒ MEM_FREE 通道本身可用（kb.c:1628）", ck);
      }
   }

   /* ================================================================
    * 步骤 6 —— [可选附加检查，失败只 WARN，不影响退出码]
    *   6a) CS_GET_GLB_IFACE 两段式（kb.c:394-452）：CSF 队列组/流能力
    *   6b) CSF USER 寄存器页 mmap（kb.c:1252-1254）：LATEST_FLUSH
    * ================================================================ */
   step_title("[可选] CSF 附加能力检查（失败只 WARN，不影响退出码）");

   if (!is_csf) {
      print_info("6a/6b 仅对 CSF 世代有意义，本机是 JM，跳过");
   } else {
      /* --- 6a CS_GET_GLB_IFACE --- */
      union kbase_ioctl_cs_get_glb_iface glb;
      uint32_t group_num, stream_num;
      struct basep_cs_group_control *groups = NULL;
      struct basep_cs_stream_control *streams = NULL;

      memset(&glb, 0, sizeof(glb));
      errno = 0;
      if (ioctl(fd, KBASE_IOCTL_CS_GET_GLB_IFACE, &glb) != 0) {
         print_warn("6a CS_GET_GLB_IFACE 第一段失败: errno=%d (%s)", errno, strerror(errno));
      } else {
         group_num  = glb.out.group_num;
         stream_num = glb.out.total_stream_num;
         print_pass("6a CS_GET_GLB_IFACE 第一段: glb_version=0x%X features=0x%X "
                    "instr_features=0x%X group_num=%u total_stream_num=%u prfcnt_size=%u",
                    glb.out.glb_version, glb.out.features, glb.out.instr_features,
                    group_num, stream_num, glb.out.prfcnt_size);

         if (group_num && stream_num) {
            groups  = (struct basep_cs_group_control *)calloc(group_num, sizeof(*groups));
            streams = (struct basep_cs_stream_control *)calloc(stream_num, sizeof(*streams));
            if (!groups || !streams) {
               print_warn("6a 第二段缓冲区 calloc 失败");
            } else {
               glb.in.max_group_num        = group_num;
               glb.in.max_total_stream_num = stream_num;
               glb.in.groups_ptr           = (uint64_t)(uintptr_t)groups;
               glb.in.streams_ptr          = (uint64_t)(uintptr_t)streams;

               errno = 0;
               if (ioctl(fd, KBASE_IOCTL_CS_GET_GLB_IFACE, &glb) != 0) {
                  print_warn("6a CS_GET_GLB_IFACE 第二段失败: errno=%d (%s)",
                             errno, strerror(errno));
               } else {
                  print_pass("6a 第二段成功: stream[0].features=0x%X "
                             "(work_regs=%u, scoreboard_slots=%u), group[0].stream_num=%u "
                             "suspend_size=%u",
                             streams[0].features,
                             streams[0].features & 0xffu,
                             (streams[0].features >> 8) & 0xffu,
                             groups[0].stream_num, groups[0].suspend_size);
                  print_info("CSF 队列组/流接口可用 ⇒ 具备命令提交（CS queue）的接口基础；"
                             "kb.c 目前只实现到内存管理，提交路径尚未打通");
               }
            }
            free(groups);
            free(streams);
         } else {
            print_warn("6a 内核上报 group_num/stream_num 为 0，无法做第二段填充");
         }
      }

      /* --- 6b CSF USER 寄存器页（LATEST_FLUSH）--- */
      {
         volatile uint32_t *ureg;
         errno = 0;
         ureg = (volatile uint32_t *)mmap(NULL, (size_t)KBASE_PAGE_SIZE, PROT_READ,
                                          MAP_SHARED, fd,
                                          (off_t)KBASE_BASEP_MEM_CSF_USER_REG_PAGE_HANDLE);
         if (ureg == MAP_FAILED) {
            print_warn("6b mmap(BASEP_MEM_CSF_USER_REG_PAGE_HANDLE=0x%llX) 失败: "
                       "errno=%d (%s) —— kb.c:1255 同样只告警（仅影响 flush id 优化）",
                       (unsigned long long)KBASE_BASEP_MEM_CSF_USER_REG_PAGE_HANDLE,
                       errno, strerror(errno));
         } else {
            uint32_t latest_flush = ureg[0];   /* mk.h:405 CS_USER_REG_LATEST_FLUSH @0 */
            print_pass("6b CSF USER 寄存器页已映射 → %p, LATEST_FLUSH=%u (0x%X)",
                       (const void *)ureg, latest_flush, latest_flush);
            munmap((void *)ureg, (size_t)KBASE_PAGE_SIZE);
         }
      }
   }

   /* ================================================================
    * 结论
    * ================================================================ */
conclusion:
   printf("\n======================================================================\n");
   printf(" 结论\n");
   printf("======================================================================\n");

   /* 能力清单：逐项来自本程序实测（cap_* 是各步骤的成功标志） */
   printf(" 能力清单（本进程实测，uid=%u，无 root）：\n", (unsigned)getuid());
   printf("   %s 打开 /dev/mali0 (O_RDWR)\n",                       cap_open ? "✔" : "✘");
   printf("   %s kbase uAPI 版本握手（首个 ioctl）%s 回传 %u.%u\n",
          cap_handshake ? "✔" : "✘", is_csf ? "CSF nr=52" : "JM nr=0",
          (unsigned)uapi_major, (unsigned)uapi_minor);
   printf("   %s GET_GPUPROPS 两段式读取 GPU 属性 (%d 字节 / %u 条)\n",
          cap_props ? "✔" : "✘", props_len, props_n);
   printf("   %s SET_FLAGS 建立内核上下文\n",                       cap_flags ? "✔" : "✘");
   printf("   %s %s 分配 %" PRIu64 " 字节 GPU 内存 (0x%" PRIx64 ")\n",
          cap_alloc ? "✔" : "✘", used_alloc_ex ? "MEM_ALLOC_EX" : "MEM_ALLOC",
          bo_size, bo_cookie_va);
   printf("   %s mmap 建立 CPU 映射\n",                              cap_map ? "✔" : "✘");
   printf("   %s 写入已知模式并读回校验通过（%" PRIu64 " 字节全一致）\n",
          cap_rw ? "✔" : "✘", bo_size);
   printf("   %s 内存释放（见上方 5g）\n",                            cap_free ? "✔" : "✘");

   if (g_fail == 0 && cap_rw) {
      printf("\n ⇒ 结论：普通 App 进程可在免 root 下直达并驱动 Mali kbase，"
             "\n          具备移植 Mesa PanVK / KRAID（kbase 后端）的**内存管理最低前提**。\n");
      printf("   注意（本自检未覆盖，需后续单独验证）：\n");
      printf("     · CSF 命令队列提交（CS_QUEUE_REGISTER/BIND/KICK + CS 指令）与真实渲染\n");
      printf("     · 非一致内存（RAW_COHERENCY_MODE 见上）下的 CPU↔GPU cache 维护\n");
      printf("     · 厂商 gralloc / dma-buf 导入与 WSI 路径\n");
      printf("\n 退出码 0 = 全部必需步骤通过（另有 %u 个 WARN，见上）\n", g_warn);
      rc = 0;
   } else {
      printf(" 自检未全通过：必需步骤失败 %u 个，WARN %u 个。\n", g_fail, g_warn);
      printf(" 如上 FAIL 行所示，第一处失败即停止 —— 该能力在当前进程/ROM 下不可用，\n");
      printf(" 或需要换更接近目标 App 的 uid/SELinux 域重试。\n");
      printf("\n 退出码 1\n");
      rc = 1;
   }
   printf("======================================================================\n");

   /* 清理 */
   if (bo != MAP_FAILED)
      munmap(bo, (size_t)bo_size);
   if (tracking != MAP_FAILED)
      munmap(tracking, (size_t)KBASE_PAGE_SIZE);
   free(props_buf);
   if (fd >= 0)
      close(fd);

   return rc;
}

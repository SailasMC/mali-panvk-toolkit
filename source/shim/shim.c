/*
 * libvkpanvk_shim.so —— Vulkan 转发垫片（Android / NDK）
 * ============================================================================
 * 目标：让「链接期直接依赖 loader 风格 libvulkan.so」的 libMobileGL.so，
 *       改走我们自己的标准 Vulkan ICD（Mesa PanVK / Mali-G720）。
 *
 * 背景（实测）：Android 系统 libvulkan.so 完全忽略 VK_ICD_FILENAMES，因此
 *   只能用本垫片顶掉 MGL 的 DT_NEEDED（patchelf --replace-needed libvulkan.so
 *   libvkpanvk_shim.so），在进程内直接把 125 个 loader 风格入口转发给 ICD。
 *
 * 为什么不叫 libvulkan.so：会污染整个进程（JVM 启动/hook 全都命中），
 *   实测 JVM 卡死在 Calling JLI_Launch。唯一命名 + 只被 MGL 依赖才是安全解。
 *
 * 语义：ICD 的句柄语义与 loader 一致（VkInstance/VkPhysicalDevice/VkDevice
 *   都是 ICD 自己产生的指针），转发时**原样透传**，不做任何翻译。
 *
 * 解析路径（按首参句柄类型分类，见 gen_report.txt）：
 *   VkInstance        -> icd_gipa(该 instance)
 *   VkPhysicalDevice  -> icd_gpdpa(g_inst) 优先，回落 icd_gipa(g_inst)
 *   VkDevice          -> icd gdpa(device) 优先，回落 icd_gipa(g_inst)
 *   VkQueue/CmdBuffer -> 无句柄，用记录下来的唯一 device
 *   无首参/创建期     -> icd_gipa(NULL, name)
 *
 * 延迟加载：**不在 constructor 里 dlopen**（避免与 linker 锁/其他线程构造期
 *   互锁，这正是上一版 JVM 卡死的形态之一），改为首次解析时按需加载。
 *
 * 日志：同时写 stderr（进 latest_game.log）和 logcat(tag=VKPANVK_SHIM)。
 */
#define _GNU_SOURCE 1
#define VK_USE_PLATFORM_ANDROID_KHR 1

#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <android/log.h>
#include <vulkan/vulkan.h>

#define SHIM_TAG "VKPANVK_SHIM"
#define SHIM_EXPORT __attribute__((visibility("default")))

#define LOGI(...)                                                              \
    do {                                                                       \
        __android_log_print(ANDROID_LOG_INFO, SHIM_TAG, __VA_ARGS__);           \
        fprintf(stderr, "[vkshim] " __VA_ARGS__);                               \
        fputc('\n', stderr);                                                    \
        fflush(stderr);                                                         \
    } while (0)

#define SHIM_BUILD "routeA-nonce-0728"

/* 只打日志、**不做 dlopen** 的构造函数：用来在日志里确认「垫片确实被加载了」。
   上一版失败尝试的 JVM 卡死与"构造函数里 dlopen 20MB ICD"有关，这里刻意避开。 */
__attribute__((constructor)) static void shim_ctor(void)
{
    LOGI("constructor: libvkpanvk_shim.so LOADED build=%s", SHIM_BUILD);
}

/* ---------------------------------------------------------------- ICD 状态 */
typedef PFN_vkVoidFunction (*shim_icd_gipa_t)(VkInstance, const char *);
typedef PFN_vkVoidFunction (*shim_icd_gpdpa_t)(VkInstance, const char *);
typedef VkResult (*shim_icd_negotiate_t)(uint32_t *);

static void              *g_icd       = NULL;
static shim_icd_gipa_t    g_icd_gipa  = NULL;
static shim_icd_gpdpa_t   g_icd_gpdpa = NULL;
static VkInstance         g_inst      = NULL;
static VkDevice           g_dev       = NULL;
static PFN_vkGetDeviceProcAddr g_gdpa = NULL;
static pthread_mutex_t    g_lock      = PTHREAD_MUTEX_INITIALIZER;
static int                g_state     = 0;   /* 0=未加载 1=成功 2=失败 */

/* ------------------------------------------------------- 手写特殊函数前置声明 */
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *, const VkAllocationCallbacks *, VkInstance *);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance, const VkAllocationCallbacks *);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo *, const VkAllocationCallbacks *, VkDevice *);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice, const VkAllocationCallbacks *);
SHIM_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char *);
SHIM_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char *);

/* --------------------------------------------------------------- 目录探测 */
static int self_dir(char *out, size_t cap)
{
    Dl_info info;
    if (!dladdr((void *)(uintptr_t)&self_dir, &info) || !info.dli_fname)
        return 0;
    const char *s = strrchr(info.dli_fname, '/');
    if (!s)
        return 0;
    size_t n = (size_t)(s - info.dli_fname) + 1;
    if (n + 1 > cap)
        return 0;
    memcpy(out, info.dli_fname, n);
    out[n] = '\0';
    return 1;
}

/* 从 /proc/self/maps 里找出某个已加载库所在目录（用来定位 libMobileGL.so） */
static int dir_of_map(const char *needle, char *out, size_t cap)
{
    FILE *f = fopen("/proc/self/maps", "re");
    if (!f)
        return 0;
    char line[2048];
    int ok = 0;
    while (fgets(line, (int)sizeof line, f)) {
        if (!strstr(line, needle))
            continue;
        char *p = strchr(line, '/');
        if (!p)
            continue;
        char *nl = strchr(p, '\n');
        if (nl)
            *nl = '\0';
        char *s = strrchr(p, '/');
        if (!s)
            continue;
        size_t n = (size_t)(s - p) + 1;
        if (n + 1 > cap)
            continue;
        memcpy(out, p, n);
        out[n] = '\0';
        ok = 1;
        break;
    }
    fclose(f);
    return ok;
}

/* 解析 VK_ICD_FILENAMES / VK_DRIVER_FILES 指向的 json 里的 library_path */
static int path_from_icd_env(char *out, size_t cap)
{
    const char *ev = getenv("VK_ICD_FILENAMES");
    if (!ev || !*ev)
        ev = getenv("VK_DRIVER_FILES");
    if (!ev || !*ev)
        return 0;

    char json[512];
    const char *colon = strchr(ev, ':');
    size_t n = colon ? (size_t)(colon - ev) : strlen(ev);
    if (n >= sizeof json)
        n = sizeof json - 1;
    memcpy(json, ev, n);
    json[n] = '\0';

    FILE *f = fopen(json, "re");
    if (!f) {
        LOGI("VK_ICD json 打不开: %s", json);
        return 0;
    }
    char buf[8192];
    size_t got = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[got] = '\0';

    char *k = strstr(buf, "library_path");
    if (!k)
        return 0;
    char *q = strchr(k, ':');
    if (!q)
        return 0;
    q = strchr(q, '"');
    if (!q)
        return 0;
    char *q2 = strchr(q + 1, '"');
    if (!q2)
        return 0;
    char lib[256];
    size_t ln = (size_t)(q2 - q - 1);
    if (ln >= sizeof lib)
        ln = sizeof lib - 1;
    memcpy(lib, q + 1, ln);
    lib[ln] = '\0';

    if (lib[0] == '/') {
        snprintf(out, cap, "%s", lib);
        return 1;
    }
    const char *s = strrchr(json, '/');            /* 相对 json 目录 */
    if (!s)
        return 0;
    size_t dn = (size_t)(s - json) + 1;
    if (dn + ln + 1 > cap)
        return 0;
    memcpy(out, json, dn);
    memcpy(out + dn, lib, ln + 1);
    return 1;
}

/* ------------------------------------------------------------- ICD 加载 */
static void *icd_probe(const char *path, const char *why)
{
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        LOGI("dlopen FAIL  (%s) %s : %s", why, path, dlerror());
        return NULL;
    }
    LOGI("dlopen OK    (%s) %s -> %p", why, path, h);

    shim_icd_gipa_t gipa = (shim_icd_gipa_t)dlsym(h, "vk_icdGetInstanceProcAddr");
    if (!gipa) {
        LOGI("REJECT %s : 没有 vk_icdGetInstanceProcAddr", path);
        dlclose(h);
        return NULL;
    }
    shim_icd_negotiate_t neg =
        (shim_icd_negotiate_t)dlsym(h, "vk_icdNegotiateLoaderICDInterfaceVersion");
    if (neg) {
        uint32_t v = 7;
        VkResult r = neg(&v);
        LOGI("negotiate(%s) -> %d, ver=%u", path, (int)r, v);
    } else {
        LOGI("WARN %s : 没有 vk_icdNegotiateLoaderICDInterfaceVersion", path);
    }
    g_icd       = h;
    g_icd_gipa  = gipa;
    g_icd_gpdpa = (shim_icd_gpdpa_t)dlsym(h, "vk_icdGetPhysicalDeviceProcAddr");
    LOGI("ICD READY  gipa=%p gpdpa=%p", (void *)gipa, (void *)g_icd_gpdpa);
    return h;
}

static void shim_load_icd(void)
{
    if (g_state)
        return;
    pthread_mutex_lock(&g_lock);
    if (g_state) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    g_state = 2;                                    /* 先标失败，成功再改 1 */

    char sd[512];  sd[0] = '\0';
    char md[512];  md[0] = '\0';
    char ep[768];  ep[0] = '\0';
    int have_sd = self_dir(sd, sizeof sd);
    int have_md = dir_of_map("libMobileGL.so", md, sizeof md);
    int have_ep = path_from_icd_env(ep, sizeof ep);
    LOGI("shim build=%s", SHIM_BUILD);
    LOGI("=== shim 首次加载 ICD === self=%s mgl=%s icdenv=%s",
         have_sd ? sd : "(none)", have_md ? md : "(none)", have_ep ? ep : "(none)");

    const char *names[] = {
        "libvulkan_freedreno.so",
        "libvulkan_panfrost.so",
        "libvulkan_panvk.so",
        NULL
    };
    const char *abs[] = {
        "/data/local/tmp/libvulkan_freedreno.so",
        "/data/local/tmp/libvulkan_panfrost.so",
        "/storage/emulated/0/mali-icd/libvulkan_freedreno.so",
        "/storage/emulated/0/mali-icd/libvulkan_panfrost.so",
        NULL
    };

    /* 1) 环境变量 json 里的绝对/相对路径（最权威） */
    if (have_ep && icd_probe(ep, "VK_ICD_FILENAMES"))
        goto ok;
    /* 2) 固定绝对路径 */
    for (int i = 0; abs[i]; i++)
        if (icd_probe(abs[i], "abs"))
            goto ok;
    /* 3) 目录 × 文件名 */
    {
        const char *dirs[3];
        int nd = 0;
        if (have_sd) dirs[nd++] = sd;
        if (have_md) dirs[nd++] = md;
        dirs[nd++] = "/data/local/tmp/";
        char p[1024];
        for (int d = 0; d < nd; d++) {
            for (int i = 0; names[i]; i++) {
                snprintf(p, sizeof p, "%s%s", dirs[d], names[i]);
                if (icd_probe(p, "dir"))
                    goto ok;
            }
        }
    }
    LOGI("FATAL: 找不到任何可用的 ICD，垫片将无法提供 Vulkan 功能");
    pthread_mutex_unlock(&g_lock);
    return;

ok:
    g_state = 1;
    pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------- 解析助手 */
static void *icd_gi(VkInstance inst, const char *n)
{
    return g_icd_gipa ? (void *)g_icd_gipa(inst, n) : NULL;
}

static void *shim_res_icd_null(const char *n)
{
    shim_load_icd();
    void *p = icd_gi(NULL, n);
    if (!p && g_inst)
        p = icd_gi(g_inst, n);
    return p;
}

static void *shim_res_instance(VkInstance inst, const char *n)
{
    shim_load_icd();
    void *p = icd_gi(inst, n);
    if (!p && inst != g_inst)
        p = icd_gi(g_inst, n);
    if (!p)
        p = icd_gi(NULL, n);
    return p;
}

static void *shim_res_physdev(const char *n)
{
    shim_load_icd();
    void *p = g_inst ? (g_icd_gpdpa ? (void *)g_icd_gpdpa(g_inst, n) : NULL) : NULL;
    if (!p && g_inst)
        p = icd_gi(g_inst, n);
    if (!p)
        p = icd_gi(NULL, n);
    return p;
}

static void *shim_res_device(VkDevice dev, const char *n)
{
    shim_load_icd();
    void *p = NULL;
    if (g_gdpa && dev)
        p = (void *)g_gdpa(dev, n);
    if (!p && g_inst)
        p = icd_gi(g_inst, n);
    if (!p)
        p = icd_gi(NULL, n);
    return p;
}

static void *shim_res_device_global(const char *n)
{
    return shim_res_device(g_dev, n);
}

/* ======================================================= 自动生成的 119 个 */
#include "shim_gen.inc"

/* ------------------------------------------------------------ 表查找 */
static void *shim_table_lookup(const char *n)
{
    for (const shim_entry_t *e = shim_table; e->name; e++)
        if (!strcmp(e->name, n))
            return e->fn;
    return NULL;
}

/* ------------------------------------------------------------ 手写特殊函数 */
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
{
    PFN_vkCreateInstance f =
        (PFN_vkCreateInstance)shim_res_icd_null("vkCreateInstance");
    if (!f) {
        LOGI("MISSING vkCreateInstance");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult r = f(pCreateInfo, pAllocator, pInstance);
    LOGI("vkCreateInstance -> %d", (int)r);
    if (r == VK_SUCCESS && pInstance && *pInstance) {
        g_inst = *pInstance;
        g_gdpa = (PFN_vkGetDeviceProcAddr)icd_gi(g_inst, "vkGetDeviceProcAddr");
        LOGI("g_inst=%p g_gdpa=%p", (void *)g_inst, (void *)g_gdpa);

        /* 直接把 ICD 报出来的物理设备名/版本打进日志 —— 成功判据的现场证据 */
        PFN_vkEnumeratePhysicalDevices epd =
            (PFN_vkEnumeratePhysicalDevices)icd_gi(g_inst, "vkEnumeratePhysicalDevices");
        PFN_vkGetPhysicalDeviceProperties gpp =
            (PFN_vkGetPhysicalDeviceProperties)shim_res_physdev("vkGetPhysicalDeviceProperties");
        if (epd && gpp) {
            uint32_t n = 0;
            if (epd(g_inst, &n, NULL) == VK_SUCCESS && n) {
                VkPhysicalDevice pd[8];
                uint32_t m = n > 8 ? 8 : n;
                if (epd(g_inst, &m, pd) == VK_SUCCESS) {
                    for (uint32_t i = 0; i < m; i++) {
                        VkPhysicalDeviceProperties pr;
                        memset(&pr, 0, sizeof pr);
                        gpp(pd[i], &pr);
                        LOGI("ICD device[%u] name='%s' api=%u.%u.%u drv=%u.%u.%u ext_count_ok=%d",
                             i, pr.deviceName,
                             VK_VERSION_MAJOR(pr.apiVersion),
                             VK_VERSION_MINOR(pr.apiVersion),
                             VK_VERSION_PATCH(pr.apiVersion),
                             VK_VERSION_MAJOR(pr.driverVersion),
                             VK_VERSION_MINOR(pr.driverVersion),
                             VK_VERSION_PATCH(pr.driverVersion), 1);
                    }
                }
            }
        }
    }
    return r;
}

SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator)
{
    PFN_vkDestroyInstance f =
        (PFN_vkDestroyInstance)shim_res_instance(instance, "vkDestroyInstance");
    LOGI("vkDestroyInstance %p", (void *)instance);
    if (instance == g_inst) {
        g_inst = NULL;
        g_gdpa = NULL;
    }
    if (f)
        f(instance, pAllocator);
}

SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDevice(VkPhysicalDevice physicalDevice,
               const VkDeviceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
    PFN_vkCreateDevice f = (PFN_vkCreateDevice)shim_res_physdev("vkCreateDevice");
    if (!f) {
        LOGI("MISSING vkCreateDevice");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult r = f(physicalDevice, pCreateInfo, pAllocator, pDevice);
    LOGI("vkCreateDevice -> %d dev=%p", (int)r,
         (r == VK_SUCCESS && pDevice) ? (void *)*pDevice : NULL);
    if (r == VK_SUCCESS && pDevice && *pDevice) {
        g_dev = *pDevice;
        if (!g_gdpa)
            g_gdpa = (PFN_vkGetDeviceProcAddr)icd_gi(g_inst, "vkGetDeviceProcAddr");
    }
    return r;
}

SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator)
{
    PFN_vkDestroyDevice f =
        (PFN_vkDestroyDevice)shim_res_device(device, "vkDestroyDevice");
    LOGI("vkDestroyDevice %p", (void *)device);
    if (device == g_dev)
        g_dev = NULL;
    if (f)
        f(device, pAllocator);
}

SHIM_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName)
{
    if (!pName)
        return NULL;
    shim_load_icd();
    void *t = shim_table_lookup(pName);
    if (t) {
        LOGI("GIPA(%p,'%s') -> shim %p", (void *)instance, pName, t);
        return (PFN_vkVoidFunction)t;
    }
    void *p = icd_gi(instance ? instance : g_inst, pName);
    if (!p)
        p = icd_gi(NULL, pName);
    LOGI("GIPA(%p,'%s') -> icd %p", (void *)instance, pName, p);
    return (PFN_vkVoidFunction)p;
}

SHIM_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char *pName)
{
    if (!pName)
        return NULL;
    shim_load_icd();
    void *t = shim_table_lookup(pName);
    if (t)
        return (PFN_vkVoidFunction)t;
    void *p = NULL;
    if (g_gdpa && device)
        p = (void *)g_gdpa(device, pName);
    if (!p)
        p = icd_gi(g_inst, pName);
    LOGI("GDPA(%p,'%s') -> icd %p", (void *)device, pName, p);
    return (PFN_vkVoidFunction)p;
}

/* 满足 --no-undefined 自检，同时给出垫片版本标记 */
SHIM_EXPORT VKAPI_ATTR const char *VKAPI_CALL
vkpanvk_shim_version(void);
SHIM_EXPORT VKAPI_ATTR const char *VKAPI_CALL
vkpanvk_shim_version(void)
{
    return "vkpanvk-shim/0.1 " SHIM_BUILD " (G720 PanVK forwarder)";
}

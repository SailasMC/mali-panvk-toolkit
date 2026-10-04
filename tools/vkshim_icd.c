/*
 * libvulkan.so —— 转发垫片（Android / NDK）
 *
 * 目的：让只认「系统 Vulkan 加载器」的调用方（MC 的 LWJGL Vulkan 绑定、Mesa/Zink）
 *       改用我们的开源驱动（Mesa PanVK / KRAID，ICD 形态）。
 *
 * 原理：
 *   FCL 会把「原生库插件」的 lib 目录拼进 LD_LIBRARY_PATH，且位置在 /system/lib64 之前。
 *   我们把本文件打包成 libvulkan.so 放进插件，任何 dlopen("libvulkan.so") 就会命中我们，
 *   于是原本会拿到系统加载器（进而加载原厂 Mali HAL）的调用方，改由我们接管。
 *   我们随后 dlopen 真正的 ICD，把全部 Vulkan 入口转发过去。
 *
 * ICD 从哪来（按优先级）：
 *   1) 环境变量 DRIVER_PATH —— FCL 已选中的「驱动插件」的 lib 目录（**推荐路径**，
 *      这样切换 FCL 里的驱动 = 切换本垫片加载的驱动，且无需在本插件里重复打包 30MB）
 *   2) 与本垫片同目录
 *   3) LD_LIBRARY_PATH 里按名字找
 *
 * 为什么能这么薄：ICD 的句柄语义与加载器一致 —— VkInstance/VkPhysicalDevice/VkDevice
 *   都是 ICD 自己产生的指针，转发时不做任何翻译，原样透传即可。
 *
 * 调试：关键步骤写 stderr（FCL 会把游戏进程 stderr 收进 latest_game.log）
 */

#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

typedef PFN_vkVoidFunction (*PFN_icd_gipa)(VkInstance, const char *);
typedef VkResult (*PFN_icd_negotiate)(uint32_t *);

static PFN_icd_gipa g_gipa = NULL;
static void        *g_icd  = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

#define LOG(...) do { fprintf(stderr, "[vkshim] " __VA_ARGS__); fprintf(stderr, "\n"); } while (0)

/* 本 .so 所在目录 */
static int sibling_path(const char *name, char *out, size_t cap)
{
    Dl_info info;
    if (!dladdr((void *)(uintptr_t)&sibling_path, &info) || !info.dli_fname)
        return 0;
    const char *slash = strrchr(info.dli_fname, '/');
    if (!slash)
        return 0;
    size_t dir = (size_t)(slash - info.dli_fname) + 1;
    if (dir + strlen(name) + 1 > cap)
        return 0;
    memcpy(out, info.dli_fname, dir);
    strcpy(out + dir, name);
    return 1;
}

static void *try_path(const char *path, const char *why)
{
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (h)
        LOG("ICD 已加载(%s): %s", why, path);
    return h;
}

static void ensure_icd(void)
{
    if (g_gipa)
        return;
    pthread_mutex_lock(&g_lock);
    if (!g_gipa) {
        static const char *NAMES[] = { "libvulkan_panfrost.so",
                                       "libvulkan_freedreno.so", NULL };
        char path[4096];
        const char *drv = getenv("DRIVER_PATH");

        /* 1) FCL 选中的驱动插件目录 */
        if (drv && *drv) {
            LOG("DRIVER_PATH=%s", drv);
            for (int i = 0; NAMES[i] && !g_icd; i++) {
                snprintf(path, sizeof path, "%s/%s", drv, NAMES[i]);
                g_icd = try_path(path, "DRIVER_PATH");
            }
        } else {
            LOG("未设置 DRIVER_PATH");
        }

        /* 2) 与本垫片同目录 */
        for (int i = 0; NAMES[i] && !g_icd; i++) {
            if (sibling_path(NAMES[i], path, sizeof path))
                g_icd = try_path(path, "同目录");
        }

        /* 3) 交给 LD_LIBRARY_PATH */
        for (int i = 0; NAMES[i] && !g_icd; i++)
            g_icd = try_path(NAMES[i], "LD_LIBRARY_PATH");

        if (!g_icd) {
            LOG("致命: 找不到 ICD (%s)", dlerror());
            pthread_mutex_unlock(&g_lock);
            return;
        }

        PFN_icd_negotiate neg =
            (PFN_icd_negotiate)dlsym(g_icd, "vk_icdNegotiateLoaderICDInterfaceVersion");
        if (neg) {
            uint32_t v = 5;
            VkResult r = neg(&v);
            LOG("ICD 接口协商: 请求 5 → 返回 %u (VkResult=%d)", v, r);
        } else {
            LOG("ICD 无协商入口（老式 ICD）");
        }
        g_gipa = (PFN_icd_gipa)dlsym(g_icd, "vk_icdGetInstanceProcAddr");
        if (!g_gipa)
            g_gipa = (PFN_icd_gipa)dlsym(g_icd, "vk_icdGetInstanceProcAddrLunar");
        if (!g_gipa)
            LOG("致命: ICD 没有 vk_icdGetInstanceProcAddr");
        else
            LOG("垫片就绪：后续 Vulkan 调用全部转发给开源驱动");
    }
    pthread_mutex_unlock(&g_lock);
}

/* ============================ 加载器 API 导出 ============================ */

PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *pName)
{
    ensure_icd();
    if (!g_gipa || !pName)
        return NULL;
    if (!strcmp(pName, "vkGetInstanceProcAddr"))
        return (PFN_vkVoidFunction)(uintptr_t)&vkGetInstanceProcAddr;
    if (!strcmp(pName, "vkGetDeviceProcAddr"))
        return (PFN_vkVoidFunction)(uintptr_t)&vkGetDeviceProcAddr;
    return g_gipa(instance, pName);
}

PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device, const char *pName)
{
    ensure_icd();
    if (!g_gipa || !pName)
        return NULL;
    static PFN_vkVoidFunction (*gdpa)(VkDevice, const char *);
    if (!gdpa)
        gdpa = (PFN_vkVoidFunction(*)(VkDevice, const char *))g_gipa(NULL, "vkGetDeviceProcAddr");
    return gdpa ? gdpa(device, pName) : NULL;
}

VkResult vkEnumerateInstanceVersion(uint32_t *pApiVersion)
{
    ensure_icd();
    PFN_vkEnumerateInstanceVersion f =
        g_gipa ? (PFN_vkEnumerateInstanceVersion)g_gipa(NULL, "vkEnumerateInstanceVersion") : NULL;
    if (f)
        return f(pApiVersion);
    if (pApiVersion)
        *pApiVersion = VK_API_VERSION_1_0;
    return VK_SUCCESS;
}

VkResult vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
{
    ensure_icd();
    PFN_vkCreateInstance f = g_gipa ? (PFN_vkCreateInstance)g_gipa(NULL, "vkCreateInstance") : NULL;
    if (!f) {
        LOG("vkCreateInstance: 取不到函数指针");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    LOG("vkCreateInstance: enabledExt=%u enabledLayers=%u",
        pCreateInfo->enabledExtensionCount, pCreateInfo->enabledLayerCount);
    VkResult r = f(pCreateInfo, pAllocator, pInstance);
    LOG("vkCreateInstance → %d", (int)r);
    return r;
}

VkResult vkEnumerateInstanceExtensionProperties(const char *pLayerName, uint32_t *pCount,
                                                VkExtensionProperties *pProps)
{
    ensure_icd();
    PFN_vkEnumerateInstanceExtensionProperties f =
        g_gipa ? (PFN_vkEnumerateInstanceExtensionProperties)
                     g_gipa(NULL, "vkEnumerateInstanceExtensionProperties")
               : NULL;
    if (!f) {
        if (pCount)
            *pCount = 0;
        return VK_SUCCESS;
    }
    return f(pLayerName, pCount, pProps);
}

/* 我们不是加载器：如实报告"没有层"，避免调用方以为层可用 */
VkResult vkEnumerateInstanceLayerProperties(uint32_t *pCount, VkLayerProperties *pProps)
{
    if (pCount)
        *pCount = 0;
    (void)pProps;
    return VK_SUCCESS;
}

VkResult vkEnumerateDeviceExtensionProperties(VkPhysicalDevice dev, const char *pLayerName,
                                              uint32_t *pCount, VkExtensionProperties *pProps)
{
    ensure_icd();
    PFN_vkEnumerateDeviceExtensionProperties f =
        g_gipa ? (PFN_vkEnumerateDeviceExtensionProperties)
                     g_gipa(NULL, "vkEnumerateDeviceExtensionProperties")
               : NULL;
    if (!f) {
        if (pCount)
            *pCount = 0;
        return VK_SUCCESS;
    }
    return f(dev, pLayerName, pCount, pProps);
}

VkResult vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pCount,
                                    VkPhysicalDevice *pDevices)
{
    ensure_icd();
    PFN_vkEnumeratePhysicalDevices f =
        g_gipa ? (PFN_vkEnumeratePhysicalDevices)g_gipa(instance, "vkEnumeratePhysicalDevices")
               : NULL;
    if (!f)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult r = f(instance, pCount, pDevices);
    LOG("vkEnumeratePhysicalDevices → %d, count=%u", (int)r, pCount ? *pCount : 0);
    return r;
}

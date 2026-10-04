/*
 * vkicd_probe —— Vulkan ICD 冒烟测试器（免 root / Android arm64）
 *
 * 用途：拿到任何一个 Mali/Adreno 开源驱动 .so 后，**几秒钟判断它能不能在这台机器上跑起来**，
 *       完全不需要先折腾启动器（FCL/ZL2）。
 *
 * 做的事：模拟 Vulkan loader 的加载流程
 *   1. dlopen(.so)                        —— 能不能加载（依赖库是否齐全）
 *   2. dlsym(vk_icdNegotiateLoaderICDInterfaceVersion) 并协商接口版本
 *   3. dlsym(vk_icdGetInstanceProcAddr)   —— 判断是 ICD 形态还是 HAL 形态
 *   4. vkCreateInstance                    —— 驱动能否初始化（这里会真正打开 GPU）
 *   5. vkEnumeratePhysicalDevices          —— 能否看见 GPU
 *   6. vkGetPhysicalDeviceProperties       —— 打印 GPU 名 / Vulkan 版本 / 驱动版本
 *   7. 枚举设备扩展                        —— 看 Zink 需要的扩展在不在
 *
 * 编译（服务器上，NDK r27c）：
 *   $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang \
 *       -O2 -o vkicd_probe vkicd_probe.c -ldl
 *
 * 运行（手机上，把 so 和这个二进制放在可执行目录）：
 *   ./vkicd_probe /data/local/tmp/libvulkan_panfrost.so
 *
 * 退出码：0 = 全通过；1..7 = 第几步失败
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo *, const VkAllocationCallbacks *, VkInstance *);
typedef VkResult (*PFN_vkEnumerateInstanceExtensionProperties)(const char *, uint32_t *, VkExtensionProperties *);
typedef VkResult (*PFN_vkEnumeratePhysicalDevices)(VkInstance, uint32_t *, VkPhysicalDevice *);
typedef void     (*PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice, VkPhysicalDeviceProperties *);
typedef VkResult (*PFN_vkEnumerateDeviceExtensionProperties)(VkPhysicalDevice, const char *, uint32_t *, VkExtensionProperties *);
typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(VkInstance, const char *);
typedef VkResult (*PFN_vk_icdNegotiateLoaderICDInterfaceVersion)(uint32_t *);

static const char *g_so = NULL;

static void *sym(void *h, const char *name)
{
    void *p = dlsym(h, name);
    printf("    %-48s %s\n", name, p ? "有" : "无");
    return p;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: %s <驱动.so 路径>\n", argv[0]);
        return 64;
    }
    g_so = argv[1];
    printf("==================================================================\n");
    printf(" Vulkan ICD 冒烟测试   目标: %s\n", g_so);
    printf("==================================================================\n");

    /* ---- 1. dlopen ---- */
    printf("\n[1] dlopen(驱动)\n");
    void *h = dlopen(g_so, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        printf("    ✘ 加载失败: %s\n", dlerror());
        printf("    ⇒ 依赖库缺失，或不是 arm64 Android 的 .so\n");
        return 1;
    }
    printf("    ✔ 加载成功 (handle=%p)\n", h);

    /* ---- 2. 入口符号 ---- */
    printf("\n[2] 探入口符号（判断 ICD 形态还是 HAL 形态）\n");
    PFN_vk_icdNegotiateLoaderICDInterfaceVersion neg = (PFN_vk_icdNegotiateLoaderICDInterfaceVersion)
        sym(h, "vk_icdNegotiateLoaderICDInterfaceVersion");
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)sym(h, "vk_icdGetInstanceProcAddr");
    if (!gipa)
        gipa = (PFN_vkGetInstanceProcAddr)sym(h, "vk_icdGetInstanceProcAddrLunar");
    void *hmi = sym(h, "HMI");
    void *hal = sym(h, "halGetInstance");

    if (!gipa) {
        printf("\n    ✘ 没有 vk_icdGetInstanceProcAddr ⇒ **不是可加载 ICD**\n");
        if (hmi || hal)
            printf("    ⇒ 检测到 Android Vulkan HAL 入口：这是「替换系统驱动」形态，需要 root，启动器加载不了\n");
        else
            printf("    ⇒ 既不是 ICD 也不是标准 HAL，可能不是 Vulkan 驱动\n");
        return 2;
    }
    printf("    ✔ 是 ICD 形态，可以走 dlopen 加载\n");

    if (neg) {
        uint32_t v = 5;
        VkResult r = neg(&v);
        printf("\n[3] 协商 ICD loader 接口版本: 请求 5 → 返回 %u  (%s)\n", v, r == VK_SUCCESS ? "成功" : "非成功");
    } else {
        printf("\n[3] 无 vk_icdNegotiateLoaderICDInterfaceVersion（老式 ICD，可直接用）\n");
    }

    /* ---- 4. vkCreateInstance ---- */
    printf("\n[4] vkCreateInstance（这一步会真正初始化 GPU）\n");
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    if (!create) { printf("    ✘ 取不到 vkCreateInstance\n"); return 4; }

    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "vkicd_probe";
    app.applicationVersion = 1;
    app.pEngineName = "none";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    VkInstance inst = VK_NULL_HANDLE;
    VkResult res = create(&ci, NULL, &inst);
    if (res != VK_SUCCESS) {
        printf("    ✘ vkCreateInstance 失败: VkResult=%d\n", res);
        printf("      -1=ERROR_OUT_OF_HOST_MEMORY  -3=ERROR_INITIALIZATION_FAILED\n");
        printf("      -9=ERROR_INCOMPATIBLE_DRIVER\n");
        printf("    ⇒ 驱动能加载但初始化不了：多半是 kbase/GPU 通路或 uAPI 不匹配\n");
        return 4;
    }
    printf("    ✔ 实例创建成功\n");

    /* ---- 5. 枚举物理设备 ---- */
    printf("\n[5] vkEnumeratePhysicalDevices\n");
    PFN_vkEnumeratePhysicalDevices enum_dev = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties get_props = (PFN_vkGetPhysicalDeviceProperties)gipa(inst, "vkGetPhysicalDeviceProperties");
    PFN_vkEnumerateDeviceExtensionProperties enum_ext = (PFN_vkEnumerateDeviceExtensionProperties)gipa(inst, "vkEnumerateDeviceExtensionProperties");
    if (!enum_dev || !get_props) { printf("    ✘ 取不到必要函数\n"); return 5; }

    uint32_t n = 0;
    res = enum_dev(inst, &n, NULL);
    if (res != VK_SUCCESS || n == 0) {
        printf("    ✘ 没找到物理设备 (VkResult=%d, count=%u)\n", res, n);
        printf("    ⇒ 驱动起来了但看不见 GPU\n");
        return 5;
    }
    VkPhysicalDevice devs[8] = {0};
    if (n > 8) n = 8;
    res = enum_dev(inst, &n, devs);
    printf("    ✔ 发现 %u 个物理设备\n", n);

    /* ---- 6. 设备属性 ---- */
    printf("\n[6] 设备属性\n");
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        memset(&p, 0, sizeof(p));
        get_props(devs[i], &p);
        printf("    ── 设备 #%u ──\n", i);
        printf("       deviceName    : %s\n", p.deviceName);
        printf("       apiVersion    : %u.%u.%u\n",
               VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion));
        printf("       driverVersion : %u.%u.%u.%u\n",
               (p.driverVersion >> 22) & 0x3ff, (p.driverVersion >> 12) & 0x3ff,
               (p.driverVersion >> 2) & 0x3ff, p.driverVersion & 0x3);
        printf("       deviceType    : %d (2=独显 3=集显 4=虚拟 1=CPU)\n", p.deviceType);
        printf("       maxComputeWG  : %u x %u x %u\n",
               p.limits.maxComputeWorkGroupCount[0], p.limits.maxComputeWorkGroupCount[1],
               p.limits.maxComputeWorkGroupCount[2]);
    }

    /* driver properties（VK_KHR_driver_properties / Vulkan 1.2 核心） */
    {
        PFN_vkGetPhysicalDeviceProperties2 gp2 =
            (PFN_vkGetPhysicalDeviceProperties2)gipa(inst, "vkGetPhysicalDeviceProperties2");
        if (gp2) {
            VkPhysicalDeviceDriverProperties dp;
            memset(&dp, 0, sizeof(dp));
            dp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
            VkPhysicalDeviceProperties2 p2;
            memset(&p2, 0, sizeof(p2));
            p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            p2.pNext = &dp;
            gp2(devs[0], &p2);
            printf("    ── 驱动身份 ──\n");
            printf("       driverName    : %s (driverID=%d)\n", dp.driverName, (int)dp.driverID);
            printf("       conformance   : %u.%u.%u\n",
                   dp.conformanceVersion.major, dp.conformanceVersion.minor,
                   dp.conformanceVersion.subminor);
        } else {
            printf("    (无 vkGetPhysicalDeviceProperties2，跳过驱动身份查询)\n");
        }
    }

    /* ---- 7. 关键扩展（Zink / 现代渲染需要） ---- */
    printf("\n[7] 关键设备扩展（Zink 与 VulkanMod 常用）\n");
    if (!enum_ext) {
        printf("    (取不到 vkEnumerateDeviceExtensionProperties)\n");
    } else {
        static const char *want[] = {
            "VK_KHR_timeline_semaphore", "VK_KHR_synchronization2", "VK_KHR_dynamic_rendering",
            "VK_EXT_descriptor_indexing", "VK_KHR_buffer_device_address", "VK_EXT_robustness2",
            "VK_KHR_imageless_framebuffer", "VK_EXT_extended_dynamic_state",
            "VK_KHR_external_memory_fd", "VK_ANDROID_external_memory_android_hardware_buffer",
            "VK_KHR_swapchain", "VK_KHR_maintenance1", "VK_EXT_custom_border_color",
        };
        uint32_t cnt = 0;
        enum_ext(devs[0], NULL, &cnt, NULL);
        VkExtensionProperties *all = calloc(cnt ? cnt : 1, sizeof(*all));
        if (all) {
            enum_ext(devs[0], NULL, &cnt, all);
            printf("    共 %u 个扩展；关注的：\n", cnt);
            for (size_t w = 0; w < sizeof(want) / sizeof(want[0]); w++) {
                int found = 0;
                for (uint32_t k = 0; k < cnt; k++)
                    if (!strcmp(all[k].extensionName, want[w])) { found = 1; break; }
                printf("      %-54s %s\n", want[w], found ? "✔" : "✘");
            }
            free(all);
        }
    }

    printf("\n==================================================================\n");
    printf(" 结论：该驱动能在这台设备上完成 加载 → 初始化 → 看见 GPU\n");
    printf("       下一步才是把它打包成 FCL/ZL2 驱动插件 APK\n");
    printf("==================================================================\n");
    return 0;
}

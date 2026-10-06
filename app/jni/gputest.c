// GPU 驱动测试台 · 原生模块 v2
//   nativeIcdSmoke(so)              —— ICD 冒烟：dlopen → 协商 → 建实例 → 枚举设备 → 建设备/队列
//   nativeTri(so)                   —— 真绘制：建渲染目标 + 图形管线，画三角形，回读像素并校验（应为纯红）
//   nativeBench(so, seconds, mode)  —— 跑分：mode = fill | blit | draw
// 不依赖系统 Vulkan loader：直接调 ICD 入口。
#include <jni.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <vulkan/vulkan.h>
#include "shaders.h"

#define LOG(...) do { int _n = snprintf(g_log + g_len, sizeof(g_log) - g_len, __VA_ARGS__); \
                      if (_n > 0) g_len += (_n < (int)sizeof(g_log) - g_len ? _n : (int)sizeof(g_log) - g_len - 1); } while (0)

static char g_log[128 * 1024];
static int  g_len;

typedef VkResult (*PFN_neg)(uint32_t *);
typedef PFN_vkVoidFunction (*PFN_gipa)(VkInstance, const char *);
typedef PFN_vkVoidFunction (*PFN_gdpa)(VkDevice, const char *);

static double now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }

// ---------------------------------------------------------------- 上下文
typedef struct {
    void *h; PFN_gipa gipa;
    VkInstance inst; VkPhysicalDevice pd; VkDevice dev; VkQueue q; uint32_t qfam;
    VkPhysicalDeviceProperties props;
    PFN_vkGetDeviceProcAddr gdpa;
} Ctx;

static PFN_gipa icd_open(const char *path, void **handle) {
    LOG("[1] dlopen(%s)\n", path);
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { LOG("    X %s\n", dlerror()); return NULL; }
    PFN_neg neg = (PFN_neg) dlsym(h, "vk_icdNegotiateLoaderICDInterfaceVersion");
    PFN_gipa gipa = (PFN_gipa) dlsym(h, "vk_icdGetInstanceProcAddr");
    if (!gipa) gipa = (PFN_gipa) dlsym(h, "vk_icdGetInstanceProcAddrLunar");
    LOG("[2] negotiate=%s gipa=%s\n", neg ? "有" : "无", gipa ? "有" : "无");
    if (!gipa) { LOG("    X 不是可加载 ICD\n"); dlclose(h); return NULL; }
    if (neg) { uint32_t v = 7; LOG("    协商版本 -> %u (VkResult %d)\n", v, neg(&v)); }
    *handle = h; return gipa;
}

static int ctx_open(Ctx *c, const char *path) {
    memset(c, 0, sizeof(*c));
    c->gipa = icd_open(path, &c->h);
    if (!c->gipa) return 0;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance) c->gipa(NULL, "vkCreateInstance");
    PFN_vkEnumeratePhysicalDevices enumPd = (PFN_vkEnumeratePhysicalDevices) c->gipa(NULL, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties getProps = (PFN_vkGetPhysicalDeviceProperties) c->gipa(NULL, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getQf = (PFN_vkGetPhysicalDeviceQueueFamilyProperties) c->gipa(NULL, "vkGetPhysicalDeviceQueueFamilyProperties");
    PFN_vkCreateDevice createDev = (PFN_vkCreateDevice) c->gipa(NULL, "vkCreateDevice");
    PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr) c->gipa(NULL, "vkGetDeviceProcAddr");
    if (!create || !enumPd || !getProps || !getQf || !createDev || !gdpa) { LOG("X 关键入口缺失\n"); return 0; }

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "GPUTest", .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkResult r = create(&ici, NULL, &c->inst);
    LOG("[3] vkCreateInstance -> %d %s\n", r, r == VK_SUCCESS ? "OK" : "失败");
    if (r != VK_SUCCESS) return 0;

    uint32_t n = 0; enumPd(c->inst, &n, NULL);
    LOG("[4] 物理设备数: %u\n", n);
    if (!n) return 0;
    VkPhysicalDevice pds[8]; if (n > 8) n = 8;
    enumPd(c->inst, &n, pds);
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p; getProps(pds[i], &p);
        LOG("    设备%u: %s | 类型=%d | api=%u.%u.%u | 驱动=0x%08x (%u.%u.%u) | vendor=0x%04x device=0x%04x\n",
            i, p.deviceName, p.deviceType,
            VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion),
            p.driverVersion, VK_VERSION_MAJOR(p.driverVersion), VK_VERSION_MINOR(p.driverVersion), VK_VERSION_PATCH(p.driverVersion),
            p.vendorID, p.deviceID);
    }
    c->pd = pds[0]; getProps(c->pd, &c->props);

    uint32_t nq = 0; getQf(c->pd, &nq, NULL);
    VkQueueFamilyProperties qfs[16]; if (nq > 16) nq = 16;
    getQf(c->pd, &nq, qfs);
    c->qfam = 0; int found = 0;
    for (uint32_t i = 0; i < nq; i++)
        if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { c->qfam = i; found = 1; break; }
    LOG("[5] 队列族 %u 个；选用 #%u (图形=%s)\n", nq, c->qfam, found ? "是" : "否(退化)");

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = c->qfam, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
    r = createDev(c->pd, &dci, NULL, &c->dev);
    LOG("[6] vkCreateDevice -> %d %s\n", r, r == VK_SUCCESS ? "OK" : "失败");
    if (r != VK_SUCCESS) return 0;
    c->gdpa = gdpa;
    PFN_vkGetDeviceQueue getQ = (PFN_vkGetDeviceQueue) gdpa(c->dev, "vkGetDeviceQueue");
    if (!getQ) return 0;
    getQ(c->dev, c->qfam, 0, &c->q);
    LOG("[7] 设备与队列就绪\n");
    return 1;
}
static void ctx_close(Ctx *c) { if (c->h) dlclose(c->h); }

// ------------------------------------------------------- 渲染目标/资源
typedef struct {
    VkImage img; VkDeviceMemory mem; VkImageView view; VkRenderPass rp; VkFramebuffer fb;
    VkCommandPool pool; VkCommandBuffer cb; VkFence fence; VkBuffer buf; VkDeviceMemory bmem;
    uint32_t W, H;
} Target;

static uint32_t pick_mem(Ctx *c, uint32_t bits, VkMemoryPropertyFlags want) {
    PFN_vkGetPhysicalDeviceMemoryProperties gm = (PFN_vkGetPhysicalDeviceMemoryProperties)
        c->gipa(NULL, "vkGetPhysicalDeviceMemoryProperties");
    if (!gm) return 0;
    VkPhysicalDeviceMemoryProperties mp; gm(c->pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) if (bits & (1u << i)) return i;
    return 0;
}

static int target_make(Ctx *c, Target *t, uint32_t W, uint32_t H, int with_rp) {
    memset(t, 0, sizeof(*t)); t->W = W; t->H = H;
    PFN_vkCreateImage mk = (PFN_vkCreateImage) c->gdpa(c->dev, "vkCreateImage");
    PFN_vkGetImageMemoryRequirements req = (PFN_vkGetImageMemoryRequirements) c->gdpa(c->dev, "vkGetImageMemoryRequirements");
    PFN_vkAllocateMemory alloc = (PFN_vkAllocateMemory) c->gdpa(c->dev, "vkAllocateMemory");
    PFN_vkBindImageMemory bind = (PFN_vkBindImageMemory) c->gdpa(c->dev, "vkBindImageMemory");
    PFN_vkCreateImageView mkview = (PFN_vkCreateImageView) c->gdpa(c->dev, "vkCreateImageView");
    if (!mk || !req || !alloc || !bind || !mkview) return 0;

    VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    if (mk(c->dev, &ici, NULL, &t->img) != VK_SUCCESS) { LOG("X 建图像失败\n"); return 0; }
    VkMemoryRequirements mr; req(c->dev, t->img, &mr);
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size, .memoryTypeIndex = pick_mem(c, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    if (alloc(c->dev, &mai, NULL, &t->mem) != VK_SUCCESS) { LOG("X 分配图像内存失败\n"); return 0; }
    bind(c->dev, t->img, t->mem, 0);

    VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = t->img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    if (mkview(c->dev, &vci, NULL, &t->view) != VK_SUCCESS) { LOG("X 建视图失败\n"); return 0; }

    if (with_rp) {
        PFN_vkCreateRenderPass mkrp = (PFN_vkCreateRenderPass) c->gdpa(c->dev, "vkCreateRenderPass");
        PFN_vkCreateFramebuffer mkfb = (PFN_vkCreateFramebuffer) c->gdpa(c->dev, "vkCreateFramebuffer");
        if (!mkrp || !mkfb) return 0;
        VkAttachmentDescription att = { .format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
        VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sub = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1, .pColorAttachments = &ref };
        VkRenderPassCreateInfo rci = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1, .pAttachments = &att, .subpassCount = 1, .pSubpasses = &sub };
        if (mkrp(c->dev, &rci, NULL, &t->rp) != VK_SUCCESS) { LOG("X 建 render pass 失败\n"); return 0; }
        VkFramebufferCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = t->rp, .attachmentCount = 1, .pAttachments = &t->view, .width = W, .height = H, .layers = 1 };
        if (mkfb(c->dev, &fci, NULL, &t->fb) != VK_SUCCESS) { LOG("X 建 framebuffer 失败\n"); return 0; }
    }

    PFN_vkCreateCommandPool mkpool = (PFN_vkCreateCommandPool) c->gdpa(c->dev, "vkCreateCommandPool");
    PFN_vkAllocateCommandBuffers alloccb = (PFN_vkAllocateCommandBuffers) c->gdpa(c->dev, "vkAllocateCommandBuffers");
    PFN_vkCreateFence mkf = (PFN_vkCreateFence) c->gdpa(c->dev, "vkCreateFence");
    VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = c->qfam };
    if (!mkpool || mkpool(c->dev, &pci, NULL, &t->pool) != VK_SUCCESS) { LOG("X 建命令池失败\n"); return 0; }
    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = t->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    alloccb(c->dev, &cbai, &t->cb);
    VkFenceCreateInfo fci2 = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    mkf(c->dev, &fci2, NULL, &t->fence);
    return t->cb && t->fence;
}

static VkResult submit_wait(Ctx *c, Target *t) {
    PFN_vkQueueSubmit sub = (PFN_vkQueueSubmit) c->gdpa(c->dev, "vkQueueSubmit");
    PFN_vkWaitForFences wf = (PFN_vkWaitForFences) c->gdpa(c->dev, "vkWaitForFences");
    PFN_vkResetFences rf = (PFN_vkResetFences) c->gdpa(c->dev, "vkResetFences");
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &t->cb };
    VkResult r = sub(c->q, 1, &si, t->fence);
    if (r != VK_SUCCESS) return r;
    r = wf(c->dev, 1, &t->fence, VK_TRUE, 3000000000ull);
    rf(c->dev, 1, &t->fence);
    return r;
}

// 把图像拷回主机缓冲并返回指针
static int readback(Ctx *c, Target *t, void **out) {
    PFN_vkCreateBuffer mkb = (PFN_vkCreateBuffer) c->gdpa(c->dev, "vkCreateBuffer");
    PFN_vkGetBufferMemoryRequirements breq = (PFN_vkGetBufferMemoryRequirements) c->gdpa(c->dev, "vkGetBufferMemoryRequirements");
    PFN_vkAllocateMemory alloc = (PFN_vkAllocateMemory) c->gdpa(c->dev, "vkAllocateMemory");
    PFN_vkBindBufferMemory bindb = (PFN_vkBindBufferMemory) c->gdpa(c->dev, "vkBindBufferMemory");
    PFN_vkMapMemory map = (PFN_vkMapMemory) c->gdpa(c->dev, "vkMapMemory");
    PFN_vkBeginCommandBuffer begin = (PFN_vkBeginCommandBuffer) c->gdpa(c->dev, "vkBeginCommandBuffer");
    PFN_vkEndCommandBuffer end = (PFN_vkEndCommandBuffer) c->gdpa(c->dev, "vkEndCommandBuffer");
    PFN_vkCmdCopyImageToBuffer cp = (PFN_vkCmdCopyImageToBuffer) c->gdpa(c->dev, "vkCmdCopyImageToBuffer");
    if (!mkb || !breq || !alloc || !bindb || !map || !begin || !end || !cp) return 0;

    VkDeviceSize sz = (VkDeviceSize) t->W * t->H * 4;
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sz,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    if (mkb(c->dev, &bci, NULL, &t->buf) != VK_SUCCESS) return 0;
    VkMemoryRequirements mr; breq(c->dev, t->buf, &mr);
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
        .memoryTypeIndex = pick_mem(c, mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    if (alloc(c->dev, &mai, NULL, &t->bmem) != VK_SUCCESS) return 0;
    bindb(c->dev, t->buf, t->bmem, 0);

    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin(t->cb, &bi);
    VkBufferImageCopy region = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { t->W, t->H, 1 } };
    cp(t->cb, t->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->buf, 1, &region);
    end(t->cb);
    if (submit_wait(c, t) != VK_SUCCESS) { LOG("X 回读提交失败\n"); return 0; }
    if (map(c->dev, t->bmem, 0, VK_WHOLE_SIZE, 0, out) != VK_SUCCESS) { LOG("X 映射回读缓冲失败\n"); return 0; }
    return 1;
}

static jstring finish(JNIEnv *env, const char *path, jstring jso) { (void)path; (void)jso; return (*env)->NewStringUTF(env, g_log); }

// ---------------------------------------------------------------- 冒烟
JNIEXPORT jstring JNICALL
Java_com_dsh_gputest_MainActivity_nativeIcdSmoke(JNIEnv *env, jobject th, jstring jso) {
    (void) th; g_len = 0; g_log[0] = 0;
    const char *path = (*env)->GetStringUTFChars(env, jso, NULL);
    LOG("=== ICD 冒烟测试 ===\n驱动: %s\n", path);
    Ctx c;
    if (ctx_open(&c, path)) LOG("\n== 判定: 驱动可用 ==\n");
    else                LOG("\n== 判定: 驱动不可用（见上） ==\n");
    ctx_close(&c);
    (*env)->ReleaseStringUTFChars(env, jso, path);
    return finish(env, path, jso);
}

// ------------------------------------------------------------ 三角形 + 校验
JNIEXPORT jstring JNICALL
Java_com_dsh_gputest_MainActivity_nativeTri(JNIEnv *env, jobject th, jstring jso) {
    (void) th; g_len = 0; g_log[0] = 0;
    const char *path = (*env)->GetStringUTFChars(env, jso, NULL);
    LOG("=== 三角形绘制 + 像素校验 ===\n驱动: %s\n", path);
    Ctx c;
    if (!ctx_open(&c, path)) { LOG("X 驱动不可用\n"); goto out; }
    {
        Target t;
        if (!target_make(&c, &t, 256, 256, 1)) { LOG("X 渲染目标创建失败\n"); goto out2; }

        PFN_vkCreateShaderModule mksh = (PFN_vkCreateShaderModule) c.gdpa(c.dev, "vkCreateShaderModule");
        PFN_vkCreatePipelineLayout mkpl = (PFN_vkCreatePipelineLayout) c.gdpa(c.dev, "vkCreatePipelineLayout");
        PFN_vkCreateGraphicsPipelines mkgp = (PFN_vkCreateGraphicsPipelines) c.gdpa(c.dev, "vkCreateGraphicsPipelines");
        PFN_vkBeginCommandBuffer begin = (PFN_vkBeginCommandBuffer) c.gdpa(c.dev, "vkBeginCommandBuffer");
        PFN_vkEndCommandBuffer end = (PFN_vkEndCommandBuffer) c.gdpa(c.dev, "vkEndCommandBuffer");
        PFN_vkCmdBeginRenderPass brp = (PFN_vkCmdBeginRenderPass) c.gdpa(c.dev, "vkCmdBeginRenderPass");
        PFN_vkCmdEndRenderPass erp = (PFN_vkCmdEndRenderPass) c.gdpa(c.dev, "vkCmdEndRenderPass");
        PFN_vkCmdBindPipeline bp = (PFN_vkCmdBindPipeline) c.gdpa(c.dev, "vkCmdBindPipeline");
        PFN_vkCmdDraw draw = (PFN_vkCmdDraw) c.gdpa(c.dev, "vkCmdDraw");
        if (!mksh || !mkpl || !mkgp || !begin || !end || !brp || !erp || !bp || !draw) { LOG("X 绘制入口缺失\n"); goto out2; }

        VkShaderModuleCreateInfo vsci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = tri_vert_spv_len * 4, .pCode = tri_vert_spv };
        VkShaderModuleCreateInfo fsci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = tri_frag_spv_len * 4, .pCode = tri_frag_spv };
        VkShaderModule vs = NULL, fs = NULL;
        VkResult r = mksh(c.dev, &vsci, NULL, &vs);
        LOG("[8] 顶点着色器模块 -> %d\n", r);
        r = mksh(c.dev, &fsci, NULL, &fs);
        LOG("[9] 片元着色器模块 -> %d\n", r);
        if (!vs || !fs) { LOG("X 着色器模块创建失败\n"); goto out2; }

        VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        VkPipelineLayout pl = NULL; mkpl(c.dev, &plci, NULL, &pl);

        VkPipelineShaderStageCreateInfo stages[2] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main" },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main" } };
        VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
        VkViewport vp = { 0, 0, (float) t.W, (float) t.H, 0, 1 };
        VkRect2D sc = { { 0, 0 }, { t.W, t.H } };
        VkPipelineViewportStateCreateInfo vps = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
        VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
        VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
        VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xF };
        VkPipelineColorBlendStateCreateInfo cb2 = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1, .pAttachments = &cba };
        VkGraphicsPipelineCreateInfo gpci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2, .pStages = stages, .pVertexInputState = &vi, .pInputAssemblyState = &ia,
            .pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &ms,
            .pColorBlendState = &cb2, .layout = pl, .renderPass = t.rp, .subpass = 0 };
        VkPipeline pipe = NULL;
        r = mkgp(c.dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe);
        LOG("[10] 图形管线 -> %d %s\n", r, r == VK_SUCCESS ? "OK" : "失败");
        if (r != VK_SUCCESS) { LOG("X 管线创建失败（这本身就是重要结果）\n"); goto out2; }

        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin(t.cb, &bi);
        VkClearValue cv = { .color = { { 0.0f, 0.0f, 0.0f, 1.0f } } };
        VkRenderPassBeginInfo rpbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = t.rp, .framebuffer = t.fb, .renderArea = { { 0, 0 }, { t.W, t.H } },
            .clearValueCount = 1, .pClearValues = &cv };
        brp(t.cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        bp(t.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        draw(t.cb, 3, 1, 0, 0);
        erp(t.cb);
        end(t.cb);
        r = submit_wait(&c, &t);
        LOG("[11] 提交并等待 fence -> %d %s\n", r, r == VK_SUCCESS ? "完成" : "失败/超时");

        if (r == VK_SUCCESS) {
            void *px = NULL;
            if (readback(&c, &t, &px) && px) {
                unsigned char *p = (unsigned char *) px;
                // 三角形覆盖左下大三角区域；抽样若干点判断"有红"与"纯红"
                long red = 0, black = 0, other = 0;
                // 中心偏下一点必然落在三角形内
                int cx = t.W / 2, cy = (int) (t.H * 0.75);
                unsigned char *cpx = p + ((size_t) cy * t.W + cx) * 4;
                LOG("[12] 采样点 (%d,%d) RGBA = %u,%u,%u,%u\n", cx, cy, cpx[0], cpx[1], cpx[2], cpx[3]);
                for (uint32_t y = 0; y < t.H; y += 4)
                    for (uint32_t x = 0; x < t.W; x += 4) {
                        unsigned char *q = p + ((size_t) y * t.W + x) * 4;
                        if (q[0] > 200 && q[1] < 60 && q[2] < 60) red++;
                        else if (q[0] < 40 && q[1] < 40 && q[2] < 40) black++;
                        else other++;
                    }
                LOG("[13] 采样统计: 红=%ld 黑=%ld 其它=%ld\n", red, black, other);
                LOG("\n== 判定: %s ==\n",
                    (red > 0 && other == 0) ? "绘制正确（三角形为纯红，其余为清屏色）"
                                            : (red > 0 ? "绘制有输出但存在异常像素（可能正是渲染 bug）" : "无绘制输出"));
            } else LOG("X 像素回读失败\n");
        }
out2:   ;
    }
out:
    ctx_close(&c);
    (*env)->ReleaseStringUTFChars(env, jso, path);
    return finish(env, path, jso);
}

// ---------------------------------------------------------------- 跑分
JNIEXPORT jstring JNICALL
Java_com_dsh_gputest_MainActivity_nativeBench(JNIEnv *env, jobject th, jstring jso, jint seconds, jstring jmode) {
    (void) th; g_len = 0; g_log[0] = 0;
    const char *path = (*env)->GetStringUTFChars(env, jso, NULL);
    const char *mode = jmode ? (*env)->GetStringUTFChars(env, jmode, NULL) : "fill";
    LOG("=== 跑分 mode=%s 时长=%d s ===\n驱动: %s\n", mode, seconds, path);
    Ctx c;
    if (!ctx_open(&c, path)) { LOG("X 驱动不可用\n"); goto out; }
    {
        Target t;
        if (!target_make(&c, &t, 1024, 1024, 0)) { LOG("X 目标创建失败\n"); goto out2; }
        PFN_vkBeginCommandBuffer begin = (PFN_vkBeginCommandBuffer) c.gdpa(c.dev, "vkBeginCommandBuffer");
        PFN_vkEndCommandBuffer end = (PFN_vkEndCommandBuffer) c.gdpa(c.dev, "vkEndCommandBuffer");
        PFN_vkCmdClearColorImage clr = (PFN_vkCmdClearColorImage) c.gdpa(c.dev, "vkCmdClearColorImage");
        PFN_vkCmdPipelineBarrier bar = (PFN_vkCmdPipelineBarrier) c.gdpa(c.dev, "vkCmdPipelineBarrier");
        PFN_vkCmdCopyImageToBuffer cpb = (PFN_vkCmdCopyImageToBuffer) c.gdpa(c.dev, "vkCmdCopyImageToBuffer");
        if (!begin || !end || !clr || !bar || !cpb) { LOG("X 跑分入口缺失\n"); goto out2; }
        VkImageSubresourceRange rg = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageMemoryBarrier b0 = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = t.img, .subresourceRange = rg, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT };
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin(t.cb, &bi);
        bar(t.cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b0);
        end(t.cb);
        if (submit_wait(&c, &t) != VK_SUCCESS) { LOG("X 预热失败\n"); goto out2; }
        VkClearColorValue col = { .float32 = { 0.1f, 0.2f, 0.3f, 1.0f } };

        const int per = 8;
        double t_start = now_ms(), deadline = t_start + (seconds > 0 ? seconds * 1000.0 : 3000.0);
        unsigned long long ops = 0, subs = 0, fails = 0; double sum_us = 0;
        while (now_ms() < deadline) {
            VkCommandBufferBeginInfo b = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            if (begin(t.cb, &b) != VK_SUCCESS) { fails++; break; }
            for (int i = 0; i < per; i++) clr(t.cb, t.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &col, 1, &rg);
            end(t.cb);
            double s0 = now_ms(); VkResult r = submit_wait(&c, &t); double s1 = now_ms();
            if (r != VK_SUCCESS) { fails++; LOG("X 第 %llu 次提交失败 r=%d\n", subs, r); break; }
            sum_us += (s1 - s0) * 1000.0; subs++; ops += per;
        }
        double dt = (now_ms() - t_start) / 1000.0;
        double mpix = (double) ops * 1024.0 * 1024.0 / 1e6;
        LOG("\n=== 跑分结果（mode=%s）===\n", mode);
        LOG("时长=%.2fs  提交=%llu  操作=%llu  失败=%llu\n", dt, subs, ops, fails);
        if (dt > 0) {
            LOG("填充率=%.1f Mpixel/s\n", mpix / dt);
            LOG("提交吞吐=%.1f 次/s   提交往返=%.1f µs\n", subs / dt, subs ? sum_us / subs : 0.0);
            LOG("\n== 分数: %.0f ==\n", fails ? 0.0 : mpix / dt);
        }
        LOG("（提示：不稳定时不给分；mode=draw 需要管线，见三角形自测）\n");
out2:   ;
    }
out:
    ctx_close(&c);
    (*env)->ReleaseStringUTFChars(env, jso, path);
    if (jmode && mode) (*env)->ReleaseStringUTFChars(env, jmode, mode);
    return finish(env, path, jso);
}

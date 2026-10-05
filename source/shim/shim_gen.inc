/* ===== 本文件由 gen_shim.py 自动生成，请勿手改 ===== */

/* ---- vkAcquireNextImageKHR  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkAcquireNextImageKHR)(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    timeout,
    VkSemaphore                                 semaphore,
    VkFence                                     fence,
    uint32_t*                                   pImageIndex);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    timeout,
    VkSemaphore                                 semaphore,
    VkFence                                     fence,
    uint32_t*                                   pImageIndex)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkAcquireNextImageKHR"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkAcquireNextImageKHR"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkAcquireNextImageKHR)fp)(device, swapchain, timeout, semaphore, fence, pImageIndex);
}

/* ---- vkAllocateCommandBuffers  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkAllocateCommandBuffers)(VkDevice                                    device,
    const VkCommandBufferAllocateInfo*          pAllocateInfo,
    VkCommandBuffer*                            pCommandBuffers);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice                                    device,
    const VkCommandBufferAllocateInfo*          pAllocateInfo,
    VkCommandBuffer*                            pCommandBuffers)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkAllocateCommandBuffers"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkAllocateCommandBuffers"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkAllocateCommandBuffers)fp)(device, pAllocateInfo, pCommandBuffers);
}

/* ---- vkAllocateDescriptorSets  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkAllocateDescriptorSets)(VkDevice                                    device,
    const VkDescriptorSetAllocateInfo*          pAllocateInfo,
    VkDescriptorSet*                            pDescriptorSets);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice                                    device,
    const VkDescriptorSetAllocateInfo*          pAllocateInfo,
    VkDescriptorSet*                            pDescriptorSets)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkAllocateDescriptorSets"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkAllocateDescriptorSets"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkAllocateDescriptorSets)fp)(device, pAllocateInfo, pDescriptorSets);
}

/* ---- vkAllocateMemory  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkAllocateMemory)(VkDevice                                    device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMemory);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice                                    device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMemory)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkAllocateMemory"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkAllocateMemory"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkAllocateMemory)fp)(device, pAllocateInfo, pAllocator, pMemory);
}

/* ---- vkBeginCommandBuffer  [device_global] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkBeginCommandBuffer)(VkCommandBuffer                             commandBuffer,
    const VkCommandBufferBeginInfo*             pBeginInfo);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer                             commandBuffer,
    const VkCommandBufferBeginInfo*             pBeginInfo)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkBeginCommandBuffer"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkBeginCommandBuffer"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkBeginCommandBuffer)fp)(commandBuffer, pBeginInfo);
}

/* ---- vkBindBufferMemory  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkBindBufferMemory)(VkDevice                                    device,
    VkBuffer                                    buffer,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice                                    device,
    VkBuffer                                    buffer,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkBindBufferMemory"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkBindBufferMemory"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkBindBufferMemory)fp)(device, buffer, memory, memoryOffset);
}

/* ---- vkBindImageMemory  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkBindImageMemory)(VkDevice                                    device,
    VkImage                                     image,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice                                    device,
    VkImage                                     image,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkBindImageMemory"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkBindImageMemory"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkBindImageMemory)fp)(device, image, memory, memoryOffset);
}

/* ---- vkCmdBeginQuery  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdBeginQuery)(VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdBeginQuery"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdBeginQuery"); return; }
    ((PFN_vkCmdBeginQuery)fp)(commandBuffer, queryPool, query, flags);
}

/* ---- vkCmdBeginRenderPass  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdBeginRenderPass)(VkCommandBuffer                             commandBuffer,
    const VkRenderPassBeginInfo*                pRenderPassBegin,
    VkSubpassContents                           contents);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer                             commandBuffer,
    const VkRenderPassBeginInfo*                pRenderPassBegin,
    VkSubpassContents                           contents)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdBeginRenderPass"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdBeginRenderPass"); return; }
    ((PFN_vkCmdBeginRenderPass)fp)(commandBuffer, pRenderPassBegin, contents);
}

/* ---- vkCmdBindDescriptorSets  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdBindDescriptorSets)(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    firstSet,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets,
    uint32_t                                    dynamicOffsetCount,
    const uint32_t*                             pDynamicOffsets);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    firstSet,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets,
    uint32_t                                    dynamicOffsetCount,
    const uint32_t*                             pDynamicOffsets)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdBindDescriptorSets"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdBindDescriptorSets"); return; }
    ((PFN_vkCmdBindDescriptorSets)fp)(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}

/* ---- vkCmdBindIndexBuffer  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdBindIndexBuffer)(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkIndexType                                 indexType);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkIndexType                                 indexType)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdBindIndexBuffer"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdBindIndexBuffer"); return; }
    ((PFN_vkCmdBindIndexBuffer)fp)(commandBuffer, buffer, offset, indexType);
}

/* ---- vkCmdBindPipeline  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdBindPipeline)(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  pipeline);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  pipeline)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdBindPipeline"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdBindPipeline"); return; }
    ((PFN_vkCmdBindPipeline)fp)(commandBuffer, pipelineBindPoint, pipeline);
}

/* ---- vkCmdBindVertexBuffers  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdBindVertexBuffers)(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdBindVertexBuffers"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdBindVertexBuffers"); return; }
    ((PFN_vkCmdBindVertexBuffers)fp)(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
}

/* ---- vkCmdBlitImage  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdBlitImage)(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageBlit*                          pRegions,
    VkFilter                                    filter);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageBlit*                          pRegions,
    VkFilter                                    filter)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdBlitImage"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdBlitImage"); return; }
    ((PFN_vkCmdBlitImage)fp)(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
}

/* ---- vkCmdClearAttachments  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdClearAttachments)(VkCommandBuffer                             commandBuffer,
    uint32_t                                    attachmentCount,
    const VkClearAttachment*                    pAttachments,
    uint32_t                                    rectCount,
    const VkClearRect*                          pRects);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdClearAttachments(VkCommandBuffer                             commandBuffer,
    uint32_t                                    attachmentCount,
    const VkClearAttachment*                    pAttachments,
    uint32_t                                    rectCount,
    const VkClearRect*                          pRects)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdClearAttachments"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdClearAttachments"); return; }
    ((PFN_vkCmdClearAttachments)fp)(commandBuffer, attachmentCount, pAttachments, rectCount, pRects);
}

/* ---- vkCmdClearColorImage  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdClearColorImage)(VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearColorValue*                    pColor,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearColorValue*                    pColor,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdClearColorImage"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdClearColorImage"); return; }
    ((PFN_vkCmdClearColorImage)fp)(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
}

/* ---- vkCmdClearDepthStencilImage  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdClearDepthStencilImage)(VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearDepthStencilValue*             pDepthStencil,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearDepthStencilValue*             pDepthStencil,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdClearDepthStencilImage"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdClearDepthStencilImage"); return; }
    ((PFN_vkCmdClearDepthStencilImage)fp)(commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
}

/* ---- vkCmdCopyBuffer  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdCopyBuffer)(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkBuffer                                    dstBuffer,
    uint32_t                                    regionCount,
    const VkBufferCopy*                         pRegions);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkBuffer                                    dstBuffer,
    uint32_t                                    regionCount,
    const VkBufferCopy*                         pRegions)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdCopyBuffer"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdCopyBuffer"); return; }
    ((PFN_vkCmdCopyBuffer)fp)(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}

/* ---- vkCmdCopyBufferToImage  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdCopyBufferToImage)(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdCopyBufferToImage"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdCopyBufferToImage"); return; }
    ((PFN_vkCmdCopyBufferToImage)fp)(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}

/* ---- vkCmdCopyImage  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdCopyImage)(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageCopy*                          pRegions);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageCopy*                          pRegions)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdCopyImage"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdCopyImage"); return; }
    ((PFN_vkCmdCopyImage)fp)(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}

/* ---- vkCmdCopyImageToBuffer  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdCopyImageToBuffer)(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkBuffer                                    dstBuffer,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkBuffer                                    dstBuffer,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdCopyImageToBuffer"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdCopyImageToBuffer"); return; }
    ((PFN_vkCmdCopyImageToBuffer)fp)(commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
}

/* ---- vkCmdDispatch  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdDispatch)(VkCommandBuffer                             commandBuffer,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer                             commandBuffer,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdDispatch"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdDispatch"); return; }
    ((PFN_vkCmdDispatch)fp)(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

/* ---- vkCmdDispatchIndirect  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdDispatchIndirect)(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdDispatchIndirect"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdDispatchIndirect"); return; }
    ((PFN_vkCmdDispatchIndirect)fp)(commandBuffer, buffer, offset);
}

/* ---- vkCmdDraw  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdDraw)(VkCommandBuffer                             commandBuffer,
    uint32_t                                    vertexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstVertex,
    uint32_t                                    firstInstance);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer                             commandBuffer,
    uint32_t                                    vertexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstVertex,
    uint32_t                                    firstInstance)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdDraw"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdDraw"); return; }
    ((PFN_vkCmdDraw)fp)(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

/* ---- vkCmdDrawIndexed  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdDrawIndexed)(VkCommandBuffer                             commandBuffer,
    uint32_t                                    indexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstIndex,
    int32_t                                     vertexOffset,
    uint32_t                                    firstInstance);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer                             commandBuffer,
    uint32_t                                    indexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstIndex,
    int32_t                                     vertexOffset,
    uint32_t                                    firstInstance)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdDrawIndexed"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdDrawIndexed"); return; }
    ((PFN_vkCmdDrawIndexed)fp)(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

/* ---- vkCmdDrawIndexedIndirect  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdDrawIndexedIndirect)(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdDrawIndexedIndirect"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdDrawIndexedIndirect"); return; }
    ((PFN_vkCmdDrawIndexedIndirect)fp)(commandBuffer, buffer, offset, drawCount, stride);
}

/* ---- vkCmdDrawIndirect  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdDrawIndirect)(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdDrawIndirect"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdDrawIndirect"); return; }
    ((PFN_vkCmdDrawIndirect)fp)(commandBuffer, buffer, offset, drawCount, stride);
}

/* ---- vkCmdEndQuery  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdEndQuery)(VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdEndQuery"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdEndQuery"); return; }
    ((PFN_vkCmdEndQuery)fp)(commandBuffer, queryPool, query);
}

/* ---- vkCmdEndRenderPass  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdEndRenderPass)(VkCommandBuffer                             commandBuffer);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer                             commandBuffer)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdEndRenderPass"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdEndRenderPass"); return; }
    ((PFN_vkCmdEndRenderPass)fp)(commandBuffer);
}

/* ---- vkCmdFillBuffer  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdFillBuffer)(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                size,
    uint32_t                                    data);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer                             commandBuffer,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                size,
    uint32_t                                    data)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdFillBuffer"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdFillBuffer"); return; }
    ((PFN_vkCmdFillBuffer)fp)(commandBuffer, dstBuffer, dstOffset, size, data);
}

/* ---- vkCmdPipelineBarrier  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdPipelineBarrier)(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        dstStageMask,
    VkDependencyFlags                           dependencyFlags,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        dstStageMask,
    VkDependencyFlags                           dependencyFlags,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdPipelineBarrier"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdPipelineBarrier"); return; }
    ((PFN_vkCmdPipelineBarrier)fp)(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}

/* ---- vkCmdPushConstants  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdPushConstants)(VkCommandBuffer                             commandBuffer,
    VkPipelineLayout                            layout,
    VkShaderStageFlags                          stageFlags,
    uint32_t                                    offset,
    uint32_t                                    size,
    const void*                                 pValues);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer                             commandBuffer,
    VkPipelineLayout                            layout,
    VkShaderStageFlags                          stageFlags,
    uint32_t                                    offset,
    uint32_t                                    size,
    const void*                                 pValues)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdPushConstants"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdPushConstants"); return; }
    ((PFN_vkCmdPushConstants)fp)(commandBuffer, layout, stageFlags, offset, size, pValues);
}

/* ---- vkCmdResetQueryPool  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdResetQueryPool)(VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdResetQueryPool"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdResetQueryPool"); return; }
    ((PFN_vkCmdResetQueryPool)fp)(commandBuffer, queryPool, firstQuery, queryCount);
}

/* ---- vkCmdResolveImage  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdResolveImage)(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageResolve*                       pRegions);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage(VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageResolve*                       pRegions)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdResolveImage"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdResolveImage"); return; }
    ((PFN_vkCmdResolveImage)fp)(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}

/* ---- vkCmdSetBlendConstants  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdSetBlendConstants)(VkCommandBuffer                             commandBuffer,
    const float                                 blendConstants[4]);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(VkCommandBuffer                             commandBuffer,
    const float                                 blendConstants[4])
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdSetBlendConstants"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdSetBlendConstants"); return; }
    ((PFN_vkCmdSetBlendConstants)fp)(commandBuffer, blendConstants);
}

/* ---- vkCmdSetDepthBias  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdSetDepthBias)(VkCommandBuffer                             commandBuffer,
    float                                       depthBiasConstantFactor,
    float                                       depthBiasClamp,
    float                                       depthBiasSlopeFactor);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(VkCommandBuffer                             commandBuffer,
    float                                       depthBiasConstantFactor,
    float                                       depthBiasClamp,
    float                                       depthBiasSlopeFactor)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdSetDepthBias"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdSetDepthBias"); return; }
    ((PFN_vkCmdSetDepthBias)fp)(commandBuffer, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
}

/* ---- vkCmdSetLineWidth  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdSetLineWidth)(VkCommandBuffer                             commandBuffer,
    float                                       lineWidth);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(VkCommandBuffer                             commandBuffer,
    float                                       lineWidth)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdSetLineWidth"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdSetLineWidth"); return; }
    ((PFN_vkCmdSetLineWidth)fp)(commandBuffer, lineWidth);
}

/* ---- vkCmdSetScissor  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdSetScissor)(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstScissor,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstScissor,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdSetScissor"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdSetScissor"); return; }
    ((PFN_vkCmdSetScissor)fp)(commandBuffer, firstScissor, scissorCount, pScissors);
}

/* ---- vkCmdSetStencilCompareMask  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdSetStencilCompareMask)(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    compareMask);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilCompareMask(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    compareMask)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdSetStencilCompareMask"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdSetStencilCompareMask"); return; }
    ((PFN_vkCmdSetStencilCompareMask)fp)(commandBuffer, faceMask, compareMask);
}

/* ---- vkCmdSetStencilReference  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdSetStencilReference)(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    reference);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilReference(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    reference)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdSetStencilReference"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdSetStencilReference"); return; }
    ((PFN_vkCmdSetStencilReference)fp)(commandBuffer, faceMask, reference);
}

/* ---- vkCmdSetStencilWriteMask  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdSetStencilWriteMask)(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    writeMask);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilWriteMask(VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    writeMask)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdSetStencilWriteMask"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdSetStencilWriteMask"); return; }
    ((PFN_vkCmdSetStencilWriteMask)fp)(commandBuffer, faceMask, writeMask);
}

/* ---- vkCmdSetViewport  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdSetViewport)(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdSetViewport"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdSetViewport"); return; }
    ((PFN_vkCmdSetViewport)fp)(commandBuffer, firstViewport, viewportCount, pViewports);
}

/* ---- vkCmdWriteTimestamp  [device_global] ---- */
typedef void (VKAPI_PTR *PFN_vkCmdWriteTimestamp)(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlagBits                     pipelineStage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlagBits                     pipelineStage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkCmdWriteTimestamp"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkCmdWriteTimestamp"); return; }
    ((PFN_vkCmdWriteTimestamp)fp)(commandBuffer, pipelineStage, queryPool, query);
}

/* ---- vkCreateAndroidSurfaceKHR  [instance] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateAndroidSurfaceKHR)(VkInstance                                  instance,
    const VkAndroidSurfaceCreateInfoKHR*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateAndroidSurfaceKHR(VkInstance                                  instance,
    const VkAndroidSurfaceCreateInfoKHR*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)instance) ? sc_f : NULL;
    if (!fp) { fp = shim_res_instance(instance, "vkCreateAndroidSurfaceKHR"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)instance; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateAndroidSurfaceKHR"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateAndroidSurfaceKHR)fp)(instance, pCreateInfo, pAllocator, pSurface);
}

/* ---- vkCreateBuffer  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateBuffer)(VkDevice                                    device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice                                    device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateBuffer"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateBuffer"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateBuffer)fp)(device, pCreateInfo, pAllocator, pBuffer);
}

/* ---- vkCreateBufferView  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateBufferView)(VkDevice                                    device,
    const VkBufferViewCreateInfo*               pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBufferView*                               pView);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(VkDevice                                    device,
    const VkBufferViewCreateInfo*               pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBufferView*                               pView)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateBufferView"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateBufferView"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateBufferView)fp)(device, pCreateInfo, pAllocator, pView);
}

/* ---- vkCreateCommandPool  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateCommandPool)(VkDevice                                    device,
    const VkCommandPoolCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCommandPool*                              pCommandPool);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice                                    device,
    const VkCommandPoolCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCommandPool*                              pCommandPool)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateCommandPool"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateCommandPool"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateCommandPool)fp)(device, pCreateInfo, pAllocator, pCommandPool);
}

/* ---- vkCreateComputePipelines  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateComputePipelines)(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkComputePipelineCreateInfo*          pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkComputePipelineCreateInfo*          pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateComputePipelines"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateComputePipelines"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateComputePipelines)fp)(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}

/* ---- vkCreateDescriptorPool  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateDescriptorPool)(VkDevice                                    device,
    const VkDescriptorPoolCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorPool*                           pDescriptorPool);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice                                    device,
    const VkDescriptorPoolCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorPool*                           pDescriptorPool)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateDescriptorPool"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateDescriptorPool"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateDescriptorPool)fp)(device, pCreateInfo, pAllocator, pDescriptorPool);
}

/* ---- vkCreateDescriptorSetLayout  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateDescriptorSetLayout)(VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorSetLayout*                      pSetLayout);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorSetLayout*                      pSetLayout)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateDescriptorSetLayout"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateDescriptorSetLayout"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateDescriptorSetLayout)fp)(device, pCreateInfo, pAllocator, pSetLayout);
}

/* ---- vkCreateFence  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateFence)(VkDevice                                    device,
    const VkFenceCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice                                    device,
    const VkFenceCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateFence"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateFence"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateFence)fp)(device, pCreateInfo, pAllocator, pFence);
}

/* ---- vkCreateFramebuffer  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateFramebuffer)(VkDevice                                    device,
    const VkFramebufferCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFramebuffer*                              pFramebuffer);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice                                    device,
    const VkFramebufferCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFramebuffer*                              pFramebuffer)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateFramebuffer"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateFramebuffer"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateFramebuffer)fp)(device, pCreateInfo, pAllocator, pFramebuffer);
}

/* ---- vkCreateGraphicsPipelines  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateGraphicsPipelines)(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkGraphicsPipelineCreateInfo*         pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkGraphicsPipelineCreateInfo*         pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateGraphicsPipelines"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateGraphicsPipelines"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateGraphicsPipelines)fp)(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}

/* ---- vkCreateImage  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateImage)(VkDevice                                    device,
    const VkImageCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImage*                                    pImage);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice                                    device,
    const VkImageCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImage*                                    pImage)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateImage"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateImage"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateImage)fp)(device, pCreateInfo, pAllocator, pImage);
}

/* ---- vkCreateImageView  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateImageView)(VkDevice                                    device,
    const VkImageViewCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImageView*                                pView);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice                                    device,
    const VkImageViewCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImageView*                                pView)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateImageView"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateImageView"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateImageView)fp)(device, pCreateInfo, pAllocator, pView);
}

/* ---- vkCreatePipelineCache  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreatePipelineCache)(VkDevice                                    device,
    const VkPipelineCacheCreateInfo*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineCache*                            pPipelineCache);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice                                    device,
    const VkPipelineCacheCreateInfo*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineCache*                            pPipelineCache)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreatePipelineCache"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreatePipelineCache"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreatePipelineCache)fp)(device, pCreateInfo, pAllocator, pPipelineCache);
}

/* ---- vkCreatePipelineLayout  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreatePipelineLayout)(VkDevice                                    device,
    const VkPipelineLayoutCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineLayout*                           pPipelineLayout);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice                                    device,
    const VkPipelineLayoutCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineLayout*                           pPipelineLayout)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreatePipelineLayout"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreatePipelineLayout"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreatePipelineLayout)fp)(device, pCreateInfo, pAllocator, pPipelineLayout);
}

/* ---- vkCreateQueryPool  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateQueryPool)(VkDevice                                    device,
    const VkQueryPoolCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkQueryPool*                                pQueryPool);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(VkDevice                                    device,
    const VkQueryPoolCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkQueryPool*                                pQueryPool)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateQueryPool"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateQueryPool"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateQueryPool)fp)(device, pCreateInfo, pAllocator, pQueryPool);
}

/* ---- vkCreateRenderPass  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateRenderPass)(VkDevice                                    device,
    const VkRenderPassCreateInfo*               pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice                                    device,
    const VkRenderPassCreateInfo*               pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateRenderPass"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateRenderPass"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateRenderPass)fp)(device, pCreateInfo, pAllocator, pRenderPass);
}

/* ---- vkCreateSampler  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateSampler)(VkDevice                                    device,
    const VkSamplerCreateInfo*                  pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSampler*                                  pSampler);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice                                    device,
    const VkSamplerCreateInfo*                  pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSampler*                                  pSampler)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateSampler"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateSampler"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateSampler)fp)(device, pCreateInfo, pAllocator, pSampler);
}

/* ---- vkCreateSemaphore  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateSemaphore)(VkDevice                                    device,
    const VkSemaphoreCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSemaphore*                                pSemaphore);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice                                    device,
    const VkSemaphoreCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSemaphore*                                pSemaphore)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateSemaphore"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateSemaphore"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateSemaphore)fp)(device, pCreateInfo, pAllocator, pSemaphore);
}

/* ---- vkCreateShaderModule  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateShaderModule)(VkDevice                                    device,
    const VkShaderModuleCreateInfo*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderModule*                             pShaderModule);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice                                    device,
    const VkShaderModuleCreateInfo*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderModule*                             pShaderModule)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateShaderModule"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateShaderModule"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateShaderModule)fp)(device, pCreateInfo, pAllocator, pShaderModule);
}

/* ---- vkCreateSwapchainKHR  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkCreateSwapchainKHR)(VkDevice                                    device,
    const VkSwapchainCreateInfoKHR*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSwapchainKHR*                             pSwapchain);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice                                    device,
    const VkSwapchainCreateInfoKHR*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSwapchainKHR*                             pSwapchain)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkCreateSwapchainKHR"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkCreateSwapchainKHR"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkCreateSwapchainKHR)fp)(device, pCreateInfo, pAllocator, pSwapchain);
}

/* ---- vkDestroyBuffer  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyBuffer)(VkDevice                                    device,
    VkBuffer                                    buffer,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice                                    device,
    VkBuffer                                    buffer,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyBuffer"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyBuffer"); return; }
    ((PFN_vkDestroyBuffer)fp)(device, buffer, pAllocator);
}

/* ---- vkDestroyBufferView  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyBufferView)(VkDevice                                    device,
    VkBufferView                                bufferView,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyBufferView(VkDevice                                    device,
    VkBufferView                                bufferView,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyBufferView"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyBufferView"); return; }
    ((PFN_vkDestroyBufferView)fp)(device, bufferView, pAllocator);
}

/* ---- vkDestroyCommandPool  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyCommandPool)(VkDevice                                    device,
    VkCommandPool                               commandPool,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice                                    device,
    VkCommandPool                               commandPool,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyCommandPool"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyCommandPool"); return; }
    ((PFN_vkDestroyCommandPool)fp)(device, commandPool, pAllocator);
}

/* ---- vkDestroyDescriptorPool  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyDescriptorPool)(VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyDescriptorPool"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyDescriptorPool"); return; }
    ((PFN_vkDestroyDescriptorPool)fp)(device, descriptorPool, pAllocator);
}

/* ---- vkDestroyDescriptorSetLayout  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyDescriptorSetLayout)(VkDevice                                    device,
    VkDescriptorSetLayout                       descriptorSetLayout,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice                                    device,
    VkDescriptorSetLayout                       descriptorSetLayout,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyDescriptorSetLayout"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyDescriptorSetLayout"); return; }
    ((PFN_vkDestroyDescriptorSetLayout)fp)(device, descriptorSetLayout, pAllocator);
}

/* ---- vkDestroyFence  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyFence)(VkDevice                                    device,
    VkFence                                     fence,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice                                    device,
    VkFence                                     fence,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyFence"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyFence"); return; }
    ((PFN_vkDestroyFence)fp)(device, fence, pAllocator);
}

/* ---- vkDestroyFramebuffer  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyFramebuffer)(VkDevice                                    device,
    VkFramebuffer                               framebuffer,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice                                    device,
    VkFramebuffer                               framebuffer,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyFramebuffer"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyFramebuffer"); return; }
    ((PFN_vkDestroyFramebuffer)fp)(device, framebuffer, pAllocator);
}

/* ---- vkDestroyImage  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyImage)(VkDevice                                    device,
    VkImage                                     image,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice                                    device,
    VkImage                                     image,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyImage"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyImage"); return; }
    ((PFN_vkDestroyImage)fp)(device, image, pAllocator);
}

/* ---- vkDestroyImageView  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyImageView)(VkDevice                                    device,
    VkImageView                                 imageView,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice                                    device,
    VkImageView                                 imageView,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyImageView"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyImageView"); return; }
    ((PFN_vkDestroyImageView)fp)(device, imageView, pAllocator);
}

/* ---- vkDestroyPipeline  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyPipeline)(VkDevice                                    device,
    VkPipeline                                  pipeline,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice                                    device,
    VkPipeline                                  pipeline,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyPipeline"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyPipeline"); return; }
    ((PFN_vkDestroyPipeline)fp)(device, pipeline, pAllocator);
}

/* ---- vkDestroyPipelineCache  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyPipelineCache)(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyPipelineCache"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyPipelineCache"); return; }
    ((PFN_vkDestroyPipelineCache)fp)(device, pipelineCache, pAllocator);
}

/* ---- vkDestroyPipelineLayout  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyPipelineLayout)(VkDevice                                    device,
    VkPipelineLayout                            pipelineLayout,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice                                    device,
    VkPipelineLayout                            pipelineLayout,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyPipelineLayout"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyPipelineLayout"); return; }
    ((PFN_vkDestroyPipelineLayout)fp)(device, pipelineLayout, pAllocator);
}

/* ---- vkDestroyQueryPool  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyQueryPool)(VkDevice                                    device,
    VkQueryPool                                 queryPool,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(VkDevice                                    device,
    VkQueryPool                                 queryPool,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyQueryPool"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyQueryPool"); return; }
    ((PFN_vkDestroyQueryPool)fp)(device, queryPool, pAllocator);
}

/* ---- vkDestroyRenderPass  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyRenderPass)(VkDevice                                    device,
    VkRenderPass                                renderPass,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice                                    device,
    VkRenderPass                                renderPass,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyRenderPass"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyRenderPass"); return; }
    ((PFN_vkDestroyRenderPass)fp)(device, renderPass, pAllocator);
}

/* ---- vkDestroySampler  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroySampler)(VkDevice                                    device,
    VkSampler                                   sampler,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroySampler(VkDevice                                    device,
    VkSampler                                   sampler,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroySampler"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroySampler"); return; }
    ((PFN_vkDestroySampler)fp)(device, sampler, pAllocator);
}

/* ---- vkDestroySemaphore  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroySemaphore)(VkDevice                                    device,
    VkSemaphore                                 semaphore,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(VkDevice                                    device,
    VkSemaphore                                 semaphore,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroySemaphore"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroySemaphore"); return; }
    ((PFN_vkDestroySemaphore)fp)(device, semaphore, pAllocator);
}

/* ---- vkDestroyShaderModule  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroyShaderModule)(VkDevice                                    device,
    VkShaderModule                              shaderModule,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice                                    device,
    VkShaderModule                              shaderModule,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroyShaderModule"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroyShaderModule"); return; }
    ((PFN_vkDestroyShaderModule)fp)(device, shaderModule, pAllocator);
}

/* ---- vkDestroySurfaceKHR  [instance] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroySurfaceKHR)(VkInstance                                  instance,
    VkSurfaceKHR                                surface,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(VkInstance                                  instance,
    VkSurfaceKHR                                surface,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)instance) ? sc_f : NULL;
    if (!fp) { fp = shim_res_instance(instance, "vkDestroySurfaceKHR"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)instance; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroySurfaceKHR"); return; }
    ((PFN_vkDestroySurfaceKHR)fp)(instance, surface, pAllocator);
}

/* ---- vkDestroySwapchainKHR  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkDestroySwapchainKHR)(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDestroySwapchainKHR"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDestroySwapchainKHR"); return; }
    ((PFN_vkDestroySwapchainKHR)fp)(device, swapchain, pAllocator);
}

/* ---- vkDeviceWaitIdle  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkDeviceWaitIdle)(VkDevice                                    device);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice                                    device)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkDeviceWaitIdle"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkDeviceWaitIdle"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkDeviceWaitIdle)fp)(device);
}

/* ---- vkEndCommandBuffer  [device_global] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkEndCommandBuffer)(VkCommandBuffer                             commandBuffer);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer                             commandBuffer)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkEndCommandBuffer"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkEndCommandBuffer"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkEndCommandBuffer)fp)(commandBuffer);
}

/* ---- vkEnumerateDeviceExtensionProperties  [physdev] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkEnumerateDeviceExtensionProperties)(VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_inst) ? sc_f : NULL;
    if (!fp) { fp = shim_res_physdev("vkEnumerateDeviceExtensionProperties"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_inst; } }
    if (!fp) { LOGI("MISSING %s", "vkEnumerateDeviceExtensionProperties"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkEnumerateDeviceExtensionProperties)fp)(physicalDevice, pLayerName, pPropertyCount, pProperties);
}

/* ---- vkEnumerateInstanceExtensionProperties  [instance_create] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkEnumerateInstanceExtensionProperties)(const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)1) ? sc_f : NULL;
    if (!fp) { fp = shim_res_icd_null("vkEnumerateInstanceExtensionProperties"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)1; } }
    if (!fp) { LOGI("MISSING %s", "vkEnumerateInstanceExtensionProperties"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkEnumerateInstanceExtensionProperties)fp)(pLayerName, pPropertyCount, pProperties);
}

/* ---- vkEnumerateInstanceLayerProperties  [instance_create] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkEnumerateInstanceLayerProperties)(uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)1) ? sc_f : NULL;
    if (!fp) { fp = shim_res_icd_null("vkEnumerateInstanceLayerProperties"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)1; } }
    if (!fp) { LOGI("MISSING %s", "vkEnumerateInstanceLayerProperties"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkEnumerateInstanceLayerProperties)fp)(pPropertyCount, pProperties);
}

/* ---- vkEnumeratePhysicalDevices  [instance] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkEnumeratePhysicalDevices)(VkInstance                                  instance,
    uint32_t*                                   pPhysicalDeviceCount,
    VkPhysicalDevice*                           pPhysicalDevices);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance                                  instance,
    uint32_t*                                   pPhysicalDeviceCount,
    VkPhysicalDevice*                           pPhysicalDevices)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)instance) ? sc_f : NULL;
    if (!fp) { fp = shim_res_instance(instance, "vkEnumeratePhysicalDevices"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)instance; } }
    if (!fp) { LOGI("MISSING %s", "vkEnumeratePhysicalDevices"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkEnumeratePhysicalDevices)fp)(instance, pPhysicalDeviceCount, pPhysicalDevices);
}

/* ---- vkFlushMappedMemoryRanges  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkFlushMappedMemoryRanges)(VkDevice                                    device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice                                    device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkFlushMappedMemoryRanges"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkFlushMappedMemoryRanges"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkFlushMappedMemoryRanges)fp)(device, memoryRangeCount, pMemoryRanges);
}

/* ---- vkFreeCommandBuffers  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkFreeCommandBuffers)(VkDevice                                    device,
    VkCommandPool                               commandPool,
    uint32_t                                    commandBufferCount,
    const VkCommandBuffer*                      pCommandBuffers);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice                                    device,
    VkCommandPool                               commandPool,
    uint32_t                                    commandBufferCount,
    const VkCommandBuffer*                      pCommandBuffers)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkFreeCommandBuffers"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkFreeCommandBuffers"); return; }
    ((PFN_vkFreeCommandBuffers)fp)(device, commandPool, commandBufferCount, pCommandBuffers);
}

/* ---- vkFreeDescriptorSets  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkFreeDescriptorSets)(VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkFreeDescriptorSets"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkFreeDescriptorSets"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkFreeDescriptorSets)fp)(device, descriptorPool, descriptorSetCount, pDescriptorSets);
}

/* ---- vkFreeMemory  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkFreeMemory)(VkDevice                                    device,
    VkDeviceMemory                              memory,
    const VkAllocationCallbacks*                pAllocator);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice                                    device,
    VkDeviceMemory                              memory,
    const VkAllocationCallbacks*                pAllocator)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkFreeMemory"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkFreeMemory"); return; }
    ((PFN_vkFreeMemory)fp)(device, memory, pAllocator);
}

/* ---- vkGetBufferMemoryRequirements  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkGetBufferMemoryRequirements)(VkDevice                                    device,
    VkBuffer                                    buffer,
    VkMemoryRequirements*                       pMemoryRequirements);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice                                    device,
    VkBuffer                                    buffer,
    VkMemoryRequirements*                       pMemoryRequirements)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkGetBufferMemoryRequirements"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkGetBufferMemoryRequirements"); return; }
    ((PFN_vkGetBufferMemoryRequirements)fp)(device, buffer, pMemoryRequirements);
}

/* ---- vkGetDeviceQueue  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkGetDeviceQueue)(VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkGetDeviceQueue"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkGetDeviceQueue"); return; }
    ((PFN_vkGetDeviceQueue)fp)(device, queueFamilyIndex, queueIndex, pQueue);
}

/* ---- vkGetFenceStatus  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkGetFenceStatus)(VkDevice                                    device,
    VkFence                                     fence);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice                                    device,
    VkFence                                     fence)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkGetFenceStatus"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkGetFenceStatus"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkGetFenceStatus)fp)(device, fence);
}

/* ---- vkGetImageMemoryRequirements  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkGetImageMemoryRequirements)(VkDevice                                    device,
    VkImage                                     image,
    VkMemoryRequirements*                       pMemoryRequirements);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice                                    device,
    VkImage                                     image,
    VkMemoryRequirements*                       pMemoryRequirements)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkGetImageMemoryRequirements"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkGetImageMemoryRequirements"); return; }
    ((PFN_vkGetImageMemoryRequirements)fp)(device, image, pMemoryRequirements);
}

/* ---- vkGetPhysicalDeviceFeatures  [physdev] ---- */
typedef void (VKAPI_PTR *PFN_vkGetPhysicalDeviceFeatures)(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures*                   pFeatures);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures*                   pFeatures)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_inst) ? sc_f : NULL;
    if (!fp) { fp = shim_res_physdev("vkGetPhysicalDeviceFeatures"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_inst; } }
    if (!fp) { LOGI("MISSING %s", "vkGetPhysicalDeviceFeatures"); return; }
    ((PFN_vkGetPhysicalDeviceFeatures)fp)(physicalDevice, pFeatures);
}

/* ---- vkGetPhysicalDeviceFormatProperties  [physdev] ---- */
typedef void (VKAPI_PTR *PFN_vkGetPhysicalDeviceFormatProperties)(VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties*                         pFormatProperties);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties*                         pFormatProperties)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_inst) ? sc_f : NULL;
    if (!fp) { fp = shim_res_physdev("vkGetPhysicalDeviceFormatProperties"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_inst; } }
    if (!fp) { LOGI("MISSING %s", "vkGetPhysicalDeviceFormatProperties"); return; }
    ((PFN_vkGetPhysicalDeviceFormatProperties)fp)(physicalDevice, format, pFormatProperties);
}

/* ---- vkGetPhysicalDeviceImageFormatProperties  [physdev] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceImageFormatProperties)(VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkImageTiling                               tiling,
    VkImageUsageFlags                           usage,
    VkImageCreateFlags                          flags,
    VkImageFormatProperties*                    pImageFormatProperties);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkImageTiling                               tiling,
    VkImageUsageFlags                           usage,
    VkImageCreateFlags                          flags,
    VkImageFormatProperties*                    pImageFormatProperties)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_inst) ? sc_f : NULL;
    if (!fp) { fp = shim_res_physdev("vkGetPhysicalDeviceImageFormatProperties"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_inst; } }
    if (!fp) { LOGI("MISSING %s", "vkGetPhysicalDeviceImageFormatProperties"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkGetPhysicalDeviceImageFormatProperties)fp)(physicalDevice, format, type, tiling, usage, flags, pImageFormatProperties);
}

/* ---- vkGetPhysicalDeviceMemoryProperties  [physdev] ---- */
typedef void (VKAPI_PTR *PFN_vkGetPhysicalDeviceMemoryProperties)(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties*           pMemoryProperties);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties*           pMemoryProperties)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_inst) ? sc_f : NULL;
    if (!fp) { fp = shim_res_physdev("vkGetPhysicalDeviceMemoryProperties"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_inst; } }
    if (!fp) { LOGI("MISSING %s", "vkGetPhysicalDeviceMemoryProperties"); return; }
    ((PFN_vkGetPhysicalDeviceMemoryProperties)fp)(physicalDevice, pMemoryProperties);
}

/* ---- vkGetPhysicalDeviceProperties  [physdev] ---- */
typedef void (VKAPI_PTR *PFN_vkGetPhysicalDeviceProperties)(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties*                 pProperties);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties*                 pProperties)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_inst) ? sc_f : NULL;
    if (!fp) { fp = shim_res_physdev("vkGetPhysicalDeviceProperties"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_inst; } }
    if (!fp) { LOGI("MISSING %s", "vkGetPhysicalDeviceProperties"); return; }
    ((PFN_vkGetPhysicalDeviceProperties)fp)(physicalDevice, pProperties);
}

/* ---- vkGetPhysicalDeviceQueueFamilyProperties  [physdev] ---- */
typedef void (VKAPI_PTR *PFN_vkGetPhysicalDeviceQueueFamilyProperties)(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties*                    pQueueFamilyProperties);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties*                    pQueueFamilyProperties)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_inst) ? sc_f : NULL;
    if (!fp) { fp = shim_res_physdev("vkGetPhysicalDeviceQueueFamilyProperties"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_inst; } }
    if (!fp) { LOGI("MISSING %s", "vkGetPhysicalDeviceQueueFamilyProperties"); return; }
    ((PFN_vkGetPhysicalDeviceQueueFamilyProperties)fp)(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

/* ---- vkGetPhysicalDeviceSurfaceCapabilitiesKHR  [physdev] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_inst) ? sc_f : NULL;
    if (!fp) { fp = shim_res_physdev("vkGetPhysicalDeviceSurfaceCapabilitiesKHR"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_inst; } }
    if (!fp) { LOGI("MISSING %s", "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)fp)(physicalDevice, surface, pSurfaceCapabilities);
}

/* ---- vkGetPhysicalDeviceSurfaceFormatsKHR  [physdev] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pSurfaceFormatCount,
    VkSurfaceFormatKHR*                         pSurfaceFormats);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pSurfaceFormatCount,
    VkSurfaceFormatKHR*                         pSurfaceFormats)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_inst) ? sc_f : NULL;
    if (!fp) { fp = shim_res_physdev("vkGetPhysicalDeviceSurfaceFormatsKHR"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_inst; } }
    if (!fp) { LOGI("MISSING %s", "vkGetPhysicalDeviceSurfaceFormatsKHR"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)fp)(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
}

/* ---- vkGetPhysicalDeviceSurfacePresentModesKHR  [physdev] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pPresentModeCount,
    VkPresentModeKHR*                           pPresentModes);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pPresentModeCount,
    VkPresentModeKHR*                           pPresentModes)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_inst) ? sc_f : NULL;
    if (!fp) { fp = shim_res_physdev("vkGetPhysicalDeviceSurfacePresentModesKHR"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_inst; } }
    if (!fp) { LOGI("MISSING %s", "vkGetPhysicalDeviceSurfacePresentModesKHR"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)fp)(physicalDevice, surface, pPresentModeCount, pPresentModes);
}

/* ---- vkGetPhysicalDeviceSurfaceSupportKHR  [physdev] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfaceSupportKHR)(VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    VkSurfaceKHR                                surface,
    VkBool32*                                   pSupported);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    VkSurfaceKHR                                surface,
    VkBool32*                                   pSupported)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_inst) ? sc_f : NULL;
    if (!fp) { fp = shim_res_physdev("vkGetPhysicalDeviceSurfaceSupportKHR"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_inst; } }
    if (!fp) { LOGI("MISSING %s", "vkGetPhysicalDeviceSurfaceSupportKHR"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkGetPhysicalDeviceSurfaceSupportKHR)fp)(physicalDevice, queueFamilyIndex, surface, pSupported);
}

/* ---- vkGetQueryPoolResults  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkGetQueryPoolResults)(VkDevice                                    device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    size_t                                      dataSize,
    void*                                       pData,
    VkDeviceSize                                stride,
    VkQueryResultFlags                          flags);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice                                    device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    size_t                                      dataSize,
    void*                                       pData,
    VkDeviceSize                                stride,
    VkQueryResultFlags                          flags)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkGetQueryPoolResults"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkGetQueryPoolResults"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkGetQueryPoolResults)fp)(device, queryPool, firstQuery, queryCount, dataSize, pData, stride, flags);
}

/* ---- vkGetSwapchainImagesKHR  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkGetSwapchainImagesKHR)(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t*                                   pSwapchainImageCount,
    VkImage*                                    pSwapchainImages);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t*                                   pSwapchainImageCount,
    VkImage*                                    pSwapchainImages)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkGetSwapchainImagesKHR"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkGetSwapchainImagesKHR"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkGetSwapchainImagesKHR)fp)(device, swapchain, pSwapchainImageCount, pSwapchainImages);
}

/* ---- vkInvalidateMappedMemoryRanges  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkInvalidateMappedMemoryRanges)(VkDevice                                    device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice                                    device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkInvalidateMappedMemoryRanges"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkInvalidateMappedMemoryRanges"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkInvalidateMappedMemoryRanges)fp)(device, memoryRangeCount, pMemoryRanges);
}

/* ---- vkMapMemory  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkMapMemory)(VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkMemoryMapFlags                            flags,
    void**                                      ppData);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkMemoryMapFlags                            flags,
    void**                                      ppData)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkMapMemory"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkMapMemory"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkMapMemory)fp)(device, memory, offset, size, flags, ppData);
}

/* ---- vkQueuePresentKHR  [device_global] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkQueuePresentKHR)(VkQueue                                     queue,
    const VkPresentInfoKHR*                     pPresentInfo);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue                                     queue,
    const VkPresentInfoKHR*                     pPresentInfo)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkQueuePresentKHR"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkQueuePresentKHR"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkQueuePresentKHR)fp)(queue, pPresentInfo);
}

/* ---- vkQueueSubmit  [device_global] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkQueueSubmit)(VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     fence);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     fence)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkQueueSubmit"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkQueueSubmit"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkQueueSubmit)fp)(queue, submitCount, pSubmits, fence);
}

/* ---- vkQueueWaitIdle  [device_global] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkQueueWaitIdle)(VkQueue                                     queue);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue                                     queue)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkQueueWaitIdle"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkQueueWaitIdle"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkQueueWaitIdle)fp)(queue);
}

/* ---- vkResetCommandBuffer  [device_global] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkResetCommandBuffer)(VkCommandBuffer                             commandBuffer,
    VkCommandBufferResetFlags                   flags);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer                             commandBuffer,
    VkCommandBufferResetFlags                   flags)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)g_dev) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device_global("vkResetCommandBuffer"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)g_dev; } }
    if (!fp) { LOGI("MISSING %s", "vkResetCommandBuffer"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkResetCommandBuffer)fp)(commandBuffer, flags);
}

/* ---- vkResetDescriptorPool  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkResetDescriptorPool)(VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    VkDescriptorPoolResetFlags                  flags);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    VkDescriptorPoolResetFlags                  flags)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkResetDescriptorPool"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkResetDescriptorPool"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkResetDescriptorPool)fp)(device, descriptorPool, flags);
}

/* ---- vkResetFences  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkResetFences)(VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkResetFences"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkResetFences"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkResetFences)fp)(device, fenceCount, pFences);
}

/* ---- vkUnmapMemory  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkUnmapMemory)(VkDevice                                    device,
    VkDeviceMemory                              memory);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice                                    device,
    VkDeviceMemory                              memory)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkUnmapMemory"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkUnmapMemory"); return; }
    ((PFN_vkUnmapMemory)fp)(device, memory);
}

/* ---- vkUpdateDescriptorSets  [device] ---- */
typedef void (VKAPI_PTR *PFN_vkUpdateDescriptorSets)(VkDevice                                    device,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites,
    uint32_t                                    descriptorCopyCount,
    const VkCopyDescriptorSet*                  pDescriptorCopies);
SHIM_EXPORT VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice                                    device,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites,
    uint32_t                                    descriptorCopyCount,
    const VkCopyDescriptorSet*                  pDescriptorCopies)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkUpdateDescriptorSets"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkUpdateDescriptorSets"); return; }
    ((PFN_vkUpdateDescriptorSets)fp)(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
}

/* ---- vkWaitForFences  [device] ---- */
typedef VkResult (VKAPI_PTR *PFN_vkWaitForFences)(VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences,
    VkBool32                                    waitAll,
    uint64_t                                    timeout);
SHIM_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences,
    VkBool32                                    waitAll,
    uint64_t                                    timeout)
{
    static void *sc_f, *sc_k;
    void *fp = (sc_f && sc_k == (void *)(uintptr_t)device) ? sc_f : NULL;
    if (!fp) { fp = shim_res_device(device, "vkWaitForFences"); if (fp) { sc_f = fp; sc_k = (void *)(uintptr_t)device; } }
    if (!fp) { LOGI("MISSING %s", "vkWaitForFences"); return VK_ERROR_INITIALIZATION_FAILED; }
    return ((PFN_vkWaitForFences)fp)(device, fenceCount, pFences, waitAll, timeout);
}

typedef struct { const char *name; void *fn; } shim_entry_t;

static const shim_entry_t shim_table[] = {
    { "vkAcquireNextImageKHR", (void *)(uintptr_t)&vkAcquireNextImageKHR },
    { "vkAllocateCommandBuffers", (void *)(uintptr_t)&vkAllocateCommandBuffers },
    { "vkAllocateDescriptorSets", (void *)(uintptr_t)&vkAllocateDescriptorSets },
    { "vkAllocateMemory", (void *)(uintptr_t)&vkAllocateMemory },
    { "vkBeginCommandBuffer", (void *)(uintptr_t)&vkBeginCommandBuffer },
    { "vkBindBufferMemory", (void *)(uintptr_t)&vkBindBufferMemory },
    { "vkBindImageMemory", (void *)(uintptr_t)&vkBindImageMemory },
    { "vkCmdBeginQuery", (void *)(uintptr_t)&vkCmdBeginQuery },
    { "vkCmdBeginRenderPass", (void *)(uintptr_t)&vkCmdBeginRenderPass },
    { "vkCmdBindDescriptorSets", (void *)(uintptr_t)&vkCmdBindDescriptorSets },
    { "vkCmdBindIndexBuffer", (void *)(uintptr_t)&vkCmdBindIndexBuffer },
    { "vkCmdBindPipeline", (void *)(uintptr_t)&vkCmdBindPipeline },
    { "vkCmdBindVertexBuffers", (void *)(uintptr_t)&vkCmdBindVertexBuffers },
    { "vkCmdBlitImage", (void *)(uintptr_t)&vkCmdBlitImage },
    { "vkCmdClearAttachments", (void *)(uintptr_t)&vkCmdClearAttachments },
    { "vkCmdClearColorImage", (void *)(uintptr_t)&vkCmdClearColorImage },
    { "vkCmdClearDepthStencilImage", (void *)(uintptr_t)&vkCmdClearDepthStencilImage },
    { "vkCmdCopyBuffer", (void *)(uintptr_t)&vkCmdCopyBuffer },
    { "vkCmdCopyBufferToImage", (void *)(uintptr_t)&vkCmdCopyBufferToImage },
    { "vkCmdCopyImage", (void *)(uintptr_t)&vkCmdCopyImage },
    { "vkCmdCopyImageToBuffer", (void *)(uintptr_t)&vkCmdCopyImageToBuffer },
    { "vkCmdDispatch", (void *)(uintptr_t)&vkCmdDispatch },
    { "vkCmdDispatchIndirect", (void *)(uintptr_t)&vkCmdDispatchIndirect },
    { "vkCmdDraw", (void *)(uintptr_t)&vkCmdDraw },
    { "vkCmdDrawIndexed", (void *)(uintptr_t)&vkCmdDrawIndexed },
    { "vkCmdDrawIndexedIndirect", (void *)(uintptr_t)&vkCmdDrawIndexedIndirect },
    { "vkCmdDrawIndirect", (void *)(uintptr_t)&vkCmdDrawIndirect },
    { "vkCmdEndQuery", (void *)(uintptr_t)&vkCmdEndQuery },
    { "vkCmdEndRenderPass", (void *)(uintptr_t)&vkCmdEndRenderPass },
    { "vkCmdFillBuffer", (void *)(uintptr_t)&vkCmdFillBuffer },
    { "vkCmdPipelineBarrier", (void *)(uintptr_t)&vkCmdPipelineBarrier },
    { "vkCmdPushConstants", (void *)(uintptr_t)&vkCmdPushConstants },
    { "vkCmdResetQueryPool", (void *)(uintptr_t)&vkCmdResetQueryPool },
    { "vkCmdResolveImage", (void *)(uintptr_t)&vkCmdResolveImage },
    { "vkCmdSetBlendConstants", (void *)(uintptr_t)&vkCmdSetBlendConstants },
    { "vkCmdSetDepthBias", (void *)(uintptr_t)&vkCmdSetDepthBias },
    { "vkCmdSetLineWidth", (void *)(uintptr_t)&vkCmdSetLineWidth },
    { "vkCmdSetScissor", (void *)(uintptr_t)&vkCmdSetScissor },
    { "vkCmdSetStencilCompareMask", (void *)(uintptr_t)&vkCmdSetStencilCompareMask },
    { "vkCmdSetStencilReference", (void *)(uintptr_t)&vkCmdSetStencilReference },
    { "vkCmdSetStencilWriteMask", (void *)(uintptr_t)&vkCmdSetStencilWriteMask },
    { "vkCmdSetViewport", (void *)(uintptr_t)&vkCmdSetViewport },
    { "vkCmdWriteTimestamp", (void *)(uintptr_t)&vkCmdWriteTimestamp },
    { "vkCreateAndroidSurfaceKHR", (void *)(uintptr_t)&vkCreateAndroidSurfaceKHR },
    { "vkCreateBuffer", (void *)(uintptr_t)&vkCreateBuffer },
    { "vkCreateBufferView", (void *)(uintptr_t)&vkCreateBufferView },
    { "vkCreateCommandPool", (void *)(uintptr_t)&vkCreateCommandPool },
    { "vkCreateComputePipelines", (void *)(uintptr_t)&vkCreateComputePipelines },
    { "vkCreateDescriptorPool", (void *)(uintptr_t)&vkCreateDescriptorPool },
    { "vkCreateDescriptorSetLayout", (void *)(uintptr_t)&vkCreateDescriptorSetLayout },
    { "vkCreateDevice", (void *)(uintptr_t)&vkCreateDevice },
    { "vkCreateFence", (void *)(uintptr_t)&vkCreateFence },
    { "vkCreateFramebuffer", (void *)(uintptr_t)&vkCreateFramebuffer },
    { "vkCreateGraphicsPipelines", (void *)(uintptr_t)&vkCreateGraphicsPipelines },
    { "vkCreateImage", (void *)(uintptr_t)&vkCreateImage },
    { "vkCreateImageView", (void *)(uintptr_t)&vkCreateImageView },
    { "vkCreateInstance", (void *)(uintptr_t)&vkCreateInstance },
    { "vkCreatePipelineCache", (void *)(uintptr_t)&vkCreatePipelineCache },
    { "vkCreatePipelineLayout", (void *)(uintptr_t)&vkCreatePipelineLayout },
    { "vkCreateQueryPool", (void *)(uintptr_t)&vkCreateQueryPool },
    { "vkCreateRenderPass", (void *)(uintptr_t)&vkCreateRenderPass },
    { "vkCreateSampler", (void *)(uintptr_t)&vkCreateSampler },
    { "vkCreateSemaphore", (void *)(uintptr_t)&vkCreateSemaphore },
    { "vkCreateShaderModule", (void *)(uintptr_t)&vkCreateShaderModule },
    { "vkCreateSwapchainKHR", (void *)(uintptr_t)&vkCreateSwapchainKHR },
    { "vkDestroyBuffer", (void *)(uintptr_t)&vkDestroyBuffer },
    { "vkDestroyBufferView", (void *)(uintptr_t)&vkDestroyBufferView },
    { "vkDestroyCommandPool", (void *)(uintptr_t)&vkDestroyCommandPool },
    { "vkDestroyDescriptorPool", (void *)(uintptr_t)&vkDestroyDescriptorPool },
    { "vkDestroyDescriptorSetLayout", (void *)(uintptr_t)&vkDestroyDescriptorSetLayout },
    { "vkDestroyDevice", (void *)(uintptr_t)&vkDestroyDevice },
    { "vkDestroyFence", (void *)(uintptr_t)&vkDestroyFence },
    { "vkDestroyFramebuffer", (void *)(uintptr_t)&vkDestroyFramebuffer },
    { "vkDestroyImage", (void *)(uintptr_t)&vkDestroyImage },
    { "vkDestroyImageView", (void *)(uintptr_t)&vkDestroyImageView },
    { "vkDestroyInstance", (void *)(uintptr_t)&vkDestroyInstance },
    { "vkDestroyPipeline", (void *)(uintptr_t)&vkDestroyPipeline },
    { "vkDestroyPipelineCache", (void *)(uintptr_t)&vkDestroyPipelineCache },
    { "vkDestroyPipelineLayout", (void *)(uintptr_t)&vkDestroyPipelineLayout },
    { "vkDestroyQueryPool", (void *)(uintptr_t)&vkDestroyQueryPool },
    { "vkDestroyRenderPass", (void *)(uintptr_t)&vkDestroyRenderPass },
    { "vkDestroySampler", (void *)(uintptr_t)&vkDestroySampler },
    { "vkDestroySemaphore", (void *)(uintptr_t)&vkDestroySemaphore },
    { "vkDestroyShaderModule", (void *)(uintptr_t)&vkDestroyShaderModule },
    { "vkDestroySurfaceKHR", (void *)(uintptr_t)&vkDestroySurfaceKHR },
    { "vkDestroySwapchainKHR", (void *)(uintptr_t)&vkDestroySwapchainKHR },
    { "vkDeviceWaitIdle", (void *)(uintptr_t)&vkDeviceWaitIdle },
    { "vkEndCommandBuffer", (void *)(uintptr_t)&vkEndCommandBuffer },
    { "vkEnumerateDeviceExtensionProperties", (void *)(uintptr_t)&vkEnumerateDeviceExtensionProperties },
    { "vkEnumerateInstanceExtensionProperties", (void *)(uintptr_t)&vkEnumerateInstanceExtensionProperties },
    { "vkEnumerateInstanceLayerProperties", (void *)(uintptr_t)&vkEnumerateInstanceLayerProperties },
    { "vkEnumeratePhysicalDevices", (void *)(uintptr_t)&vkEnumeratePhysicalDevices },
    { "vkFlushMappedMemoryRanges", (void *)(uintptr_t)&vkFlushMappedMemoryRanges },
    { "vkFreeCommandBuffers", (void *)(uintptr_t)&vkFreeCommandBuffers },
    { "vkFreeDescriptorSets", (void *)(uintptr_t)&vkFreeDescriptorSets },
    { "vkFreeMemory", (void *)(uintptr_t)&vkFreeMemory },
    { "vkGetBufferMemoryRequirements", (void *)(uintptr_t)&vkGetBufferMemoryRequirements },
    { "vkGetDeviceProcAddr", (void *)(uintptr_t)&vkGetDeviceProcAddr },
    { "vkGetDeviceQueue", (void *)(uintptr_t)&vkGetDeviceQueue },
    { "vkGetFenceStatus", (void *)(uintptr_t)&vkGetFenceStatus },
    { "vkGetImageMemoryRequirements", (void *)(uintptr_t)&vkGetImageMemoryRequirements },
    { "vkGetInstanceProcAddr", (void *)(uintptr_t)&vkGetInstanceProcAddr },
    { "vkGetPhysicalDeviceFeatures", (void *)(uintptr_t)&vkGetPhysicalDeviceFeatures },
    { "vkGetPhysicalDeviceFormatProperties", (void *)(uintptr_t)&vkGetPhysicalDeviceFormatProperties },
    { "vkGetPhysicalDeviceImageFormatProperties", (void *)(uintptr_t)&vkGetPhysicalDeviceImageFormatProperties },
    { "vkGetPhysicalDeviceMemoryProperties", (void *)(uintptr_t)&vkGetPhysicalDeviceMemoryProperties },
    { "vkGetPhysicalDeviceProperties", (void *)(uintptr_t)&vkGetPhysicalDeviceProperties },
    { "vkGetPhysicalDeviceQueueFamilyProperties", (void *)(uintptr_t)&vkGetPhysicalDeviceQueueFamilyProperties },
    { "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", (void *)(uintptr_t)&vkGetPhysicalDeviceSurfaceCapabilitiesKHR },
    { "vkGetPhysicalDeviceSurfaceFormatsKHR", (void *)(uintptr_t)&vkGetPhysicalDeviceSurfaceFormatsKHR },
    { "vkGetPhysicalDeviceSurfacePresentModesKHR", (void *)(uintptr_t)&vkGetPhysicalDeviceSurfacePresentModesKHR },
    { "vkGetPhysicalDeviceSurfaceSupportKHR", (void *)(uintptr_t)&vkGetPhysicalDeviceSurfaceSupportKHR },
    { "vkGetQueryPoolResults", (void *)(uintptr_t)&vkGetQueryPoolResults },
    { "vkGetSwapchainImagesKHR", (void *)(uintptr_t)&vkGetSwapchainImagesKHR },
    { "vkInvalidateMappedMemoryRanges", (void *)(uintptr_t)&vkInvalidateMappedMemoryRanges },
    { "vkMapMemory", (void *)(uintptr_t)&vkMapMemory },
    { "vkQueuePresentKHR", (void *)(uintptr_t)&vkQueuePresentKHR },
    { "vkQueueSubmit", (void *)(uintptr_t)&vkQueueSubmit },
    { "vkQueueWaitIdle", (void *)(uintptr_t)&vkQueueWaitIdle },
    { "vkResetCommandBuffer", (void *)(uintptr_t)&vkResetCommandBuffer },
    { "vkResetDescriptorPool", (void *)(uintptr_t)&vkResetDescriptorPool },
    { "vkResetFences", (void *)(uintptr_t)&vkResetFences },
    { "vkUnmapMemory", (void *)(uintptr_t)&vkUnmapMemory },
    { "vkUpdateDescriptorSets", (void *)(uintptr_t)&vkUpdateDescriptorSets },
    { "vkWaitForFences", (void *)(uintptr_t)&vkWaitForFences },
    { NULL, NULL }
};

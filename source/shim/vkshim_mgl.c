// 自动生成（修好正则 + 设备缓存版）—— 唯一名 libvkpanvk_shim.so
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <android/log.h>
#define FLOG(...) do{ __android_log_print(ANDROID_LOG_INFO,"vkshim",__VA_ARGS__);\
  FILE* _f=fopen("/sdcard/MG/vkshim.log","a"); if(_f){ fprintf(_f,__VA_ARGS__); fputc(10,_f); fclose(_f);} }while(0)
#include <stdint.h>
static void* g_icd=0;
static PFN_vkVoidFunction (*g_gipa)(VkInstance,const char*)=0;
static PFN_vkGetDeviceProcAddr g_gdpa=0;
static VkDevice g_dev=0;
static VkInstance g_inst=0;
static PFN_vkVoidFunction (*g_pdpa)(VkPhysicalDevice,const char*)=0;

static PFN_vkVoidFunction shim_lookup(const char*);
static PFN_vkVoidFunction gp_inst(VkInstance i, const char* n){
  PFN_vkVoidFunction f = g_gipa?g_gipa(i,n):0;
  if(!f && g_pdpa) f = g_pdpa((VkPhysicalDevice)i, n);
  return f;
}
static PFN_vkVoidFunction gp_dev(VkDevice d, const char* n){
  if(!g_gdpa && g_gipa && g_inst) g_gdpa=(PFN_vkGetDeviceProcAddr)g_gipa(g_inst,"vkGetDeviceProcAddr");
  PFN_vkVoidFunction f = g_gdpa?g_gdpa(d,n):0;
  if(!f && g_gipa) f = g_gipa(g_inst, n);          /* vkCreateDevice 等：instance 级 */
  if(!f && g_pdpa) f = g_pdpa((VkPhysicalDevice)g_dev, n);
  return f;
}
static PFN_vkVoidFunction gipa_pd(VkPhysicalDevice pd, const char* n){
  if(!g_pdpa && g_gipa) g_pdpa=(PFN_vkVoidFunction(*)(VkPhysicalDevice,const char*))g_gipa(g_inst,"vk_icdGetPhysicalDeviceProcAddr");
  PFN_vkVoidFunction f = 0;
  if(g_pdpa) f = g_pdpa(pd, n);              /* ★ 物理设备级优先（instance GIPA 会返回错函数）*/
  if(!f && g_gipa) f = g_gipa(g_inst, n);    /* 再退回 instance 级 */
  return f;
}
static void shim_init(void){
  if(g_icd) return;
  char selfdir[512]={0}; Dl_info info;
  if(dladdr((void*)(uintptr_t)&shim_init,&info)&&info.dli_fname){ const char* s=info.dli_fname; const char* sl=strrchr(s,'/'); if(sl&&(size_t)(sl-s)<sizeof(selfdir)-1){ size_t nn=(size_t)(sl-s); memcpy(selfdir,s,nn); selfdir[nn]=0; } }
  char cand0[640]={0}; if(selfdir[0]) snprintf(cand0,sizeof(cand0),"%s/libvulkan_freedreno.so",selfdir);
  const char* c[]={cand0,"libvulkan_freedreno.so","/data/local/tmp/libvulkan_freedreno.so",0};
  for(int i=0;c[i];i++){ g_icd=dlopen(c[i],RTLD_NOW|RTLD_LOCAL); if(g_icd) break; }
  if(!g_icd){ FLOG("[vkshim] cannot dlopen ICD\n"); return; }
  typedef VkResult(*neg_t)(uint32_t*);
  neg_t neg=(neg_t)dlsym(g_icd,"vk_icdNegotiateLoaderICDInterfaceVersion"); if(neg){uint32_t v=7;neg(&v);}
  g_gipa=(PFN_vkVoidFunction(*)(VkInstance,const char*))dlsym(g_icd,"vk_icdGetInstanceProcAddr");
  if(g_gipa && !g_gdpa) g_gdpa=(PFN_vkGetDeviceProcAddr)gp_inst(NULL,"vkGetDeviceProcAddr");
  FLOG("[vkshim] icd=%p gipa=%p gdpa=%p\n",g_icd,(void*)g_gipa,(void*)g_gdpa);
}
PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance i,const char* n){ shim_init(); PFN_vkVoidFunction f=shim_lookup(n); return f?f:gp_inst(i,n); }
PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice d,const char* n){ shim_init(); PFN_vkVoidFunction f=shim_lookup(n); return f?f:gp_dev(d,n); }
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
{
  shim_init();
  PFN_vkAcquireNextImageKHR fn=(PFN_vkAcquireNextImageKHR)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkAcquireNextImageKHR");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, swapchain, timeout, semaphore, fence, pImageIndex);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers)
{
  shim_init();
  PFN_vkAllocateCommandBuffers fn=(PFN_vkAllocateCommandBuffers)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkAllocateCommandBuffers");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pAllocateInfo, pCommandBuffers);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets)
{
  shim_init();
  PFN_vkAllocateDescriptorSets fn=(PFN_vkAllocateDescriptorSets)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkAllocateDescriptorSets");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pAllocateInfo, pDescriptorSets);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
{
  shim_init();
  PFN_vkAllocateMemory fn=(PFN_vkAllocateMemory)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkAllocateMemory");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pAllocateInfo, pAllocator, pMemory);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo)
{
  shim_init();
  PFN_vkBeginCommandBuffer fn=(PFN_vkBeginCommandBuffer)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkBeginCommandBuffer");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(commandBuffer, pBeginInfo);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
  shim_init();
  PFN_vkBindBufferMemory fn=(PFN_vkBindBufferMemory)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkBindBufferMemory");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, buffer, memory, memoryOffset);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
  shim_init();
  PFN_vkBindImageMemory fn=(PFN_vkBindImageMemory)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkBindImageMemory");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, image, memory, memoryOffset);
  return res;
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags){
  shim_init();
  PFN_vkCmdBeginQuery fn=(PFN_vkCmdBeginQuery)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdBeginQuery");
  if(fn) fn(commandBuffer, queryPool, query, flags);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents){
  shim_init();
  PFN_vkCmdBeginRenderPass fn=(PFN_vkCmdBeginRenderPass)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdBeginRenderPass");
  if(fn) fn(commandBuffer, pRenderPassBegin, contents);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets){
  shim_init();
  PFN_vkCmdBindDescriptorSets fn=(PFN_vkCmdBindDescriptorSets)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdBindDescriptorSets");
  if(fn) fn(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType){
  shim_init();
  PFN_vkCmdBindIndexBuffer fn=(PFN_vkCmdBindIndexBuffer)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdBindIndexBuffer");
  if(fn) fn(commandBuffer, buffer, offset, indexType);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline){
  shim_init();
  PFN_vkCmdBindPipeline fn=(PFN_vkCmdBindPipeline)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdBindPipeline");
  if(fn) fn(commandBuffer, pipelineBindPoint, pipeline);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets){
  shim_init();
  PFN_vkCmdBindVertexBuffers fn=(PFN_vkCmdBindVertexBuffers)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdBindVertexBuffers");
  if(fn) fn(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter){
  shim_init();
  PFN_vkCmdBlitImage fn=(PFN_vkCmdBlitImage)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdBlitImage");
  if(fn) fn(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
}
VKAPI_ATTR void VKAPI_CALL vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects){
  shim_init();
  PFN_vkCmdClearAttachments fn=(PFN_vkCmdClearAttachments)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdClearAttachments");
  if(fn) fn(commandBuffer, attachmentCount, pAttachments, rectCount, pRects);
}
VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges){
  shim_init();
  PFN_vkCmdClearColorImage fn=(PFN_vkCmdClearColorImage)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdClearColorImage");
  if(fn) fn(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
}
VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges){
  shim_init();
  PFN_vkCmdClearDepthStencilImage fn=(PFN_vkCmdClearDepthStencilImage)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdClearDepthStencilImage");
  if(fn) fn(commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions){
  shim_init();
  PFN_vkCmdCopyBuffer fn=(PFN_vkCmdCopyBuffer)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdCopyBuffer");
  if(fn) fn(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions){
  shim_init();
  PFN_vkCmdCopyBufferToImage fn=(PFN_vkCmdCopyBufferToImage)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdCopyBufferToImage");
  if(fn) fn(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions){
  shim_init();
  PFN_vkCmdCopyImage fn=(PFN_vkCmdCopyImage)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdCopyImage");
  if(fn) fn(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions){
  shim_init();
  PFN_vkCmdCopyImageToBuffer fn=(PFN_vkCmdCopyImageToBuffer)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdCopyImageToBuffer");
  if(fn) fn(commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ){
  shim_init();
  PFN_vkCmdDispatch fn=(PFN_vkCmdDispatch)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdDispatch");
  if(fn) fn(commandBuffer, groupCountX, groupCountY, groupCountZ);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset){
  shim_init();
  PFN_vkCmdDispatchIndirect fn=(PFN_vkCmdDispatchIndirect)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdDispatchIndirect");
  if(fn) fn(commandBuffer, buffer, offset);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance){
  shim_init();
  PFN_vkCmdDraw fn=(PFN_vkCmdDraw)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdDraw");
  if(fn) fn(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance){
  shim_init();
  PFN_vkCmdDrawIndexed fn=(PFN_vkCmdDrawIndexed)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdDrawIndexed");
  if(fn) fn(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride){
  shim_init();
  PFN_vkCmdDrawIndexedIndirect fn=(PFN_vkCmdDrawIndexedIndirect)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdDrawIndexedIndirect");
  if(fn) fn(commandBuffer, buffer, offset, drawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride){
  shim_init();
  PFN_vkCmdDrawIndirect fn=(PFN_vkCmdDrawIndirect)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdDrawIndirect");
  if(fn) fn(commandBuffer, buffer, offset, drawCount, stride);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query){
  shim_init();
  PFN_vkCmdEndQuery fn=(PFN_vkCmdEndQuery)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdEndQuery");
  if(fn) fn(commandBuffer, queryPool, query);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer commandBuffer){
  shim_init();
  PFN_vkCmdEndRenderPass fn=(PFN_vkCmdEndRenderPass)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdEndRenderPass");
  if(fn) fn(commandBuffer);
}
VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data){
  shim_init();
  PFN_vkCmdFillBuffer fn=(PFN_vkCmdFillBuffer)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdFillBuffer");
  if(fn) fn(commandBuffer, dstBuffer, dstOffset, size, data);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers){
  shim_init();
  PFN_vkCmdPipelineBarrier fn=(PFN_vkCmdPipelineBarrier)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdPipelineBarrier");
  if(fn) fn(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues){
  shim_init();
  PFN_vkCmdPushConstants fn=(PFN_vkCmdPushConstants)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdPushConstants");
  if(fn) fn(commandBuffer, layout, stageFlags, offset, size, pValues);
}
VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount){
  shim_init();
  PFN_vkCmdResetQueryPool fn=(PFN_vkCmdResetQueryPool)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdResetQueryPool");
  if(fn) fn(commandBuffer, queryPool, firstQuery, queryCount);
}
VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions){
  shim_init();
  PFN_vkCmdResolveImage fn=(PFN_vkCmdResolveImage)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdResolveImage");
  if(fn) fn(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]){
  shim_init();
  PFN_vkCmdSetBlendConstants fn=(PFN_vkCmdSetBlendConstants)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdSetBlendConstants");
  if(fn) fn(commandBuffer, blendConstants);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor){
  shim_init();
  PFN_vkCmdSetDepthBias fn=(PFN_vkCmdSetDepthBias)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdSetDepthBias");
  if(fn) fn(commandBuffer, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth){
  shim_init();
  PFN_vkCmdSetLineWidth fn=(PFN_vkCmdSetLineWidth)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdSetLineWidth");
  if(fn) fn(commandBuffer, lineWidth);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors){
  shim_init();
  PFN_vkCmdSetScissor fn=(PFN_vkCmdSetScissor)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdSetScissor");
  if(fn) fn(commandBuffer, firstScissor, scissorCount, pScissors);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask){
  shim_init();
  PFN_vkCmdSetStencilCompareMask fn=(PFN_vkCmdSetStencilCompareMask)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdSetStencilCompareMask");
  if(fn) fn(commandBuffer, faceMask, compareMask);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference){
  shim_init();
  PFN_vkCmdSetStencilReference fn=(PFN_vkCmdSetStencilReference)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdSetStencilReference");
  if(fn) fn(commandBuffer, faceMask, reference);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask){
  shim_init();
  PFN_vkCmdSetStencilWriteMask fn=(PFN_vkCmdSetStencilWriteMask)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdSetStencilWriteMask");
  if(fn) fn(commandBuffer, faceMask, writeMask);
}
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports){
  shim_init();
  PFN_vkCmdSetViewport fn=(PFN_vkCmdSetViewport)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdSetViewport");
  if(fn) fn(commandBuffer, firstViewport, viewportCount, pViewports);
}
VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query){
  shim_init();
  PFN_vkCmdWriteTimestamp fn=(PFN_vkCmdWriteTimestamp)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkCmdWriteTimestamp");
  if(fn) fn(commandBuffer, pipelineStage, queryPool, query);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateAndroidSurfaceKHR(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface)
{
  shim_init();
  PFN_vkCreateAndroidSurfaceKHR fn=(PFN_vkCreateAndroidSurfaceKHR)gp_inst(instance,"vkCreateAndroidSurfaceKHR");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(instance, pCreateInfo, pAllocator, pSurface);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer)
{
  shim_init();
  PFN_vkCreateBuffer fn=(PFN_vkCreateBuffer)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateBuffer");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pBuffer);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView)
{
  shim_init();
  PFN_vkCreateBufferView fn=(PFN_vkCreateBufferView)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateBufferView");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pView);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool)
{
  shim_init();
  PFN_vkCreateCommandPool fn=(PFN_vkCreateCommandPool)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateCommandPool");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pCommandPool);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
{
  shim_init();
  PFN_vkCreateComputePipelines fn=(PFN_vkCreateComputePipelines)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateComputePipelines");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool)
{
  shim_init();
  PFN_vkCreateDescriptorPool fn=(PFN_vkCreateDescriptorPool)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateDescriptorPool");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pDescriptorPool);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout)
{
  shim_init();
  PFN_vkCreateDescriptorSetLayout fn=(PFN_vkCreateDescriptorSetLayout)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateDescriptorSetLayout");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pSetLayout);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
  shim_init();
  /* ★ 把驱动不支持的 feature 位屏蔽掉，避免 vkCreateDevice 因虚假支持而失败 */
  VkPhysicalDeviceFeatures _sup, _loc; VkDeviceCreateInfo _ci; int _patched=0;
  if(pCreateInfo){
    memset(&_sup,0,sizeof(_sup)); memset(&_loc,0,sizeof(_loc));
    PFN_vkGetPhysicalDeviceFeatures _gf=(PFN_vkGetPhysicalDeviceFeatures)gipa_pd(physicalDevice,"vkGetPhysicalDeviceFeatures");
    if(_gf){ _gf(physicalDevice,&_sup);
      _ci=*pCreateInfo;
      if(pCreateInfo->pEnabledFeatures){
        uint32_t* w=(uint32_t*)&_loc; const uint32_t* s=(const uint32_t*)&_sup; const uint32_t* r=(const uint32_t*)pCreateInfo->pEnabledFeatures;
        uint32_t x=0; for(unsigned i=0;i<sizeof(_loc)/4;i++){ w[i]=r[i]&s[i]; x|=r[i]&~s[i]; }
        _ci.pEnabledFeatures=&_loc; _patched=1;
        FLOG("vkCreateDevice: feature mask applied, masked-out bits=0x%08x", x);
      }
    }
  }
  { if(1){ uint32_t ec=(pCreateInfo?pCreateInfo->enabledExtensionCount:0);
      { PFN_vkGetPhysicalDeviceFeatures gf=(PFN_vkGetPhysicalDeviceFeatures)gipa_pd(physicalDevice,"vkGetPhysicalDeviceFeatures");
        VkPhysicalDeviceFeatures sup; memset(&sup,0,sizeof(sup));
        if(gf) gf(physicalDevice,&sup);
        const uint32_t* w=(const uint32_t*)(pCreateInfo?pCreateInfo->pEnabledFeatures:0);
        const uint32_t* s=(const uint32_t*)&sup;
        if(w) for(int i=0;i<6;i++) FLOG("  feat[%d] want=0x%08x sup=0x%08x xor=0x%08x", i, w[i], s[i], w[i]&~s[i]);
        else  FLOG("  feat: pEnabledFeatures=NULL (默认全关)"); }
      FLOG("vkCreateDevice: ext=%u", ec);
      for(uint32_t i=0;i<ec && i<40;i++) FLOG("  want ext: %s", pCreateInfo->ppEnabledExtensionNames[i]);
      FLOG("  features: %p", (void*)(pCreateInfo?pCreateInfo->pEnabledFeatures:0)); } }
  PFN_vkCreateDevice fn=(PFN_vkCreateDevice)gipa_pd(physicalDevice,"vkCreateDevice");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(physicalDevice, _patched ? &_ci : pCreateInfo, pAllocator, pDevice);
  if(res==VK_SUCCESS && pDevice) g_dev=*pDevice;
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence)
{
  shim_init();
  PFN_vkCreateFence fn=(PFN_vkCreateFence)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateFence");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pFence);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer)
{
  shim_init();
  PFN_vkCreateFramebuffer fn=(PFN_vkCreateFramebuffer)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateFramebuffer");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pFramebuffer);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
{
  shim_init();
  PFN_vkCreateGraphicsPipelines fn=(PFN_vkCreateGraphicsPipelines)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateGraphicsPipelines");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage)
{
  shim_init();
  PFN_vkCreateImage fn=(PFN_vkCreateImage)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateImage");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pImage);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView)
{
  shim_init();
  PFN_vkCreateImageView fn=(PFN_vkCreateImageView)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateImageView");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pView);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance)
{
  shim_init();
  PFN_vkCreateInstance fn=(PFN_vkCreateInstance)gp_inst(pCreateInfo,"vkCreateInstance");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(pCreateInfo, pAllocator, pInstance);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache)
{
  shim_init();
  PFN_vkCreatePipelineCache fn=(PFN_vkCreatePipelineCache)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreatePipelineCache");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pPipelineCache);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout)
{
  shim_init();
  PFN_vkCreatePipelineLayout fn=(PFN_vkCreatePipelineLayout)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreatePipelineLayout");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pPipelineLayout);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool)
{
  shim_init();
  PFN_vkCreateQueryPool fn=(PFN_vkCreateQueryPool)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateQueryPool");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pQueryPool);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass)
{
  shim_init();
  PFN_vkCreateRenderPass fn=(PFN_vkCreateRenderPass)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateRenderPass");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pRenderPass);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler)
{
  shim_init();
  PFN_vkCreateSampler fn=(PFN_vkCreateSampler)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateSampler");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pSampler);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore)
{
  shim_init();
  PFN_vkCreateSemaphore fn=(PFN_vkCreateSemaphore)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateSemaphore");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pSemaphore);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule)
{
  shim_init();
  PFN_vkCreateShaderModule fn=(PFN_vkCreateShaderModule)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateShaderModule");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, pCreateInfo, pAllocator, pShaderModule);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
  shim_init();
  PFN_vkCreateSwapchainKHR fn=(PFN_vkCreateSwapchainKHR)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkCreateSwapchainKHR");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  if(pCreateInfo) FLOG("CreateSwapchain: surf=%p usage=0x%x fmt=%d cs=%d pm=%d alpha=0x%x layers=%u old=%p", (void*)pCreateInfo->surface, pCreateInfo->imageUsage, (int)pCreateInfo->imageFormat, (int)pCreateInfo->imageColorSpace, (int)pCreateInfo->presentMode, pCreateInfo->compositeAlpha, pCreateInfo->imageArrayLayers, (void*)pCreateInfo->oldSwapchain);
res=fn(device, pCreateInfo, pAllocator, pSwapchain);
  return res;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyBuffer fn=(PFN_vkDestroyBuffer)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyBuffer");
  if(fn) fn(device, buffer, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyBufferView fn=(PFN_vkDestroyBufferView)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyBufferView");
  if(fn) fn(device, bufferView, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyCommandPool fn=(PFN_vkDestroyCommandPool)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyCommandPool");
  if(fn) fn(device, commandPool, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyDescriptorPool fn=(PFN_vkDestroyDescriptorPool)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyDescriptorPool");
  if(fn) fn(device, descriptorPool, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyDescriptorSetLayout fn=(PFN_vkDestroyDescriptorSetLayout)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyDescriptorSetLayout");
  if(fn) fn(device, descriptorSetLayout, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyDevice fn=(PFN_vkDestroyDevice)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyDevice");
  if(fn) fn(device, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyFence fn=(PFN_vkDestroyFence)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyFence");
  if(fn) fn(device, fence, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyFramebuffer fn=(PFN_vkDestroyFramebuffer)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyFramebuffer");
  if(fn) fn(device, framebuffer, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyImage fn=(PFN_vkDestroyImage)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyImage");
  if(fn) fn(device, image, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyImageView fn=(PFN_vkDestroyImageView)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyImageView");
  if(fn) fn(device, imageView, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyInstance fn=(PFN_vkDestroyInstance)gp_inst(instance,"vkDestroyInstance");
  if(fn) fn(instance, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyPipeline fn=(PFN_vkDestroyPipeline)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyPipeline");
  if(fn) fn(device, pipeline, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyPipelineCache fn=(PFN_vkDestroyPipelineCache)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyPipelineCache");
  if(fn) fn(device, pipelineCache, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyPipelineLayout fn=(PFN_vkDestroyPipelineLayout)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyPipelineLayout");
  if(fn) fn(device, pipelineLayout, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyQueryPool fn=(PFN_vkDestroyQueryPool)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyQueryPool");
  if(fn) fn(device, queryPool, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyRenderPass fn=(PFN_vkDestroyRenderPass)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyRenderPass");
  if(fn) fn(device, renderPass, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroySampler fn=(PFN_vkDestroySampler)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroySampler");
  if(fn) fn(device, sampler, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroySemaphore fn=(PFN_vkDestroySemaphore)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroySemaphore");
  if(fn) fn(device, semaphore, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroyShaderModule fn=(PFN_vkDestroyShaderModule)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroyShaderModule");
  if(fn) fn(device, shaderModule, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroySurfaceKHR fn=(PFN_vkDestroySurfaceKHR)gp_inst(instance,"vkDestroySurfaceKHR");
  if(fn) fn(instance, surface, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkDestroySwapchainKHR fn=(PFN_vkDestroySwapchainKHR)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDestroySwapchainKHR");
  if(fn) fn(device, swapchain, pAllocator);
}
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device)
{
  shim_init();
  PFN_vkDeviceWaitIdle fn=(PFN_vkDeviceWaitIdle)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkDeviceWaitIdle");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
  shim_init();
  PFN_vkEndCommandBuffer fn=(PFN_vkEndCommandBuffer)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkEndCommandBuffer");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(commandBuffer);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
  shim_init();
  PFN_vkEnumerateDeviceExtensionProperties fn=(PFN_vkEnumerateDeviceExtensionProperties)gipa_pd(physicalDevice,"vkEnumerateDeviceExtensionProperties");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(physicalDevice, pLayerName, pPropertyCount, pProperties);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties)
{
  shim_init();
  PFN_vkEnumerateInstanceExtensionProperties fn=(PFN_vkEnumerateInstanceExtensionProperties)gp_inst(NULL,"vkEnumerateInstanceExtensionProperties");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(pLayerName, pPropertyCount, pProperties);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t* c, VkLayerProperties* p)
{ if(p) return VK_ERROR_INITIALIZATION_FAILED; if(c) *c=0; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices){
  shim_init();
  if(!g_inst) g_inst=instance;
  PFN_vkEnumeratePhysicalDevices fn=(PFN_vkEnumeratePhysicalDevices)gp_inst(instance,"vkEnumeratePhysicalDevices");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(instance, pPhysicalDeviceCount, pPhysicalDevices);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges)
{
  shim_init();
  PFN_vkFlushMappedMemoryRanges fn=(PFN_vkFlushMappedMemoryRanges)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkFlushMappedMemoryRanges");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, memoryRangeCount, pMemoryRanges);
  return res;
}
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers){
  shim_init();
  PFN_vkFreeCommandBuffers fn=(PFN_vkFreeCommandBuffers)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkFreeCommandBuffers");
  if(fn) fn(device, commandPool, commandBufferCount, pCommandBuffers);
}
VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets)
{
  shim_init();
  PFN_vkFreeDescriptorSets fn=(PFN_vkFreeDescriptorSets)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkFreeDescriptorSets");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, descriptorPool, descriptorSetCount, pDescriptorSets);
  return res;
}
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator){
  shim_init();
  PFN_vkFreeMemory fn=(PFN_vkFreeMemory)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkFreeMemory");
  if(fn) fn(device, memory, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements){
  shim_init();
  PFN_vkGetBufferMemoryRequirements fn=(PFN_vkGetBufferMemoryRequirements)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkGetBufferMemoryRequirements");
  if(fn) fn(device, buffer, pMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue){
  shim_init();
  PFN_vkGetDeviceQueue fn=(PFN_vkGetDeviceQueue)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkGetDeviceQueue");
  if(fn) fn(device, queueFamilyIndex, queueIndex, pQueue);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device, VkFence fence)
{
  shim_init();
  PFN_vkGetFenceStatus fn=(PFN_vkGetFenceStatus)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkGetFenceStatus");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, fence);
  return res;
}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements){
  shim_init();
  PFN_vkGetImageMemoryRequirements fn=(PFN_vkGetImageMemoryRequirements)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkGetImageMemoryRequirements");
  if(fn) fn(device, image, pMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures){
  shim_init();
  PFN_vkGetPhysicalDeviceFeatures fn=(PFN_vkGetPhysicalDeviceFeatures)gipa_pd(physicalDevice,"vkGetPhysicalDeviceFeatures");
  if(fn) fn(physicalDevice, pFeatures);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties){
  shim_init();
  PFN_vkGetPhysicalDeviceFormatProperties fn=(PFN_vkGetPhysicalDeviceFormatProperties)gipa_pd(physicalDevice,"vkGetPhysicalDeviceFormatProperties");
  if(fn) fn(physicalDevice, format, pFormatProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties)
{
  shim_init();
  PFN_vkGetPhysicalDeviceImageFormatProperties fn=(PFN_vkGetPhysicalDeviceImageFormatProperties)gipa_pd(physicalDevice,"vkGetPhysicalDeviceImageFormatProperties");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(physicalDevice, format, type, tiling, usage, flags, pImageFormatProperties);
  return res;
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties){
  shim_init();
  PFN_vkGetPhysicalDeviceMemoryProperties fn=(PFN_vkGetPhysicalDeviceMemoryProperties)gipa_pd(physicalDevice,"vkGetPhysicalDeviceMemoryProperties");
  if(fn) fn(physicalDevice, pMemoryProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties){
  shim_init();
  PFN_vkGetPhysicalDeviceProperties fn=(PFN_vkGetPhysicalDeviceProperties)gipa_pd(physicalDevice,"vkGetPhysicalDeviceProperties");
  if(fn) fn(physicalDevice, pProperties);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties){
  shim_init();
  PFN_vkGetPhysicalDeviceQueueFamilyProperties fn=(PFN_vkGetPhysicalDeviceQueueFamilyProperties)gipa_pd(physicalDevice,"vkGetPhysicalDeviceQueueFamilyProperties");
  if(fn) fn(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
  { PFN_vkGetPhysicalDeviceQueueFamilyProperties _f2=
      (PFN_vkGetPhysicalDeviceQueueFamilyProperties)(g_gipa?g_gipa(g_inst,"vkGetPhysicalDeviceQueueFamilyProperties"):0);
    FLOG("QFam-diag: fn=%p viaInstanceGIPA=%p pdpa=%p gipa=%p g_inst=%p gdpa=%p",
         (void*)(uintptr_t)fn,(void*)(uintptr_t)_f2,(void*)g_pdpa,(void*)g_gipa,(void*)g_inst,(void*)g_gdpa); }
  if(pQueueFamilyPropertyCount) FLOG("QueueFamilyProperties: count=%u", *pQueueFamilyPropertyCount);
  if(pQueueFamilyProperties && pQueueFamilyPropertyCount && *pQueueFamilyPropertyCount<=8)
    for(uint32_t _i=0;_i<*pQueueFamilyPropertyCount;_i++)
      FLOG("  family[%u] flags=0x%x queues=%u", _i, pQueueFamilyProperties[_i].queueFlags, pQueueFamilyProperties[_i].queueCount);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
{
  shim_init();
  PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fn=(PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)gipa_pd(physicalDevice,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(physicalDevice, surface, pSurfaceCapabilities);
  if(pSurfaceCapabilities) FLOG("SurfaceCaps: min=%u max=%u cur=%dx%d usage=0x%x alpha=0x%x presentModes_ok", pSurfaceCapabilities->minImageCount, pSurfaceCapabilities->maxImageCount, pSurfaceCapabilities->currentExtent.width, pSurfaceCapabilities->currentExtent.height, pSurfaceCapabilities->supportedUsageFlags, pSurfaceCapabilities->supportedCompositeAlpha);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats)
{
  shim_init();
  PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fn=(PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)gipa_pd(physicalDevice,"vkGetPhysicalDeviceSurfaceFormatsKHR");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes)
{
  shim_init();
  PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fn=(PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)gipa_pd(physicalDevice,"vkGetPhysicalDeviceSurfacePresentModesKHR");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(physicalDevice, surface, pPresentModeCount, pPresentModes);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported)
{
  shim_init();
  PFN_vkGetPhysicalDeviceSurfaceSupportKHR fn=(PFN_vkGetPhysicalDeviceSurfaceSupportKHR)gipa_pd(physicalDevice,"vkGetPhysicalDeviceSurfaceSupportKHR");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(physicalDevice, queueFamilyIndex, surface, pSupported);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags)
{
  shim_init();
  PFN_vkGetQueryPoolResults fn=(PFN_vkGetQueryPoolResults)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkGetQueryPoolResults");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, queryPool, firstQuery, queryCount, dataSize, pData, stride, flags);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages)
{
  shim_init();
  PFN_vkGetSwapchainImagesKHR fn=(PFN_vkGetSwapchainImagesKHR)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkGetSwapchainImagesKHR");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, swapchain, pSwapchainImageCount, pSwapchainImages);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges)
{
  shim_init();
  PFN_vkInvalidateMappedMemoryRanges fn=(PFN_vkInvalidateMappedMemoryRanges)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkInvalidateMappedMemoryRanges");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, memoryRangeCount, pMemoryRanges);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData)
{
  shim_init();
  PFN_vkMapMemory fn=(PFN_vkMapMemory)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkMapMemory");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, memory, offset, size, flags, ppData);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
  shim_init();
  PFN_vkQueuePresentKHR fn=(PFN_vkQueuePresentKHR)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)queue,"vkQueuePresentKHR");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(queue, pPresentInfo);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
{
  shim_init();
  PFN_vkQueueSubmit fn=(PFN_vkQueueSubmit)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)queue,"vkQueueSubmit");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(queue, submitCount, pSubmits, fence);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue)
{
  shim_init();
  PFN_vkQueueWaitIdle fn=(PFN_vkQueueWaitIdle)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)queue,"vkQueueWaitIdle");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(queue);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
{
  shim_init();
  PFN_vkResetCommandBuffer fn=(PFN_vkResetCommandBuffer)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)commandBuffer,"vkResetCommandBuffer");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(commandBuffer, flags);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags)
{
  shim_init();
  PFN_vkResetDescriptorPool fn=(PFN_vkResetDescriptorPool)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkResetDescriptorPool");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, descriptorPool, flags);
  return res;
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences)
{
  shim_init();
  PFN_vkResetFences fn=(PFN_vkResetFences)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkResetFences");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, fenceCount, pFences);
  return res;
}
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice device, VkDeviceMemory memory){
  shim_init();
  PFN_vkUnmapMemory fn=(PFN_vkUnmapMemory)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkUnmapMemory");
  if(fn) fn(device, memory);
}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies){
  shim_init();
  PFN_vkUpdateDescriptorSets fn=(PFN_vkUpdateDescriptorSets)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkUpdateDescriptorSets");
  if(fn) fn(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout)
{
  shim_init();
  PFN_vkWaitForFences fn=(PFN_vkWaitForFences)gp_dev(g_dev?g_dev:(VkDevice)(uintptr_t)device,"vkWaitForFences");
  VkResult res;
  if(!fn) return VK_ERROR_INITIALIZATION_FAILED;
  res=fn(device, fenceCount, pFences, waitAll, timeout);
  return res;
}

typedef struct { const char* n; PFN_vkVoidFunction f; } ShimEntry;
static const ShimEntry g_table[] = {
  { "vkAcquireNextImageKHR", (PFN_vkVoidFunction)vkAcquireNextImageKHR },
  { "vkAllocateCommandBuffers", (PFN_vkVoidFunction)vkAllocateCommandBuffers },
  { "vkAllocateDescriptorSets", (PFN_vkVoidFunction)vkAllocateDescriptorSets },
  { "vkAllocateMemory", (PFN_vkVoidFunction)vkAllocateMemory },
  { "vkBeginCommandBuffer", (PFN_vkVoidFunction)vkBeginCommandBuffer },
  { "vkBindBufferMemory", (PFN_vkVoidFunction)vkBindBufferMemory },
  { "vkBindImageMemory", (PFN_vkVoidFunction)vkBindImageMemory },
  { "vkCmdBeginQuery", (PFN_vkVoidFunction)vkCmdBeginQuery },
  { "vkCmdBeginRenderPass", (PFN_vkVoidFunction)vkCmdBeginRenderPass },
  { "vkCmdBindDescriptorSets", (PFN_vkVoidFunction)vkCmdBindDescriptorSets },
  { "vkCmdBindIndexBuffer", (PFN_vkVoidFunction)vkCmdBindIndexBuffer },
  { "vkCmdBindPipeline", (PFN_vkVoidFunction)vkCmdBindPipeline },
  { "vkCmdBindVertexBuffers", (PFN_vkVoidFunction)vkCmdBindVertexBuffers },
  { "vkCmdBlitImage", (PFN_vkVoidFunction)vkCmdBlitImage },
  { "vkCmdClearAttachments", (PFN_vkVoidFunction)vkCmdClearAttachments },
  { "vkCmdClearColorImage", (PFN_vkVoidFunction)vkCmdClearColorImage },
  { "vkCmdClearDepthStencilImage", (PFN_vkVoidFunction)vkCmdClearDepthStencilImage },
  { "vkCmdCopyBuffer", (PFN_vkVoidFunction)vkCmdCopyBuffer },
  { "vkCmdCopyBufferToImage", (PFN_vkVoidFunction)vkCmdCopyBufferToImage },
  { "vkCmdCopyImage", (PFN_vkVoidFunction)vkCmdCopyImage },
  { "vkCmdCopyImageToBuffer", (PFN_vkVoidFunction)vkCmdCopyImageToBuffer },
  { "vkCmdDispatch", (PFN_vkVoidFunction)vkCmdDispatch },
  { "vkCmdDispatchIndirect", (PFN_vkVoidFunction)vkCmdDispatchIndirect },
  { "vkCmdDraw", (PFN_vkVoidFunction)vkCmdDraw },
  { "vkCmdDrawIndexed", (PFN_vkVoidFunction)vkCmdDrawIndexed },
  { "vkCmdDrawIndexedIndirect", (PFN_vkVoidFunction)vkCmdDrawIndexedIndirect },
  { "vkCmdDrawIndirect", (PFN_vkVoidFunction)vkCmdDrawIndirect },
  { "vkCmdEndQuery", (PFN_vkVoidFunction)vkCmdEndQuery },
  { "vkCmdEndRenderPass", (PFN_vkVoidFunction)vkCmdEndRenderPass },
  { "vkCmdFillBuffer", (PFN_vkVoidFunction)vkCmdFillBuffer },
  { "vkCmdPipelineBarrier", (PFN_vkVoidFunction)vkCmdPipelineBarrier },
  { "vkCmdPushConstants", (PFN_vkVoidFunction)vkCmdPushConstants },
  { "vkCmdResetQueryPool", (PFN_vkVoidFunction)vkCmdResetQueryPool },
  { "vkCmdResolveImage", (PFN_vkVoidFunction)vkCmdResolveImage },
  { "vkCmdSetBlendConstants", (PFN_vkVoidFunction)vkCmdSetBlendConstants },
  { "vkCmdSetDepthBias", (PFN_vkVoidFunction)vkCmdSetDepthBias },
  { "vkCmdSetLineWidth", (PFN_vkVoidFunction)vkCmdSetLineWidth },
  { "vkCmdSetScissor", (PFN_vkVoidFunction)vkCmdSetScissor },
  { "vkCmdSetStencilCompareMask", (PFN_vkVoidFunction)vkCmdSetStencilCompareMask },
  { "vkCmdSetStencilReference", (PFN_vkVoidFunction)vkCmdSetStencilReference },
  { "vkCmdSetStencilWriteMask", (PFN_vkVoidFunction)vkCmdSetStencilWriteMask },
  { "vkCmdSetViewport", (PFN_vkVoidFunction)vkCmdSetViewport },
  { "vkCmdWriteTimestamp", (PFN_vkVoidFunction)vkCmdWriteTimestamp },
  { "vkCreateAndroidSurfaceKHR", (PFN_vkVoidFunction)vkCreateAndroidSurfaceKHR },
  { "vkCreateBuffer", (PFN_vkVoidFunction)vkCreateBuffer },
  { "vkCreateBufferView", (PFN_vkVoidFunction)vkCreateBufferView },
  { "vkCreateCommandPool", (PFN_vkVoidFunction)vkCreateCommandPool },
  { "vkCreateComputePipelines", (PFN_vkVoidFunction)vkCreateComputePipelines },
  { "vkCreateDescriptorPool", (PFN_vkVoidFunction)vkCreateDescriptorPool },
  { "vkCreateDescriptorSetLayout", (PFN_vkVoidFunction)vkCreateDescriptorSetLayout },
  { "vkCreateDevice", (PFN_vkVoidFunction)vkCreateDevice },
  { "vkCreateFence", (PFN_vkVoidFunction)vkCreateFence },
  { "vkCreateFramebuffer", (PFN_vkVoidFunction)vkCreateFramebuffer },
  { "vkCreateGraphicsPipelines", (PFN_vkVoidFunction)vkCreateGraphicsPipelines },
  { "vkCreateImage", (PFN_vkVoidFunction)vkCreateImage },
  { "vkCreateImageView", (PFN_vkVoidFunction)vkCreateImageView },
  { "vkCreateInstance", (PFN_vkVoidFunction)vkCreateInstance },
  { "vkCreatePipelineCache", (PFN_vkVoidFunction)vkCreatePipelineCache },
  { "vkCreatePipelineLayout", (PFN_vkVoidFunction)vkCreatePipelineLayout },
  { "vkCreateQueryPool", (PFN_vkVoidFunction)vkCreateQueryPool },
  { "vkCreateRenderPass", (PFN_vkVoidFunction)vkCreateRenderPass },
  { "vkCreateSampler", (PFN_vkVoidFunction)vkCreateSampler },
  { "vkCreateSemaphore", (PFN_vkVoidFunction)vkCreateSemaphore },
  { "vkCreateShaderModule", (PFN_vkVoidFunction)vkCreateShaderModule },
  { "vkCreateSwapchainKHR", (PFN_vkVoidFunction)vkCreateSwapchainKHR },
  { "vkDestroyBuffer", (PFN_vkVoidFunction)vkDestroyBuffer },
  { "vkDestroyBufferView", (PFN_vkVoidFunction)vkDestroyBufferView },
  { "vkDestroyCommandPool", (PFN_vkVoidFunction)vkDestroyCommandPool },
  { "vkDestroyDescriptorPool", (PFN_vkVoidFunction)vkDestroyDescriptorPool },
  { "vkDestroyDescriptorSetLayout", (PFN_vkVoidFunction)vkDestroyDescriptorSetLayout },
  { "vkDestroyDevice", (PFN_vkVoidFunction)vkDestroyDevice },
  { "vkDestroyFence", (PFN_vkVoidFunction)vkDestroyFence },
  { "vkDestroyFramebuffer", (PFN_vkVoidFunction)vkDestroyFramebuffer },
  { "vkDestroyImage", (PFN_vkVoidFunction)vkDestroyImage },
  { "vkDestroyImageView", (PFN_vkVoidFunction)vkDestroyImageView },
  { "vkDestroyInstance", (PFN_vkVoidFunction)vkDestroyInstance },
  { "vkDestroyPipeline", (PFN_vkVoidFunction)vkDestroyPipeline },
  { "vkDestroyPipelineCache", (PFN_vkVoidFunction)vkDestroyPipelineCache },
  { "vkDestroyPipelineLayout", (PFN_vkVoidFunction)vkDestroyPipelineLayout },
  { "vkDestroyQueryPool", (PFN_vkVoidFunction)vkDestroyQueryPool },
  { "vkDestroyRenderPass", (PFN_vkVoidFunction)vkDestroyRenderPass },
  { "vkDestroySampler", (PFN_vkVoidFunction)vkDestroySampler },
  { "vkDestroySemaphore", (PFN_vkVoidFunction)vkDestroySemaphore },
  { "vkDestroyShaderModule", (PFN_vkVoidFunction)vkDestroyShaderModule },
  { "vkDestroySurfaceKHR", (PFN_vkVoidFunction)vkDestroySurfaceKHR },
  { "vkDestroySwapchainKHR", (PFN_vkVoidFunction)vkDestroySwapchainKHR },
  { "vkDeviceWaitIdle", (PFN_vkVoidFunction)vkDeviceWaitIdle },
  { "vkEndCommandBuffer", (PFN_vkVoidFunction)vkEndCommandBuffer },
  { "vkEnumerateDeviceExtensionProperties", (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties },
  { "vkEnumerateInstanceExtensionProperties", (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties },
  { "vkEnumerateInstanceLayerProperties", (PFN_vkVoidFunction)vkEnumerateInstanceLayerProperties },
  { "vkEnumeratePhysicalDevices", (PFN_vkVoidFunction)vkEnumeratePhysicalDevices },
  { "vkFlushMappedMemoryRanges", (PFN_vkVoidFunction)vkFlushMappedMemoryRanges },
  { "vkFreeCommandBuffers", (PFN_vkVoidFunction)vkFreeCommandBuffers },
  { "vkFreeDescriptorSets", (PFN_vkVoidFunction)vkFreeDescriptorSets },
  { "vkFreeMemory", (PFN_vkVoidFunction)vkFreeMemory },
  { "vkGetBufferMemoryRequirements", (PFN_vkVoidFunction)vkGetBufferMemoryRequirements },
  { "vkGetDeviceQueue", (PFN_vkVoidFunction)vkGetDeviceQueue },
  { "vkGetFenceStatus", (PFN_vkVoidFunction)vkGetFenceStatus },
  { "vkGetImageMemoryRequirements", (PFN_vkVoidFunction)vkGetImageMemoryRequirements },
  { "vkGetPhysicalDeviceFeatures", (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures },
  { "vkGetPhysicalDeviceFormatProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties },
  { "vkGetPhysicalDeviceImageFormatProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties },
  { "vkGetPhysicalDeviceMemoryProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties },
  { "vkGetPhysicalDeviceProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties },
  { "vkGetPhysicalDeviceQueueFamilyProperties", (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties },
  { "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceCapabilitiesKHR },
  { "vkGetPhysicalDeviceSurfaceFormatsKHR", (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceFormatsKHR },
  { "vkGetPhysicalDeviceSurfacePresentModesKHR", (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfacePresentModesKHR },
  { "vkGetPhysicalDeviceSurfaceSupportKHR", (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceSupportKHR },
  { "vkGetQueryPoolResults", (PFN_vkVoidFunction)vkGetQueryPoolResults },
  { "vkGetSwapchainImagesKHR", (PFN_vkVoidFunction)vkGetSwapchainImagesKHR },
  { "vkInvalidateMappedMemoryRanges", (PFN_vkVoidFunction)vkInvalidateMappedMemoryRanges },
  { "vkMapMemory", (PFN_vkVoidFunction)vkMapMemory },
  { "vkQueuePresentKHR", (PFN_vkVoidFunction)vkQueuePresentKHR },
  { "vkQueueSubmit", (PFN_vkVoidFunction)vkQueueSubmit },
  { "vkQueueWaitIdle", (PFN_vkVoidFunction)vkQueueWaitIdle },
  { "vkResetCommandBuffer", (PFN_vkVoidFunction)vkResetCommandBuffer },
  { "vkResetDescriptorPool", (PFN_vkVoidFunction)vkResetDescriptorPool },
  { "vkResetFences", (PFN_vkVoidFunction)vkResetFences },
  { "vkUnmapMemory", (PFN_vkVoidFunction)vkUnmapMemory },
  { "vkUpdateDescriptorSets", (PFN_vkVoidFunction)vkUpdateDescriptorSets },
  { "vkWaitForFences", (PFN_vkVoidFunction)vkWaitForFences },
};
static const int g_table_n = (int)(sizeof(g_table)/sizeof(g_table[0]));
static PFN_vkVoidFunction shim_lookup(const char* n){ for(int i=0;i<g_table_n;i++) if(!strcmp(g_table[i].n,n)) return g_table[i].f; return 0; }

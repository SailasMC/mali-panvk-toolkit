#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>
typedef VkResult (*PFN_ci)(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
typedef VkResult (*PFN_epd)(VkInstance, uint32_t*, VkPhysicalDevice*);
typedef void (*PFN_gpdp)(VkPhysicalDevice, VkPhysicalDeviceProperties*);
typedef void (*PFN_gpf)(VkPhysicalDevice, VkPhysicalDeviceFeatures*);
typedef VkResult (*PFN_edep)(VkPhysicalDevice, const char*, uint32_t*, VkExtensionProperties*);
typedef PFN_vkVoidFunction (*PFN_gipa)(VkInstance, const char*);
int main(int argc, char**argv){
  if(argc<2){printf("usage: %s <driver.so>\n",argv[0]);return 64;}
  void*h=dlopen(argv[1],RTLD_NOW);
  if(!h){printf("[1] dlopen 失败: %s\n",dlerror());return 1;}
  PFN_gipa g=(PFN_gipa)dlsym(h,"vk_icdGetInstanceProcAddr");
  if(!g){printf("[2] 非 ICD 形态（无 vk_icdGetInstanceProcAddr）\n");return 2;}
  VkApplicationInfo app={0}; app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion=VK_API_VERSION_1_0; app.pApplicationName="zink_check";
  VkInstanceCreateInfo ci={0}; ci.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; ci.pApplicationInfo=&app;
  VkInstance inst; PFN_ci create=(PFN_ci)g(NULL,"vkCreateInstance");
  if(!create||create(&ci,NULL,&inst)!=VK_SUCCESS){printf("[3] vkCreateInstance 失败\n");return 3;}
  uint32_t n=0; PFN_epd ed=(PFN_epd)g(inst,"vkEnumeratePhysicalDevices"); ed(inst,&n,NULL);
  VkPhysicalDevice d; ed(inst,&n,&d);
  PFN_gpdp gp=(PFN_gpdp)g(inst,"vkGetPhysicalDeviceProperties");
  VkPhysicalDeviceProperties p; memset(&p,0,sizeof p); gp(d,&p);
  printf("deviceName    : %s\n",p.deviceName);
  printf("apiVersion    : %u.%u.%u\n",VK_VERSION_MAJOR(p.apiVersion),VK_VERSION_MINOR(p.apiVersion),VK_VERSION_PATCH(p.apiVersion));
  PFN_gpf gf=(PFN_gpf)g(inst,"vkGetPhysicalDeviceFeatures");
  VkPhysicalDeviceFeatures f; memset(&f,0,sizeof f); if(gf) gf(d,&f);
  printf("\n== Zink 关心的 feature ==\n");
  printf("  logicOp                             %s\n", f.logicOp?"YES":"NO");
  printf("  fillModeNonSolid                    %s\n", f.fillModeNonSolid?"YES":"NO");
  printf("  depthClamp                          %s\n", f.depthClamp?"YES":"NO");
  printf("  independentBlend                    %s\n", f.independentBlend?"YES":"NO");
  printf("  dualSrcBlend                        %s\n", f.dualSrcBlend?"YES":"NO");
  printf("  robustBufferAccess                  %s\n", f.robustBufferAccess?"YES":"NO");
  printf("  shaderStorageImageWriteWithoutFormat %s\n", f.shaderStorageImageWriteWithoutFormat?"YES":"NO");
  PFN_edep ee=(PFN_edep)g(inst,"vkEnumerateDeviceExtensionProperties");
  uint32_t c=0; ee(d,NULL,&c,NULL);
  VkExtensionProperties*all=malloc(sizeof(*all)*(c?c:1)); ee(d,NULL,&c,all);
  const char*want[]={"VK_EXT_vertex_attribute_divisor","VK_KHR_push_descriptor","VK_EXT_line_rasterization","VK_EXT_extended_dynamic_state","VK_EXT_extended_dynamic_state2","VK_EXT_extended_dynamic_state3","VK_KHR_timeline_semaphore","VK_EXT_transform_feedback","VK_EXT_provoking_vertex","VK_EXT_depth_clip_control","VK_KHR_shader_float_controls","VK_EXT_custom_border_color","VK_EXT_index_type_uint8","VK_EXT_4444_formats","VK_KHR_maintenance5","VK_EXT_multi_draw","VK_KHR_dynamic_rendering","VK_EXT_robustness2"};
  printf("\n== 扩展（设备共 %u 个）==\n",c);
  for(size_t i=0;i<sizeof(want)/sizeof(want[0]);i++){
    int found=0; for(uint32_t k=0;k<c;k++) if(!strcmp(all[k].extensionName,want[i])){found=1;break;}
    printf("  %-40s %s\n",want[i],found?"YES":"NO");
  }
  return 0;
}

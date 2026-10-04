#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>
typedef PFN_vkVoidFunction (*gipa_t)(VkInstance, const char*);
int main(int argc,char**argv){
  void*h=dlopen(argv[1],RTLD_NOW);
  if(!h){printf("dlopen失败: %s\n",dlerror());return 1;}
  gipa_t g=(gipa_t)dlsym(h,"vk_icdGetInstanceProcAddr");
  if(!g){printf("非ICD\n");return 2;}
  /* 实例级扩展 */
  PFN_vkEnumerateInstanceExtensionProperties eiep =
     (PFN_vkEnumerateInstanceExtensionProperties)g(NULL,"vkEnumerateInstanceExtensionProperties");
  uint32_t n=0; eiep(NULL,&n,NULL);
  VkExtensionProperties*all=malloc(sizeof(*all)*(n?n:1)); eiep(NULL,&n,all);
  printf("实例级扩展共 %u 个:\n", n);
  for(uint32_t i=0;i<n;i++) printf("  %s\n", all[i].extensionName);
  /* 建实例后再看设备级 WSI */
  VkApplicationInfo app={0}; app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion=VK_API_VERSION_1_0;
  VkInstanceCreateInfo ci={0}; ci.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; ci.pApplicationInfo=&app;
  VkInstance inst; PFN_vkCreateInstance cr=(PFN_vkCreateInstance)g(NULL,"vkCreateInstance");
  if(cr(&ci,NULL,&inst)!=VK_SUCCESS){printf("createInstance失败\n");return 3;}
  uint32_t dn=0; PFN_vkEnumeratePhysicalDevices epd=(PFN_vkEnumeratePhysicalDevices)g(inst,"vkEnumeratePhysicalDevices");
  epd(inst,&dn,NULL); VkPhysicalDevice d; epd(inst,&dn,&d);
  PFN_vkEnumerateDeviceExtensionProperties edep=(PFN_vkEnumerateDeviceExtensionProperties)g(inst,"vkEnumerateDeviceExtensionProperties");
  uint32_t c=0; edep(d,NULL,&c,NULL); VkExtensionProperties*da=malloc(sizeof(*da)*(c?c:1)); edep(d,NULL,&c,da);
  printf("\n设备级 WSI 相关:\n");
  const char*w[]={"VK_KHR_surface","VK_KHR_android_surface","VK_KHR_swapchain","VK_KHR_display","VK_EXT_headless_surface","VK_KHR_wayland_surface","VK_KHR_xcb_surface","VK_KHR_xlib_surface","VK_KHR_incremental_present","VK_KHR_present_wait","VK_KHR_shared_presentable_image"};
  for(size_t i=0;i<sizeof(w)/sizeof(w[0]);i++){int f2=0;for(uint32_t k=0;k<c;k++) if(!strcmp(da[k].extensionName,w[i])){f2=1;break;} printf("  %-34s %s\n",w[i],f2?"YES":"NO");}
  return 0;
}

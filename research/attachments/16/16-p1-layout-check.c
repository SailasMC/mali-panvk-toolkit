/* Verify what the 1.25 (112-byte) group-create initializer actually puts in
 * memory, in particular whether csi_handlers=1 leaks into neural_max. */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include "drm-uapi/mali_kbase_ioctl.h"

int main(void)
{
   union kbase_ioctl_cs_queue_group_create req = {
      .in = {
         .tiler_mask = 1,
         .fragment_mask = ~0ull,
         .compute_mask = ~0ull,
         .cs_min = 4,
         .priority = 0,
         .tiler_max = 1,
         .fragment_max = 64,
         .compute_max = 64,
         .cs_fault_report_enable = 1,
         .csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG,
      },
   };
   unsigned char *p = (unsigned char *)&req;
   int i;

   printf("sizeof=%zu offsetof(csi_handlers)=%zu offsetof(neural_max)=%zu "
          "offsetof(cs_fault_report_enable)=%zu\n",
          sizeof(req), offsetof(__typeof__(req.in), csi_handlers),
          offsetof(__typeof__(req.in), neural_max),
          offsetof(__typeof__(req.in), cs_fault_report_enable));
   printf("in bytes 24..35:");
   for (i = 24; i <= 35; i++)
      printf(" %02x", p[i]);
   printf("\n");
   printf("csi_handlers=%u neural_max=%u cs_fault_report_enable=%u\n",
          req.in.csi_handlers, req.in.neural_max, req.in.cs_fault_report_enable);

   union kbase_ioctl_cs_queue_group_create_1_18 r18 = {
      .in = {
         .tiler_mask = 1,
         .fragment_mask = ~0ull,
         .compute_mask = ~0ull,
         .cs_min = 4,
         .priority = 0,
         .tiler_max = 1,
         .fragment_max = 64,
         .compute_max = 64,
         .csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG,
      },
   };
   unsigned char *q = (unsigned char *)&r18;
   printf("1.18 sizeof=%zu bytes 24..39:", sizeof(r18));
   for (i = 24; i <= 39; i++)
      printf(" %02x", q[i]);
   printf("\n");
   printf("KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_18=0x%08x KBASE_IOCTL_CS_QUEUE_GROUP_CREATE=0x%08x\n",
          (unsigned)KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_18,
          (unsigned)KBASE_IOCTL_CS_QUEUE_GROUP_CREATE);
   return 0;
}

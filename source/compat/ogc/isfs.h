#ifndef GMU_COMPAT_ISFS_H
#define GMU_COMPAT_ISFS_H
/* GodModeWii reads and sets NAND file owners / permissions through IOS's ISFS. GodModeU reaches the
 * vWii NAND through Cafe OS's file system instead, so there are no ISFS attributes: reads fail (callers
 * then use the usual save permissions) and writes are skipped. */
#include <stdint.h>
static inline int32_t ISFS_GetAttr(const char *path, uint32_t *uid, uint16_t *gid, uint8_t *attr, uint8_t *op,
                                   uint8_t *gp, uint8_t *ot)
{
    (void)path; (void)uid; (void)gid; (void)attr; (void)op; (void)gp; (void)ot;
    return -1;
}
static inline int32_t ISFS_SetAttr(const char *path, uint32_t uid, uint16_t gid, uint8_t attr, uint8_t op,
                                   uint8_t gp, uint8_t ot)
{
    (void)path; (void)uid; (void)gid; (void)attr; (void)op; (void)gp; (void)ot;
    return -1;
}
#endif

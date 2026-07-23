#include "computer/xfs.h"

#include "computer.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void xfs_mkdir_if_needed(const char* path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST)
        server_printf("xfs: failed to create %s: %s\n", path, strerror(errno));
}

void xfs_init(struct computer_t* computer)
{
    memset(&computer->xfs, 0, sizeof(computer->xfs));
    snprintf(computer->xfs.base_path, sizeof(computer->xfs.base_path), "runtime/xfs");
    xfs_mkdir_if_needed("runtime");
    xfs_mkdir_if_needed(computer->xfs.base_path);
}

void xfs_reset(struct computer_t* computer)
{
    xfs_free(computer);
    memset(&computer->xfs.registers, 0, sizeof(computer->xfs.registers));
}

uint8_t xfs_read(struct computer_t* computer, uint16_t address)
{
    return ((uint8_t*)&computer->xfs.registers)[address & 0x0fff];
}

void xfs_write(struct computer_t* computer, uint16_t address, uint8_t value)
{
    const uint16_t offset = address & 0x0fff;
    ((uint8_t*)&computer->xfs.registers)[offset] = value;
    if (offset == 0)
        xfs_handle_command(computer, &computer->xfs.registers);
}

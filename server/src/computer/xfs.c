#include "computer/xfs.h"

#include "computer.h"

#include <stdio.h>
#include <string.h>

static struct xfs_handle_t* xfs_get_handle(struct computer_t* computer, uint8_t handle)
{
    if (handle < 1 || handle > XFS_MAX_FDS)
        return NULL;
    return &computer->xfs.handles[handle - 1];
}

static uint8_t xfs_allocate_handle(struct computer_t* computer, enum xfs_handle_type_t type, uint8_t mount_point)
{
    for (uint8_t i = 0; i < XFS_MAX_FDS; i++)
    {
        if (computer->xfs.handles[i].type == XFS_HANDLE_TYPE_NONE)
        {
            computer->xfs.handles[i].type = type;
            computer->xfs.handles[i].mount_point = mount_point;
            return i + 1;
        }
    }
    return 0;
}

static void xfs_free_handle(struct computer_t* computer, uint8_t handle, uint8_t mount_point)
{
    struct xfs_handle_t* h = xfs_get_handle(computer, handle);
    if (!h)
        return;

    const struct xfs_engine_mount_t* mount = &computer->xfs.mounts[mount_point];
    if (mount->engine && mount->engine->free_handle)
        mount->engine->free_handle(mount, h);

    h->type = XFS_HANDLE_TYPE_NONE;
    h->mount_point = 0;
    h->data = NULL;
}

static uint8_t xfs_ensure_mounted(struct computer_t* computer, uint8_t mount_point)
{
    return mount_point < XFS_MAX_MOUNTS && computer->xfs.mounts[mount_point].engine != NULL;
}

static void xfs_handle_mount(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const char* protocol = registers->arguments.mount.protocol;
    const char* hostname = registers->arguments.mount.hostname;
    const char* path = registers->arguments.mount.path;
    const uint8_t mount_point = registers->mount_point;

    if (mount_point >= XFS_MAX_MOUNTS)
    {
        registers->result = XFS_ERR_INVAL;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    if (computer->xfs.mounts[mount_point].engine != NULL)
    {
        registers->result = XFS_ERR_EXIST;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    if (strcmp(protocol, "xfs") != 0 || strcmp(hostname, "ram") != 0)
    {
        registers->result = XFS_ERR_INVAL;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    const int16_t err = xfs_ram_engine.mount(computer, hostname, path, &computer->xfs.mounts[mount_point]);
    if (err != XFS_ERR_OK)
    {
        registers->result = err;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    computer->xfs.mounts[mount_point].engine = &xfs_ram_engine;
    registers->result = XFS_ERR_OK;
    registers->status = XFS_STATUS_COMPLETE;
}

static void xfs_handle_open(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    if (!xfs_ensure_mounted(computer, mount_point))
    {
        registers->result = XFS_ERR_IO;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    const uint8_t handle = xfs_allocate_handle(computer, XFS_HANDLE_TYPE_FILE, mount_point);
    if (handle == 0)
    {
        registers->result = XFS_ERR_NOMEM;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    struct xfs_handle_t* h = xfs_get_handle(computer, handle);
    const int16_t err = computer->xfs.mounts[mount_point].engine->open(
        &computer->xfs.mounts[mount_point], h, registers->arguments.open.path, registers->arguments.open.flags);
    if (err)
    {
        xfs_free_handle(computer, handle, mount_point);
        registers->result = err;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    registers->file_handle = handle;
    registers->result = XFS_ERR_OK;
    registers->status = XFS_STATUS_COMPLETE;
}

static void xfs_handle_read(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    const uint8_t handle = registers->file_handle;
    struct xfs_handle_t* h = xfs_get_handle(computer, handle);

    if (!xfs_ensure_mounted(computer, mount_point) || h == NULL || h->type != XFS_HANDLE_TYPE_FILE)
    {
        registers->result = XFS_ERR_BADF;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    const int16_t bytes_read = computer->xfs.mounts[mount_point].engine->read(
        &computer->xfs.mounts[mount_point], h, registers->workspace, registers->arguments.read.size);
    if (bytes_read < 0)
    {
        registers->result = bytes_read;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    registers->result = bytes_read;
    registers->status = XFS_STATUS_COMPLETE;
}

static void xfs_handle_write(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    const uint8_t handle = registers->file_handle;
    struct xfs_handle_t* h = xfs_get_handle(computer, handle);

    if (!xfs_ensure_mounted(computer, mount_point) || h == NULL || h->type != XFS_HANDLE_TYPE_FILE)
    {
        registers->result = XFS_ERR_BADF;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    const int16_t bytes_written = computer->xfs.mounts[mount_point].engine->write(
        &computer->xfs.mounts[mount_point], h, registers->workspace, registers->arguments.write.size);
    if (bytes_written < 0)
    {
        registers->result = bytes_written;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    registers->result = bytes_written;
    registers->status = XFS_STATUS_COMPLETE;
}

static void xfs_handle_close(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    const uint8_t handle = registers->file_handle;
    struct xfs_handle_t* h = xfs_get_handle(computer, handle);
    int16_t err = XFS_ERR_OK;

    if (!xfs_ensure_mounted(computer, mount_point) || h == NULL)
    {
        registers->result = XFS_ERR_BADF;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    if (h->type == XFS_HANDLE_TYPE_FILE)
        err = computer->xfs.mounts[mount_point].engine->close(&computer->xfs.mounts[mount_point], h);
    else if (h->type == XFS_HANDLE_TYPE_DIR)
        err = computer->xfs.mounts[mount_point].engine->closedir(&computer->xfs.mounts[mount_point], h);

    xfs_free_handle(computer, handle, mount_point);

    if (err)
    {
        registers->result = err;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    registers->result = XFS_ERR_OK;
    registers->status = XFS_STATUS_COMPLETE;
}

static void xfs_handle_opendir(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    if (!xfs_ensure_mounted(computer, mount_point))
    {
        registers->result = XFS_ERR_IO;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    const uint8_t handle = xfs_allocate_handle(computer, XFS_HANDLE_TYPE_DIR, mount_point);
    if (handle == 0)
    {
        registers->result = XFS_ERR_NOMEM;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    struct xfs_handle_t* h = xfs_get_handle(computer, handle);
    const int16_t err = computer->xfs.mounts[mount_point].engine->opendir(
        &computer->xfs.mounts[mount_point], h, registers->arguments.opendir.path);
    if (err)
    {
        xfs_free_handle(computer, handle, mount_point);
        registers->result = err;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    registers->file_handle = handle;
    registers->result = XFS_ERR_OK;
    registers->status = XFS_STATUS_COMPLETE;
}

static void xfs_handle_readdir(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    const uint8_t handle = registers->file_handle;
    struct xfs_handle_t* h = xfs_get_handle(computer, handle);
    struct xfs_stat_info info;

    if (!xfs_ensure_mounted(computer, mount_point) || h == NULL || h->type != XFS_HANDLE_TYPE_DIR)
    {
        registers->result = XFS_ERR_BADF;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    const int16_t err = computer->xfs.mounts[mount_point].engine->readdir(
        &computer->xfs.mounts[mount_point], h, &info);
    if (err < 0)
    {
        registers->result = err;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    if (err == 0)
    {
        registers->result = 1;
    }
    else
    {
        strcpy((char*)registers->workspace, info.name);
        registers->result = 0;
    }
    registers->status = XFS_STATUS_COMPLETE;
}

static void xfs_handle_stat(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    struct xfs_stat_info info;

    if (!xfs_ensure_mounted(computer, mount_point))
    {
        registers->result = XFS_ERR_IO;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    const int16_t err = computer->xfs.mounts[mount_point].engine->stat(
        &computer->xfs.mounts[mount_point], registers->arguments.stat.path, &info);
    if (err)
    {
        registers->result = err;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    struct xfs_stat_t* stat = (struct xfs_stat_t*)registers->workspace;
    stat->mode = (info.type == XFS_TYPE_DIR) ? 0x4000 : 0x8000;
    stat->mode |= 0x01a4; /* 0644 */
    stat->uid = 0;
    stat->gid = 0;
    stat->size = info.size;
    stat->atime = 0;
    stat->mtime = 0;
    stat->ctime = 0;
    registers->workspace[22] = 0;
    registers->workspace[23] = 0;
    registers->result = XFS_ERR_OK;
    registers->status = XFS_STATUS_COMPLETE;
}

static void xfs_handle_simple_path_op(struct computer_t* computer, struct xfs_registers_t* registers,
                                      int16_t (*op)(const struct xfs_engine_mount_t*, const char*),
                                      const char* path)
{
    const uint8_t mount_point = registers->mount_point;
    if (!xfs_ensure_mounted(computer, mount_point))
    {
        registers->result = XFS_ERR_IO;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    const int16_t err = op(&computer->xfs.mounts[mount_point], path);
    if (err)
    {
        registers->result = err;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    registers->result = XFS_ERR_OK;
    registers->status = XFS_STATUS_COMPLETE;
}

static void xfs_handle_unlink(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    if (!xfs_ensure_mounted(computer, mount_point))
    {
        registers->result = XFS_ERR_IO;
        registers->status = XFS_STATUS_ERROR;
        return;
    }
    xfs_handle_simple_path_op(computer, registers,
                              computer->xfs.mounts[mount_point].engine->unlink,
                              registers->arguments.unlink.path);
}

static void xfs_handle_mkdir(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    if (!xfs_ensure_mounted(computer, mount_point))
    {
        registers->result = XFS_ERR_IO;
        registers->status = XFS_STATUS_ERROR;
        return;
    }
    xfs_handle_simple_path_op(computer, registers,
                              computer->xfs.mounts[mount_point].engine->mkdir,
                              registers->arguments.mkdir.path);
}

static void xfs_handle_rmdir(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    if (!xfs_ensure_mounted(computer, mount_point))
    {
        registers->result = XFS_ERR_IO;
        registers->status = XFS_STATUS_ERROR;
        return;
    }
    xfs_handle_simple_path_op(computer, registers,
                              computer->xfs.mounts[mount_point].engine->rmdir,
                              registers->arguments.rmdir.path);
}

static void xfs_handle_chdir(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    if (!xfs_ensure_mounted(computer, mount_point))
    {
        registers->result = XFS_ERR_IO;
        registers->status = XFS_STATUS_ERROR;
        return;
    }
    xfs_handle_simple_path_op(computer, registers,
                              computer->xfs.mounts[mount_point].engine->chdir,
                              registers->arguments.chdir.path);
}

static void xfs_handle_getcwd(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    if (!xfs_ensure_mounted(computer, mount_point))
    {
        registers->result = XFS_ERR_IO;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    const int16_t err = computer->xfs.mounts[mount_point].engine->getcwd(
        &computer->xfs.mounts[mount_point], (char*)registers->workspace, registers->arguments.getcwd.buffer_size);
    if (err)
    {
        registers->result = err;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    registers->result = XFS_ERR_OK;
    registers->status = XFS_STATUS_COMPLETE;
}

static void xfs_handle_rename(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    if (!xfs_ensure_mounted(computer, mount_point))
    {
        registers->result = XFS_ERR_IO;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    const int16_t err = computer->xfs.mounts[mount_point].engine->rename(
        &computer->xfs.mounts[mount_point],
        registers->arguments.rename.old_path,
        registers->arguments.rename.new_path);
    if (err)
    {
        registers->result = err;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    registers->result = XFS_ERR_OK;
    registers->status = XFS_STATUS_COMPLETE;
}

static void xfs_handle_lseek(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;
    const uint8_t handle = registers->file_handle;
    struct xfs_handle_t* h = xfs_get_handle(computer, handle);

    if (!xfs_ensure_mounted(computer, mount_point) || h == NULL || h->type != XFS_HANDLE_TYPE_FILE)
    {
        registers->result = XFS_ERR_BADF;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    const int32_t new_pos = computer->xfs.mounts[mount_point].engine->lseek(
        &computer->xfs.mounts[mount_point], h, registers->arguments.lseek.offset,
        registers->arguments.lseek.whence);
    if (new_pos < 0)
    {
        registers->result = (int16_t)new_pos;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    memcpy(registers->workspace, &new_pos, sizeof(new_pos));
    registers->result = XFS_ERR_OK;
    registers->status = XFS_STATUS_COMPLETE;
}

static void xfs_handle_umount(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t mount_point = registers->mount_point;

    if (mount_point >= XFS_MAX_MOUNTS)
    {
        registers->result = XFS_ERR_INVAL;
        registers->status = XFS_STATUS_ERROR;
        return;
    }

    for (uint8_t i = 0; i < XFS_MAX_FDS; i++)
    {
        if (computer->xfs.handles[i].type != XFS_HANDLE_TYPE_NONE &&
            computer->xfs.handles[i].mount_point == mount_point)
        {
            xfs_free_handle(computer, i + 1, mount_point);
        }
    }

    if (computer->xfs.mounts[mount_point].engine && computer->xfs.mounts[mount_point].engine->unmount)
        computer->xfs.mounts[mount_point].engine->unmount(&computer->xfs.mounts[mount_point]);
    memset(&computer->xfs.mounts[mount_point], 0, sizeof(computer->xfs.mounts[mount_point]));

    registers->result = XFS_ERR_OK;
    registers->status = XFS_STATUS_COMPLETE;
}

void xfs_handle_command(struct computer_t* computer, struct xfs_registers_t* registers)
{
    const uint8_t command = registers->command;

    if (!command)
        return;

    registers->command = 0;
    registers->result = 0;
    registers->status = XFS_STATUS_BUSY;

    switch (command)
    {
        case XFS_CMD_MOUNT:
            xfs_handle_mount(computer, registers);
            break;
        case XFS_CMD_OPEN:
            xfs_handle_open(computer, registers);
            break;
        case XFS_CMD_READ:
            xfs_handle_read(computer, registers);
            break;
        case XFS_CMD_WRITE:
            xfs_handle_write(computer, registers);
            break;
        case XFS_CMD_CLOSE:
            xfs_handle_close(computer, registers);
            break;
        case XFS_CMD_OPENDIR:
            xfs_handle_opendir(computer, registers);
            break;
        case XFS_CMD_READDIR:
            xfs_handle_readdir(computer, registers);
            break;
        case XFS_CMD_CLOSEDIR:
            xfs_handle_close(computer, registers);
            break;
        case XFS_CMD_STAT:
            xfs_handle_stat(computer, registers);
            break;
        case XFS_CMD_UNLINK:
            xfs_handle_unlink(computer, registers);
            break;
        case XFS_CMD_MKDIR:
            xfs_handle_mkdir(computer, registers);
            break;
        case XFS_CMD_RMDIR:
            xfs_handle_rmdir(computer, registers);
            break;
        case XFS_CMD_CHDIR:
            xfs_handle_chdir(computer, registers);
            break;
        case XFS_CMD_GETCWD:
            xfs_handle_getcwd(computer, registers);
            break;
        case XFS_CMD_RENAME:
            xfs_handle_rename(computer, registers);
            break;
        case XFS_CMD_LSEEK:
            xfs_handle_lseek(computer, registers);
            break;
        case XFS_CMD_UNMOUNT:
            xfs_handle_umount(computer, registers);
            break;
        default:
            registers->result = XFS_ERR_INVAL;
            registers->status = XFS_STATUS_ERROR;
            break;
    }
}

void xfs_free(struct computer_t* computer)
{
    for (uint8_t i = 0; i < XFS_MAX_FDS; i++)
    {
        if (computer->xfs.handles[i].type != XFS_HANDLE_TYPE_NONE)
            xfs_free_handle(computer, i + 1, computer->xfs.handles[i].mount_point);
    }

    for (uint8_t mount_point = 0; mount_point < XFS_MAX_MOUNTS; mount_point++)
    {
        if (computer->xfs.mounts[mount_point].engine && computer->xfs.mounts[mount_point].engine->unmount)
            computer->xfs.mounts[mount_point].engine->unmount(&computer->xfs.mounts[mount_point]);
        memset(&computer->xfs.mounts[mount_point], 0, sizeof(computer->xfs.mounts[mount_point]));
    }
}

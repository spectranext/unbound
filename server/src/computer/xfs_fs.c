#include "computer/xfs.h"

#include "computer.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct xfs_fs_mount_data_t
{
    struct computer_t* computer;
    char base_path[PATH_MAX];
    char cwd[PATH_MAX];
};

enum xfs_fs_handle_kind_t
{
    XFS_FS_HANDLE_REAL = 0,
    XFS_FS_HANDLE_VIRTUAL = 1,
};

#define XFS_FS_MAX_SYNTHETIC_DIR_ENTRIES 64

struct xfs_fs_file_handle_t
{
    enum xfs_fs_handle_kind_t kind;
    int fd;
    struct computer_xfs_path_bind_t* bind;
    void* virtual_handle;
};

struct xfs_fs_dir_handle_t
{
    enum xfs_fs_handle_kind_t kind;
    DIR* dir;
    struct computer_xfs_path_bind_t* bind;
    void* virtual_handle;
    char path[PATH_MAX];
    char synthetic_entries[XFS_FS_MAX_SYNTHETIC_DIR_ENTRIES][64];
    uint8_t synthetic_count;
    uint8_t synthetic_index;
};

static int16_t xfs_error_from_errno(int err)
{
    return err ? (int16_t)(-err) : XFS_ERR_OK;
}

static struct xfs_fs_mount_data_t* get_mount_data(const struct xfs_engine_mount_t* mount)
{
    return (struct xfs_fs_mount_data_t*)mount->mount_data;
}

static struct xfs_fs_file_handle_t* get_file_handle(const struct xfs_handle_t* handle)
{
    return (struct xfs_fs_file_handle_t*)handle->data;
}

static struct xfs_fs_dir_handle_t* get_dir_handle(const struct xfs_handle_t* handle)
{
    return (struct xfs_fs_dir_handle_t*)handle->data;
}

static int mkdir_p(const char* path)
{
    char tmp[PATH_MAX];
    size_t len;

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    len = strlen(tmp);
    if (len == 0)
        return 0;

    if (tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    for (char* p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int normalize_virtual_path(const char* cwd, const char* input, char* out, size_t out_size)
{
    char source[PATH_MAX];
    char result[PATH_MAX];
    size_t result_len = 1;

    if (!cwd || !cwd[0])
        cwd = "/";
    if (!input || !input[0])
        input = ".";

    if (input[0] == '/')
        snprintf(source, sizeof(source), "%s", input);
    else if (strcmp(cwd, "/") == 0)
        snprintf(source, sizeof(source), "/%s", input);
    else
        snprintf(source, sizeof(source), "%s/%s", cwd, input);

    result[0] = '/';
    result[1] = '\0';

    char* saveptr = NULL;
    char source_copy[PATH_MAX];
    strncpy(source_copy, source, sizeof(source_copy) - 1);
    source_copy[sizeof(source_copy) - 1] = '\0';

    for (char* token = strtok_r(source_copy, "/", &saveptr); token; token = strtok_r(NULL, "/", &saveptr))
    {
        if (strcmp(token, ".") == 0 || token[0] == '\0')
            continue;
        if (strcmp(token, "..") == 0)
        {
            if (result_len > 1)
            {
                result[--result_len] = '\0';
                while (result_len > 1 && result[result_len - 1] != '/')
                    result[--result_len] = '\0';
            }
            continue;
        }

        if (result_len > 1)
            result[result_len++] = '/';

        size_t token_len = strlen(token);
        if (result_len + token_len >= sizeof(result))
            return -1;
        memcpy(&result[result_len], token, token_len);
        result_len += token_len;
        result[result_len] = '\0';
    }

    if (result_len == 0)
    {
        result[0] = '/';
        result[1] = '\0';
    }

    if (strlen(result) >= out_size)
        return -1;
    strcpy(out, result);
    return 0;
}

static int build_host_path(const struct xfs_engine_mount_t* mount, const char* path, char* out, size_t out_size)
{
    char normalized[PATH_MAX];
    struct xfs_fs_mount_data_t* mount_data = get_mount_data(mount);

    if (!mount_data)
        return -1;
    if (normalize_virtual_path(mount_data->cwd, path, normalized, sizeof(normalized)) != 0)
        return -1;

    if (strcmp(normalized, "/") == 0)
        snprintf(out, out_size, "%s", mount_data->base_path);
    else
        snprintf(out, out_size, "%s%s", mount_data->base_path, normalized);
    return 0;
}

static const char* virtual_basename(const char* path)
{
    const char* name = strrchr(path, '/');
    if (!name)
        return path;
    if (!name[1])
        return "/";
    return name + 1;
}

static uint8_t append_synthetic_entry(struct xfs_fs_dir_handle_t* dir_handle, const char* name)
{
    if (!name || !name[0] || strlen(name) >= sizeof(dir_handle->synthetic_entries[0]))
        return 0;

    for (uint8_t i = 0; i < dir_handle->synthetic_count; i++)
    {
        if (strcmp(dir_handle->synthetic_entries[i], name) == 0)
            return 1;
    }

    if (dir_handle->synthetic_count >= XFS_FS_MAX_SYNTHETIC_DIR_ENTRIES)
        return 0;

    snprintf(dir_handle->synthetic_entries[dir_handle->synthetic_count],
             sizeof(dir_handle->synthetic_entries[0]), "%s", name);
    dir_handle->synthetic_count++;
    return 1;
}

static uint8_t synthetic_entry_is_dir(struct computer_t* computer, const char* path, const char* name)
{
    char child_path[PATH_MAX];
    struct computer_xfs_path_bind_t* bind;

    if (strcmp(path, "/") == 0)
        snprintf(child_path, sizeof(child_path), "/%s", name);
    else
        snprintf(child_path, sizeof(child_path), "%s/%s", path, name);

    bind = computer_find_xfs_path_bind(computer, child_path);
    return (bind && bind->has_readdir) || computer_xfs_path_has_children(computer, child_path);
}

static uint8_t synthetic_entry_exists_on_host(const struct xfs_engine_mount_t* mount, const char* path, const char* name)
{
    char child_path[PATH_MAX];
    char full_path[PATH_MAX];
    struct stat st;

    if (strcmp(path, "/") == 0)
        snprintf(child_path, sizeof(child_path), "/%s", name);
    else
        snprintf(child_path, sizeof(child_path), "%s/%s", path, name);

    if (build_host_path(mount, child_path, full_path, sizeof(full_path)) != 0)
        return 0;
    return stat(full_path, &st) == 0;
}

static void collect_synthetic_entries(struct computer_t* computer, const char* path,
                                      struct xfs_fs_dir_handle_t* dir_handle)
{
    struct computer_xfs_path_bind_t* bind;
    const size_t path_len = strlen(path);

    for (bind = computer->xfs_path_binds; bind; bind = bind->hh.next)
    {
        const char* rest = NULL;
        char name[64];
        size_t name_len;

        if (strcmp(path, "/") == 0)
        {
            if (strcmp(bind->path, "/") == 0 || bind->path[0] != '/')
                continue;
            rest = bind->path + 1;
        }
        else
        {
            if (strncmp(bind->path, path, path_len) != 0 || bind->path[path_len] != '/')
                continue;
            rest = bind->path + path_len + 1;
        }

        if (!rest || !rest[0])
            continue;

        name_len = strcspn(rest, "/");
        if (name_len == 0 || name_len >= sizeof(name))
            continue;

        memcpy(name, rest, name_len);
        name[name_len] = '\0';
        append_synthetic_entry(dir_handle, name);
    }
}

static int16_t fs_mount(struct computer_t* computer, const char* hostname, const char* path,
                        struct xfs_engine_mount_t* out_mount)
{
    (void)hostname;

    struct xfs_fs_mount_data_t* mount_data = calloc(1, sizeof(*mount_data));
    if (!mount_data)
        return XFS_ERR_NOMEM;

    mount_data->computer = computer;
    snprintf(mount_data->base_path, sizeof(mount_data->base_path), "%s/team_%u",
             computer->xfs.base_path, (unsigned)computer->device.namespace_id);
    if (mkdir_p(mount_data->base_path) != 0)
    {
        free(mount_data);
        return xfs_error_from_errno(errno);
    }

    if (normalize_virtual_path("/", path && path[0] ? path : "/", mount_data->cwd, sizeof(mount_data->cwd)) != 0)
        strcpy(mount_data->cwd, "/");

    out_mount->mount_data = mount_data;
    return XFS_ERR_OK;
}

static void fs_unmount(struct xfs_engine_mount_t* mount)
{
    free(mount->mount_data);
    mount->mount_data = NULL;
}

static int16_t fs_open(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle, const char* path, int flags)
{
    char normalized[PATH_MAX];
    struct xfs_fs_mount_data_t* mount_data = get_mount_data(mount);
    struct computer_xfs_path_bind_t* bind;

    if (!mount_data)
        return XFS_ERR_IO;
    if (normalize_virtual_path(mount_data->cwd, path, normalized, sizeof(normalized)) != 0)
        return XFS_ERR_INVAL;

    bind = computer_find_xfs_path_bind(mount_data->computer, normalized);
    if (bind)
    {
        struct xfs_fs_file_handle_t* file_handle;
        void* virtual_handle = NULL;
        int16_t err = XFS_ERR_OK;

        if (bind->has_readdir && !bind->read)
            return XFS_ERR_ISDIR;

        file_handle = calloc(1, sizeof(*file_handle));
        if (!file_handle)
            return XFS_ERR_NOMEM;

        if (bind->open)
            err = bind->open(bind->user, normalized, flags, 0, &virtual_handle);
        if (err)
        {
            free(file_handle);
            return err;
        }

        file_handle->kind = XFS_FS_HANDLE_VIRTUAL;
        file_handle->fd = -1;
        file_handle->bind = bind;
        file_handle->virtual_handle = virtual_handle;
        handle->data = file_handle;
        return XFS_ERR_OK;
    }

    char full_path[PATH_MAX];
    if (build_host_path(mount, path, full_path, sizeof(full_path)) != 0)
        return XFS_ERR_INVAL;

    int open_flags;
    switch (flags & 0x0003)
    {
        case XFS_O_WRONLY: open_flags = O_WRONLY; break;
        case XFS_O_RDWR: open_flags = O_RDWR; break;
        case XFS_O_RDONLY:
        default: open_flags = O_RDONLY; break;
    }

    if (flags & XFS_O_APPEND) open_flags |= O_APPEND;
    if (flags & XFS_O_CREAT) open_flags |= O_CREAT;
    if (flags & XFS_O_EXCL) open_flags |= O_EXCL;
    if (flags & XFS_O_TRUNC) open_flags |= O_TRUNC;

    struct xfs_fs_file_handle_t* file_handle = calloc(1, sizeof(*file_handle));
    if (!file_handle)
        return XFS_ERR_NOMEM;

    file_handle->kind = XFS_FS_HANDLE_REAL;
    file_handle->fd = open(full_path, open_flags, 0644);
    if (file_handle->fd < 0)
    {
        int saved = errno;
        free(file_handle);
        return xfs_error_from_errno(saved);
    }

    handle->data = file_handle;
    return XFS_ERR_OK;
}

static int16_t fs_read(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle, void* buffer, uint16_t size)
{
    (void)mount;
    struct xfs_fs_file_handle_t* file_handle = get_file_handle(handle);
    if (!file_handle)
        return XFS_ERR_BADF;

    if (file_handle->kind == XFS_FS_HANDLE_VIRTUAL)
    {
        if (!file_handle->bind || !file_handle->bind->read)
            return XFS_ERR_BADF;
        return file_handle->bind->read(file_handle->bind->user, file_handle->virtual_handle, buffer, size);
    }

    ssize_t bytes_read = read(file_handle->fd, buffer, size);
    if (bytes_read < 0)
        return xfs_error_from_errno(errno);
    return (int16_t)bytes_read;
}

static int16_t fs_write(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle, const void* buffer, uint16_t size)
{
    (void)mount;
    struct xfs_fs_file_handle_t* file_handle = get_file_handle(handle);
    if (!file_handle)
        return XFS_ERR_BADF;

    if (file_handle->kind == XFS_FS_HANDLE_VIRTUAL)
    {
        if (!file_handle->bind || !file_handle->bind->write)
            return XFS_ERR_BADF;
        return file_handle->bind->write(file_handle->bind->user, file_handle->virtual_handle, buffer, size);
    }

    ssize_t bytes_written = write(file_handle->fd, buffer, size);
    if (bytes_written < 0)
        return xfs_error_from_errno(errno);
    return (int16_t)bytes_written;
}

static int16_t fs_close(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle)
{
    (void)mount;
    struct xfs_fs_file_handle_t* file_handle = get_file_handle(handle);
    if (!file_handle)
        return XFS_ERR_BADF;

    if (file_handle->kind == XFS_FS_HANDLE_VIRTUAL)
    {
        int16_t err = XFS_ERR_OK;
        if (file_handle->bind && file_handle->bind->close)
            err = file_handle->bind->close(file_handle->bind->user, file_handle->virtual_handle);
        free(file_handle);
        handle->data = NULL;
        return err;
    }

    int ret = close(file_handle->fd);
    free(file_handle);
    handle->data = NULL;
    return ret == 0 ? XFS_ERR_OK : xfs_error_from_errno(errno);
}

static int32_t fs_lseek(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle, uint32_t offset, uint8_t whence)
{
    (void)mount;
    struct xfs_fs_file_handle_t* file_handle = get_file_handle(handle);
    int origin = SEEK_SET;

    if (!file_handle)
        return XFS_ERR_BADF;

    if (file_handle->kind == XFS_FS_HANDLE_VIRTUAL)
    {
        if (!file_handle->bind || !file_handle->bind->seek)
            return XFS_ERR_INVAL;
        return file_handle->bind->seek(file_handle->bind->user, file_handle->virtual_handle, whence, offset);
    }

    if (whence == XFS_SEEK_CUR)
        origin = SEEK_CUR;
    else if (whence == XFS_SEEK_END)
        origin = SEEK_END;

    off_t pos = lseek(file_handle->fd, (off_t)offset, origin);
    if (pos < 0)
        return xfs_error_from_errno(errno);
    return (int32_t)pos;
}

static int16_t fs_opendir(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle, const char* path)
{
    char normalized[PATH_MAX];
    char full_path[PATH_MAX];
    struct xfs_fs_mount_data_t* mount_data = get_mount_data(mount);
    struct computer_xfs_path_bind_t* bind;
    uint8_t has_children;
    DIR* dir = NULL;

    if (!mount_data)
        return XFS_ERR_IO;
    if (normalize_virtual_path(mount_data->cwd, path, normalized, sizeof(normalized)) != 0)
        return XFS_ERR_INVAL;

    bind = computer_find_xfs_path_bind(mount_data->computer, normalized);
    has_children = computer_xfs_path_has_children(mount_data->computer, normalized);

    if (bind && bind->has_readdir)
    {
        struct xfs_fs_dir_handle_t* dir_handle = calloc(1, sizeof(*dir_handle));
        void* virtual_handle = NULL;
        int16_t err = XFS_ERR_OK;

        if (!dir_handle)
            return XFS_ERR_NOMEM;

        if (bind && bind->has_readdir && bind->open)
            err = bind->open(bind->user, normalized, 0, 1, &virtual_handle);
        if (err)
        {
            free(dir_handle);
            return err;
        }

        dir_handle->kind = XFS_FS_HANDLE_VIRTUAL;
        dir_handle->bind = bind;
        dir_handle->virtual_handle = virtual_handle;
        snprintf(dir_handle->path, sizeof(dir_handle->path), "%s", normalized);
        handle->data = dir_handle;
        return XFS_ERR_OK;
    }

    if (build_host_path(mount, path, full_path, sizeof(full_path)) != 0)
        return XFS_ERR_INVAL;

    dir = opendir(full_path);
    if (!dir && !has_children)
        return xfs_error_from_errno(errno);

    struct xfs_fs_dir_handle_t* dir_handle = calloc(1, sizeof(*dir_handle));
    if (!dir_handle)
    {
        if (dir)
            closedir(dir);
        return XFS_ERR_NOMEM;
    }

    dir_handle->kind = XFS_FS_HANDLE_REAL;
    dir_handle->dir = dir;
    snprintf(dir_handle->path, sizeof(dir_handle->path), "%s", normalized);
    if (has_children)
        collect_synthetic_entries(mount_data->computer, normalized, dir_handle);
    handle->data = dir_handle;
    return XFS_ERR_OK;
}

static int16_t fs_readdir(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle, struct xfs_stat_info* info)
{
    struct xfs_fs_mount_data_t* mount_data = get_mount_data(mount);
    struct xfs_fs_dir_handle_t* dir_handle = get_dir_handle(handle);
    if (!dir_handle)
        return XFS_ERR_BADF;

    if (dir_handle->bind && dir_handle->bind->readdir)
    {
        return dir_handle->bind->readdir(dir_handle->bind->user, dir_handle->virtual_handle, info);
    }

    errno = 0;
    while (dir_handle->dir)
    {
        struct dirent* entry = readdir(dir_handle->dir);
        if (!entry)
        {
            if (errno)
                return xfs_error_from_errno(errno);
            closedir(dir_handle->dir);
            dir_handle->dir = NULL;
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        memset(info, 0, sizeof(*info));
        strncpy(info->name, entry->d_name, sizeof(info->name) - 1);
        info->type = (entry->d_type == DT_DIR) ? XFS_TYPE_DIR : XFS_TYPE_REG;
        return 1;
    }

    while (dir_handle->synthetic_index < dir_handle->synthetic_count)
    {
        const char* name = dir_handle->synthetic_entries[dir_handle->synthetic_index++];
        if (synthetic_entry_exists_on_host(mount, dir_handle->path, name))
            continue;

        memset(info, 0, sizeof(*info));
        strncpy(info->name, name, sizeof(info->name) - 1);
        info->type = mount_data && synthetic_entry_is_dir(mount_data->computer, dir_handle->path, info->name)
            ? XFS_TYPE_DIR : XFS_TYPE_REG;
        return 1;
    }

    return 0;
}

static int16_t fs_closedir(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle)
{
    (void)mount;
    struct xfs_fs_dir_handle_t* dir_handle = get_dir_handle(handle);
    if (!dir_handle)
        return XFS_ERR_BADF;

    if (dir_handle->bind && dir_handle->bind->close)
    {
        int16_t err = XFS_ERR_OK;
        err = dir_handle->bind->close(dir_handle->bind->user, dir_handle->virtual_handle);
        if (dir_handle->dir)
            closedir(dir_handle->dir);
        free(dir_handle);
        handle->data = NULL;
        return err;
    }

    int ret = dir_handle->dir ? closedir(dir_handle->dir) : 0;
    free(dir_handle);
    handle->data = NULL;
    return ret == 0 ? XFS_ERR_OK : xfs_error_from_errno(errno);
}

static int16_t fs_stat(const struct xfs_engine_mount_t* mount, const char* path, struct xfs_stat_info* stat_info)
{
    char normalized[PATH_MAX];
    struct xfs_fs_mount_data_t* mount_data = get_mount_data(mount);
    struct computer_xfs_path_bind_t* bind;
    char full_path[PATH_MAX];
    struct stat st;

    if (!mount_data)
        return XFS_ERR_IO;
    if (normalize_virtual_path(mount_data->cwd, path, normalized, sizeof(normalized)) != 0)
        return XFS_ERR_INVAL;

    bind = computer_find_xfs_path_bind(mount_data->computer, normalized);
    if (bind || computer_xfs_path_has_children(mount_data->computer, normalized))
    {
        memset(stat_info, 0, sizeof(*stat_info));
        stat_info->type = ((bind && bind->has_readdir) ||
                           computer_xfs_path_has_children(mount_data->computer, normalized))
            ? XFS_TYPE_DIR : XFS_TYPE_REG;
        stat_info->size = 0;
        strncpy(stat_info->name, virtual_basename(normalized), sizeof(stat_info->name) - 1);
        return XFS_ERR_OK;
    }

    if (build_host_path(mount, path, full_path, sizeof(full_path)) != 0)
        return XFS_ERR_INVAL;
    if (stat(full_path, &st) != 0)
        return xfs_error_from_errno(errno);

    memset(stat_info, 0, sizeof(*stat_info));
    stat_info->type = S_ISDIR(st.st_mode) ? XFS_TYPE_DIR : XFS_TYPE_REG;
    stat_info->size = (uint32_t)st.st_size;

    const char* name = strrchr(path, '/');
    name = name ? name + 1 : path;
    if (!name[0])
        name = "/";
    strncpy(stat_info->name, name, sizeof(stat_info->name) - 1);
    return XFS_ERR_OK;
}

static int16_t fs_unlink(const struct xfs_engine_mount_t* mount, const char* path)
{
    char full_path[PATH_MAX];
    if (build_host_path(mount, path, full_path, sizeof(full_path)) != 0)
        return XFS_ERR_INVAL;
    return unlink(full_path) == 0 ? XFS_ERR_OK : xfs_error_from_errno(errno);
}

static int16_t fs_mkdir(const struct xfs_engine_mount_t* mount, const char* path)
{
    char full_path[PATH_MAX];
    if (build_host_path(mount, path, full_path, sizeof(full_path)) != 0)
        return XFS_ERR_INVAL;
    return mkdir(full_path, 0755) == 0 ? XFS_ERR_OK : xfs_error_from_errno(errno);
}

static int16_t fs_rmdir(const struct xfs_engine_mount_t* mount, const char* path)
{
    char full_path[PATH_MAX];
    if (build_host_path(mount, path, full_path, sizeof(full_path)) != 0)
        return XFS_ERR_INVAL;
    return rmdir(full_path) == 0 ? XFS_ERR_OK : xfs_error_from_errno(errno);
}

static int16_t fs_chdir(const struct xfs_engine_mount_t* mount, const char* path)
{
    char full_path[PATH_MAX];
    char normalized[PATH_MAX];
    struct stat st;
    struct xfs_fs_mount_data_t* mount_data = get_mount_data(mount);
    struct computer_xfs_path_bind_t* bind;

    if (!mount_data)
        return XFS_ERR_IO;
    if (normalize_virtual_path(mount_data->cwd, path, normalized, sizeof(normalized)) != 0)
        return XFS_ERR_INVAL;
    bind = computer_find_xfs_path_bind(mount_data->computer, normalized);
    if ((bind && bind->has_readdir) || computer_xfs_path_has_children(mount_data->computer, normalized))
    {
        strcpy(mount_data->cwd, normalized);
        return XFS_ERR_OK;
    }
    if (build_host_path(mount, normalized, full_path, sizeof(full_path)) != 0)
        return XFS_ERR_INVAL;
    if (stat(full_path, &st) != 0)
        return xfs_error_from_errno(errno);
    if (!S_ISDIR(st.st_mode))
        return XFS_ERR_NOTDIR;

    strcpy(mount_data->cwd, normalized);
    return XFS_ERR_OK;
}

static int16_t fs_getcwd(const struct xfs_engine_mount_t* mount, char* buffer, uint16_t size)
{
    struct xfs_fs_mount_data_t* mount_data = get_mount_data(mount);
    if (!mount_data || size < 2)
        return XFS_ERR_INVAL;
    if (strlen(mount_data->cwd) + 1 > size)
        return XFS_ERR_NAMETOOLONG;
    strcpy(buffer, mount_data->cwd);
    return XFS_ERR_OK;
}

static int16_t fs_rename(const struct xfs_engine_mount_t* mount, const char* old_path, const char* new_path)
{
    char full_old[PATH_MAX];
    char full_new[PATH_MAX];
    if (build_host_path(mount, old_path, full_old, sizeof(full_old)) != 0 ||
        build_host_path(mount, new_path, full_new, sizeof(full_new)) != 0)
        return XFS_ERR_INVAL;
    return rename(full_old, full_new) == 0 ? XFS_ERR_OK : xfs_error_from_errno(errno);
}

static void fs_free_handle(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle)
{
    (void)mount;

    if (handle->type == XFS_HANDLE_TYPE_FILE)
    {
        struct xfs_fs_file_handle_t* file_handle = get_file_handle(handle);
        if (file_handle)
        {
            if (file_handle->kind == XFS_FS_HANDLE_VIRTUAL)
            {
                if (file_handle->bind && file_handle->bind->close)
                    file_handle->bind->close(file_handle->bind->user, file_handle->virtual_handle);
            }
            else if (file_handle->fd >= 0)
                close(file_handle->fd);
            free(file_handle);
            handle->data = NULL;
        }
    }
    else if (handle->type == XFS_HANDLE_TYPE_DIR)
    {
        struct xfs_fs_dir_handle_t* dir_handle = get_dir_handle(handle);
        if (dir_handle)
        {
            if (dir_handle->bind && dir_handle->bind->close)
            {
                dir_handle->bind->close(dir_handle->bind->user, dir_handle->virtual_handle);
            }
            if (dir_handle->dir)
                closedir(dir_handle->dir);
            free(dir_handle);
            handle->data = NULL;
        }
    }
}

const struct xfs_engine_t xfs_ram_engine = {
    .mount = fs_mount,
    .unmount = fs_unmount,
    .open = fs_open,
    .read = fs_read,
    .write = fs_write,
    .close = fs_close,
    .lseek = fs_lseek,
    .opendir = fs_opendir,
    .readdir = fs_readdir,
    .closedir = fs_closedir,
    .stat = fs_stat,
    .unlink = fs_unlink,
    .mkdir = fs_mkdir,
    .rmdir = fs_rmdir,
    .chdir = fs_chdir,
    .getcwd = fs_getcwd,
    .rename = fs_rename,
    .free_handle = fs_free_handle,
};

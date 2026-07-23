#ifndef ZX_SANDBOX_XFS_H
#define ZX_SANDBOX_XFS_H

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

struct computer_t;

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define XFS_SPECTRANET_PAGE (0x49u)

enum xfs_error
{
    XFS_ERR_OK = 0,
    XFS_ERR_IO = -5,
    XFS_ERR_NOENT = -2,
    XFS_ERR_EXIST = -17,
    XFS_ERR_NOTDIR = -20,
    XFS_ERR_ISDIR = -21,
    XFS_ERR_NOTEMPTY = -39,
    XFS_ERR_BADF = -9,
    XFS_ERR_INVAL = -22,
    XFS_ERR_NOSPC = -28,
    XFS_ERR_NOMEM = -12,
    XFS_ERR_NAMETOOLONG = -36,
};

enum xfs_type
{
    XFS_TYPE_REG = 0x001,
    XFS_TYPE_DIR = 0x002,
};

enum xfs_open_flags
{
    XFS_O_RDONLY = 1,
    XFS_O_WRONLY = 2,
    XFS_O_RDWR = 3,
    XFS_O_CREAT = 0x0100,
    XFS_O_EXCL = 0x0200,
    XFS_O_TRUNC = 0x0400,
    XFS_O_APPEND = 0x0800,
};

enum xfs_whence_flags
{
    XFS_SEEK_SET = 0,
    XFS_SEEK_CUR = 1,
    XFS_SEEK_END = 2,
};

#define XFS_CMD_MOUNT (1)
#define XFS_CMD_OPEN (2)
#define XFS_CMD_READ (3)
#define XFS_CMD_WRITE (4)
#define XFS_CMD_CLOSE (5)
#define XFS_CMD_OPENDIR (6)
#define XFS_CMD_READDIR (7)
#define XFS_CMD_CLOSEDIR (8)
#define XFS_CMD_STAT (9)
#define XFS_CMD_UNLINK (10)
#define XFS_CMD_MKDIR (11)
#define XFS_CMD_RMDIR (12)
#define XFS_CMD_CHDIR (13)
#define XFS_CMD_GETCWD (14)
#define XFS_CMD_RENAME (15)
#define XFS_CMD_LSEEK (16)
#define XFS_CMD_UNMOUNT (17)

#define XFS_STATUS_IDLE (0)
#define XFS_STATUS_BUSY (1)
#define XFS_STATUS_COMPLETE (2)
#define XFS_STATUS_ERROR (3)

#define XFS_MAX_FDS (16)
#define XFS_MAX_MOUNTS (4)

struct xfs_handle_t;
struct xfs_engine_mount_t;

struct xfs_stat_info
{
    uint8_t type;
    uint32_t size;
    char name[64];
};

struct xfs_engine_t
{
    int16_t (*mount)(struct computer_t* computer, const char* hostname, const char* path,
                     struct xfs_engine_mount_t* out_mount);
    void (*unmount)(struct xfs_engine_mount_t* mount);
    int16_t (*open)(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle,
                    const char* path, int flags);
    int16_t (*read)(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle,
                    void* buffer, uint16_t size);
    int16_t (*write)(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle,
                     const void* buffer, uint16_t size);
    int16_t (*close)(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle);
    int32_t (*lseek)(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle,
                     uint32_t offset, uint8_t whence);
    int16_t (*opendir)(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle,
                       const char* path);
    int16_t (*readdir)(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle,
                       struct xfs_stat_info* info);
    int16_t (*closedir)(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle);
    int16_t (*stat)(const struct xfs_engine_mount_t* mount, const char* path,
                    struct xfs_stat_info* stat_info);
    int16_t (*unlink)(const struct xfs_engine_mount_t* mount, const char* path);
    int16_t (*mkdir)(const struct xfs_engine_mount_t* mount, const char* path);
    int16_t (*rmdir)(const struct xfs_engine_mount_t* mount, const char* path);
    int16_t (*chdir)(const struct xfs_engine_mount_t* mount, const char* path);
    int16_t (*getcwd)(const struct xfs_engine_mount_t* mount, char* buffer, uint16_t size);
    int16_t (*rename)(const struct xfs_engine_mount_t* mount, const char* old_path, const char* new_path);
    void (*free_handle)(const struct xfs_engine_mount_t* mount, struct xfs_handle_t* handle);
};

enum xfs_handle_type_t
{
    XFS_HANDLE_TYPE_NONE = 0,
    XFS_HANDLE_TYPE_FILE = 1,
    XFS_HANDLE_TYPE_DIR = 2,
};

struct xfs_handle_t
{
    enum xfs_handle_type_t type;
    uint8_t mount_point;
    void* data;
};

struct xfs_engine_mount_t
{
    const struct xfs_engine_t* engine;
    void* mount_data;
};

#pragma pack(push, 1)

struct xfs_args_mount_t
{
    char protocol[32];
    char hostname[64];
    char path[160];
};

struct xfs_args_open_t
{
    char path[128];
    uint16_t flags;
    uint16_t mode;
};

struct xfs_args_read_t
{
    uint16_t size;
    uint8_t reserved[254];
};

struct xfs_args_write_t
{
    uint16_t size;
    uint8_t reserved[254];
};

struct xfs_args_opendir_t
{
    char path[256];
};

struct xfs_args_stat_t
{
    char path[256];
};

struct xfs_args_unlink_t
{
    char path[256];
};

struct xfs_args_mkdir_t
{
    char path[256];
};

struct xfs_args_rmdir_t
{
    char path[256];
};

struct xfs_args_chdir_t
{
    char path[256];
};

struct xfs_args_getcwd_t
{
    uint16_t buffer_size;
    uint8_t reserved[254];
};

struct xfs_args_rename_t
{
    char old_path[128];
    char new_path[128];
};

struct xfs_args_lseek_t
{
    uint32_t offset;
    uint8_t whence;
    uint8_t reserved[251];
};

struct xfs_stat_t
{
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint32_t size;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
};

union xfs_arguments_t
{
    uint8_t raw[256];
    struct xfs_args_mount_t mount;
    struct xfs_args_open_t open;
    struct xfs_args_read_t read;
    struct xfs_args_write_t write;
    struct xfs_args_opendir_t opendir;
    struct xfs_args_stat_t stat;
    struct xfs_args_unlink_t unlink;
    struct xfs_args_mkdir_t mkdir;
    struct xfs_args_rmdir_t rmdir;
    struct xfs_args_chdir_t chdir;
    struct xfs_args_getcwd_t getcwd;
    struct xfs_args_rename_t rename;
    struct xfs_args_lseek_t lseek;
};

struct xfs_registers_t
{
    uint8_t command;
    uint8_t status;
    int16_t result;
    uint8_t file_handle;
    uint8_t saved_page_a;
    uint8_t mount_point;
    uint8_t reserved[1];
    union xfs_arguments_t arguments;
    union
    {
        uint8_t tmp[248];
        struct
        {
            uint16_t remaining;
            uint16_t total;
        } fops;
    };
    uint8_t workspace[1024];
    uint8_t module_space[2560];
};

#pragma pack(pop)

_Static_assert(sizeof(struct xfs_registers_t) == 4096, "xfs registers must fit one page");

struct xfs_state_t
{
    struct xfs_registers_t registers;
    struct xfs_engine_mount_t mounts[XFS_MAX_MOUNTS];
    struct xfs_handle_t handles[XFS_MAX_FDS];
    char base_path[PATH_MAX];
};

extern void xfs_init(struct computer_t* computer);
extern void xfs_reset(struct computer_t* computer);
extern uint8_t xfs_read(struct computer_t* computer, uint16_t address);
extern void xfs_write(struct computer_t* computer, uint16_t address, uint8_t value);
extern void xfs_handle_command(struct computer_t* computer, struct xfs_registers_t* registers);
extern void xfs_free(struct computer_t* computer);
extern const struct xfs_engine_t xfs_ram_engine;

#endif

#ifndef ZX_SANDBOX_SPECTRANEXT_H
#define ZX_SANDBOX_SPECTRANEXT_H

#include <stdint.h>

struct computer_t;

#define SPECTRANEXT_STATUS_IN_PROGRESS (0xFFu)
#define SPECTRANEXT_STATUS_SUCCESS (0u)
#define SPECTRANEXT_STATUS_ERROR (1u)

enum spectranext_cmd_t
{
    SPECTRANEXT_CMD_GET_CONTROLLER_STATUS = 0,
    SPECTRANEXT_CMD_WIFI_SCAN_ACCESS_POINTS = 1,
    SPECTRANEXT_CMD_WIFI_GET_ACCESS_POINT = 2,
    SPECTRANEXT_CMD_WIFI_CONNECT_ACCESS_POINT = 3,
    SPECTRANEXT_CMD_WIFI_DISCONNECT = 4,
    SPECTRANEXT_CMD_DNS_GETHOSTBYNAME = 5,
    SPECTRANEXT_CMD_ENGINECALL = 6,
};

#define WIFI_CONTROLLER_STATUS_OFFLINE (0u)
#define WIFI_CONTROLLER_STATUS_BUSY_UPDATING (1u)
#define WIFI_CONTROLLER_STATUS_OPERATIONAL (2u)

#define WIFI_CONNECT_DISCONNECTED (0)
#define WIFI_CONNECT_CONNECTING (1)
#define WIFI_CONNECT_CONNECT_SUCCESS (2)
#define WIFI_CONNECT_CONNECT_IP_OBTAINED (3)

#define SPECTRANEXT_CONTROLLER_PAGE (0x48u)
#define SPECTRANEXT_SCAN_AP_MAX (64u)
#define SPECTRANEXT_CMD_REG_IDLE (0xFFu)

#pragma pack(push, 1)

typedef struct spectranext_get_status_out_s
{
    uint8_t controller_status;
    int8_t wifi_connection;
    uint32_t ipv4;
} spectranext_get_status_out_t;

typedef union spectranext_workspace_u
{
    struct
    {
        spectranext_get_status_out_t out;
    } get_controller_status;

    struct
    {
        union
        {
            struct
            {
                char host[64];
            } in;
            struct
            {
                uint32_t ipv4;
            } out;
        } io;
    } dns;

    struct
    {
        union
        {
            struct
            {
                char ssid[64];
                char password[64];
            } in;
        } io;
    } wifi_connect;

    struct
    {
        union
        {
            struct
            {
                uint8_t ap_index;
            } in;
            struct
            {
                char ap_name[64];
            } out;
        } io;
    } wifi_get_ap;

    struct
    {
        union
        {
            struct
            {
                uint8_t scan_count;
            } out;
        } io;
    } wifi_scan;

    struct
    {
        struct
        {
            char input_file[128];
            char output_file[128];
            char operation[256];
        } io;
    } enginecall;

    char page[4094];
} spectranext_workspace_t;

struct spectranext_controller_t
{
    uint8_t command;
    uint8_t status;
    spectranext_workspace_t workspace;
};

struct spectranext_state_t
{
    uint8_t controller_status;
    int8_t connection_status;
    uint32_t ipv4_host;
};

#pragma pack(pop)

_Static_assert(sizeof(struct spectranext_controller_t) == 4096, "spectranext controller must fit one page");

extern void spectranext_controller_init(struct computer_t* computer);
extern uint8_t spectranext_controller_read(struct computer_t* computer, uint16_t address);
extern void spectranext_controller_write(struct computer_t* computer, uint16_t address, uint8_t value);
extern void spectranext_stdout_write(struct computer_t* computer, uint16_t port, uint8_t data);

#endif

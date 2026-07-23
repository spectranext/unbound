#include "computer/spectranext.h"

#include "computer.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

static char scan_ap_names[SPECTRANEXT_SCAN_AP_MAX][64];
static uint8_t scan_ap_count;

static void spectranext_set_status(struct computer_t* computer, uint8_t status)
{
    computer->spectranext_controller.status = status;
}

static int spectranext_enginecall_dispatch(const char* input_file, const char* output_file, const char* operation)
{
    (void)input_file;
    (void)output_file;
    (void)operation;
    return -1;
}

static void spectranext_controller_process_command(struct computer_t* computer)
{
    const uint8_t cmd = computer->spectranext_controller.command;
    computer->spectranext_controller.command = SPECTRANEXT_CMD_REG_IDLE;

    switch (cmd)
    {
        case SPECTRANEXT_CMD_GET_CONTROLLER_STATUS:
            computer->spectranext_controller.workspace.get_controller_status.out.controller_status =
                computer->spectranext_state.controller_status;
            computer->spectranext_controller.workspace.get_controller_status.out.wifi_connection =
                computer->spectranext_state.connection_status;
            computer->spectranext_controller.workspace.get_controller_status.out.ipv4 =
                computer->spectranext_state.ipv4_host;
            spectranext_set_status(computer, SPECTRANEXT_STATUS_SUCCESS);
            break;

        case SPECTRANEXT_CMD_WIFI_SCAN_ACCESS_POINTS:
            scan_ap_count = 1;
            strncpy(scan_ap_names[0], "spectranext", sizeof(scan_ap_names[0]) - 1);
            scan_ap_names[0][sizeof(scan_ap_names[0]) - 1] = '\0';
            computer->spectranext_controller.workspace.wifi_scan.io.out.scan_count = scan_ap_count;
            spectranext_set_status(computer, SPECTRANEXT_STATUS_SUCCESS);
            break;

        case SPECTRANEXT_CMD_WIFI_GET_ACCESS_POINT:
        {
            const uint8_t idx = computer->spectranext_controller.workspace.wifi_get_ap.io.in.ap_index;
            if (idx >= scan_ap_count)
            {
                spectranext_set_status(computer, SPECTRANEXT_STATUS_ERROR);
                break;
            }

            strncpy(
                computer->spectranext_controller.workspace.wifi_get_ap.io.out.ap_name,
                scan_ap_names[idx],
                sizeof(computer->spectranext_controller.workspace.wifi_get_ap.io.out.ap_name) - 1);
            computer->spectranext_controller.workspace.wifi_get_ap.io.out.ap_name[
                sizeof(computer->spectranext_controller.workspace.wifi_get_ap.io.out.ap_name) - 1] = '\0';
            spectranext_set_status(computer, SPECTRANEXT_STATUS_SUCCESS);
            break;
        }

        case SPECTRANEXT_CMD_WIFI_CONNECT_ACCESS_POINT:
            computer->spectranext_state.connection_status = WIFI_CONNECT_CONNECT_SUCCESS;
            spectranext_set_status(computer, SPECTRANEXT_STATUS_SUCCESS);
            break;

        case SPECTRANEXT_CMD_WIFI_DISCONNECT:
            computer->spectranext_state.connection_status = WIFI_CONNECT_DISCONNECTED;
            spectranext_set_status(computer, SPECTRANEXT_STATUS_SUCCESS);
            break;

        case SPECTRANEXT_CMD_DNS_GETHOSTBYNAME:
        {
            char host[64];
            memcpy(host, computer->spectranext_controller.workspace.dns.io.in.host, sizeof(host) - 1);
            host[sizeof(host) - 1] = '\0';

            if (!host[0])
            {
                computer->spectranext_controller.workspace.dns.io.out.ipv4 = 0;
                spectranext_set_status(computer, SPECTRANEXT_STATUS_ERROR);
                break;
            }

            struct addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            struct addrinfo* res = NULL;
            const int gai_err = getaddrinfo(host, NULL, &hints, &res);
            if (gai_err != 0 || res == NULL)
            {
                computer->spectranext_controller.workspace.dns.io.out.ipv4 = 0;
                spectranext_set_status(computer, SPECTRANEXT_STATUS_ERROR);
                if (res)
                    freeaddrinfo(res);
                break;
            }

            uint32_t ipv4_host = 0;
            int found = 0;
            for (struct addrinfo* rp = res; rp != NULL; rp = rp->ai_next)
            {
                if (rp->ai_family == AF_INET && rp->ai_addr != NULL)
                {
                    const struct sockaddr_in* sin = (const struct sockaddr_in*)rp->ai_addr;
                    ipv4_host = (uint32_t)sin->sin_addr.s_addr;
                    found = 1;
                    break;
                }
            }
            freeaddrinfo(res);

            if (!found)
            {
                computer->spectranext_controller.workspace.dns.io.out.ipv4 = 0;
                spectranext_set_status(computer, SPECTRANEXT_STATUS_ERROR);
                break;
            }

            computer->spectranext_controller.workspace.dns.io.out.ipv4 = ipv4_host;
            computer->spectranext_state.ipv4_host = ipv4_host;
            spectranext_set_status(computer, SPECTRANEXT_STATUS_SUCCESS);
            break;
        }

        case SPECTRANEXT_CMD_ENGINECALL:
        {
            char input_file[128];
            char output_file[128];
            char operation[256];

            memcpy(input_file, computer->spectranext_controller.workspace.enginecall.io.input_file,
                   sizeof(input_file) - 1);
            input_file[sizeof(input_file) - 1] = '\0';
            memcpy(output_file, computer->spectranext_controller.workspace.enginecall.io.output_file,
                   sizeof(output_file) - 1);
            output_file[sizeof(output_file) - 1] = '\0';
            memcpy(operation, computer->spectranext_controller.workspace.enginecall.io.operation,
                   sizeof(operation) - 1);
            operation[sizeof(operation) - 1] = '\0';

            const int result = spectranext_enginecall_dispatch(input_file, output_file, operation);
            spectranext_set_status(computer, (uint8_t)(int8_t)result);
            break;
        }

        default:
            spectranext_set_status(computer, SPECTRANEXT_STATUS_ERROR);
            break;
    }
}

void spectranext_controller_init(struct computer_t* computer)
{
    memset(&computer->spectranext_controller, 0, sizeof(computer->spectranext_controller));
    computer->spectranext_controller.command = SPECTRANEXT_CMD_REG_IDLE;
    computer->spectranext_controller.status = SPECTRANEXT_STATUS_SUCCESS;
    computer->spectranext_state.controller_status = WIFI_CONTROLLER_STATUS_OPERATIONAL;
    computer->spectranext_state.connection_status = WIFI_CONNECT_CONNECT_IP_OBTAINED;
    computer->spectranext_state.ipv4_host = 0x7f000001u;
    scan_ap_count = 0;
}

uint8_t spectranext_controller_read(struct computer_t* computer, uint16_t address)
{
    const uint16_t offset = address & 0x0fff;
    uint8_t* registers = (uint8_t*)&computer->spectranext_controller;
    return registers[offset];
}

void spectranext_controller_write(struct computer_t* computer, uint16_t address, uint8_t value)
{
    const uint16_t offset = address & 0x0fff;
    uint8_t* registers = (uint8_t*)&computer->spectranext_controller;
    const uint8_t old_command = registers[0];

    registers[offset] = value;
    if (offset == 0 && old_command == SPECTRANEXT_CMD_REG_IDLE && value != SPECTRANEXT_CMD_REG_IDLE)
        spectranext_controller_process_command(computer);
}

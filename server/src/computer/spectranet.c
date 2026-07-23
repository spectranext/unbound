#include "computer.h"
#include "computer/w5100.h"

static uint8_t convertSpectranetPageRead(uint8_t page)
{
    if (page >= 0xC0) { // ram
        return MEM_PAGE_SPECTRANET_RAM + (page - 0xC0);
    } else if (page <= 0x1F) { // rom
        return MEM_PAGE_SPECTRANET_ROM + page;
    } else {
        return MEM_PAGE_SCRATCH;
    }
}

static uint8_t convertSpectranetPageWrite(uint8_t page)
{
    if (page >= 0xC0) { // ram
        return MEM_PAGE_SPECTRANET_RAM + (page - 0xC0);
    } else {
        return MEM_PAGE_SCRATCH;
    }
}

static uint8_t checkW5100Page(uint8_t page)
{
    return page >= 0x40 && page <= 0x47;
}

static uint8_t checkFlashPage(uint8_t page)
{
    return page <= 0x1F;
}

void computer_spectranet_refresh_pages(struct computer_t* computer)
{
    if (!computer->state.spectranet_paged_in) {
        return;
    }

    if (checkW5100Page(computer->state.spectranet_page_a))
    {
        computer->state.w5100_page_a = computer->state.spectranet_page_a;
        computer->state.flash_page_a = 0xFF;
    }
    else
    {
        computer->state.memory_page_read_map[1] = convertSpectranetPageRead(computer->state.spectranet_page_a);
        computer->state.memory_page_write_map[1] = convertSpectranetPageWrite(computer->state.spectranet_page_a);

        if (checkFlashPage(computer->state.spectranet_page_a))
        {
            computer->state.w5100_page_a = computer->state.spectranet_page_a;
        }
        else
        {
            computer->state.w5100_page_a = 0xFF;
        }

        computer->state.w5100_page_a = 0xFF;
    }

    if (checkW5100Page(computer->state.spectranet_page_b))
    {
        computer->state.w5100_page_b = computer->state.spectranet_page_b;
        computer->state.flash_page_b = 0xFF;
    }
    else
    {
        computer->state.memory_page_read_map[2] = convertSpectranetPageRead(computer->state.spectranet_page_b);
        computer->state.memory_page_write_map[2]  = convertSpectranetPageWrite(computer->state.spectranet_page_b);

        if (checkFlashPage(computer->state.spectranet_page_b))
        {
            computer->state.flash_page_b = computer->state.spectranet_page_b;
        }
        else
        {
            computer->state.flash_page_b = 0xFF;
        }

        computer->state.w5100_page_b = 0xFF;
    }
}

void spectranet_page_in(struct computer_t* computer, uint8_t io)
{
    // Memory pages for Spectranet

    // sector 0x0000...0x0fff is permanently mapped into page 0 ROM
    computer->state.memory_page_read_map[0] = MEM_PAGE_SPECTRANET_ROM;
    // sector 0x3000...0x3fff is permanently mapped into page 0 RAM
    computer->state.memory_page_read_map[3] = MEM_PAGE_SPECTRANET_RAM;

    // sector 0x0000...0x0fff is not writable
    computer->state.memory_page_write_map[0] = MEM_PAGE_SCRATCH;
    computer->state.memory_page_write_map[3] = MEM_PAGE_SPECTRANET_RAM;

    computer->state.spectranet_paged_in = 1;
    computer->state.spectranet_paged_in_io = io;

    computer_spectranet_refresh_pages(computer);
}

void spectranet_enable_programmable_trap(struct computer_t* computer, uint8_t trap)
{
    computer->state.spectranet_trap = trap;
}

void spectranet_supply_trap_pc(struct computer_t* computer, uint8_t val)
{
    if (computer->state.spectranet_trap_msb)
    {
        computer->state.spectranet_trap_pc = (computer->state.spectranet_trap_pc & 0x00ff) | ((uint16_t)val << 8);
    }
    else
    {
        computer->state.spectranet_trap_pc = (computer->state.spectranet_trap_pc & 0xff00) | val;
    }

    computer->state.spectranet_trap_msb = !computer->state.spectranet_trap_msb;
}

void spectranet_page_out(struct computer_t* computer)
{
    computer->state.memory_page_write_map[0] = MEM_PAGE_SCRATCH;
    computer->state.memory_page_write_map[1] = MEM_PAGE_SCRATCH;
    computer->state.memory_page_write_map[2] = MEM_PAGE_SCRATCH;
    computer->state.memory_page_write_map[3] = MEM_PAGE_SCRATCH;

    computer->state.memory_page_read_map[0] = MEM_PAGE_ROM;
    computer->state.memory_page_read_map[1] = MEM_PAGE_ROM + 1;
    computer->state.memory_page_read_map[2] = MEM_PAGE_ROM + 2;
    computer->state.memory_page_read_map[3] = MEM_PAGE_ROM + 3;

    computer->state.w5100_page_a = 0xFF;
    computer->state.w5100_page_b = 0xFF;
    computer->state.flash_page_a = 0xFF;
    computer->state.flash_page_b = 0xFF;

    computer->state.spectranet_paged_in = 0;
    computer->state.spectranet_paged_in_io = 0;
}

void spectranet_set_page_a(struct computer_t* computer, uint8_t val)
{
    computer->state.spectranet_page_a = val;
    computer_spectranet_refresh_pages(computer);
}

void spectranet_set_page_b(struct computer_t* computer, uint8_t val)
{
    computer->state.spectranet_page_b = val;
    computer_spectranet_refresh_pages(computer);
}

static void erase_spectranet_rom(struct computer_t* computer, uint32_t from, uint32_t size, uint8_t b)
{
    memset(computer->state.memory + MEM_PAGE_SPECTRANET_ROM * MEM_PAGE_SIZE + from, b, size);
}

static void write_spectranet_rom(struct computer_t* computer, uint32_t addr, uint8_t val)
{
    computer->state.memory[MEM_PAGE_SPECTRANET_ROM * MEM_PAGE_SIZE + addr] = val;
}

static uint16_t get_w5100_register(uint8_t page, uint16_t address)
{
    uint16_t base_address = (uint16_t)((page - 0x40) * 0x1000);
    return base_address + (address & 0xfff);
}

uint8_t spectranet_w5100_read(struct computer_t* computer, uint8_t page, uint16_t address)
{
    return nic_w5100_read(&computer->spectranet_w5100, get_w5100_register(page, address));
}

void spectranet_w5100_write(struct computer_t* computer, uint8_t page, uint16_t address, uint8_t value)
{
    address &= 0xfff;
    nic_w5100_write(&computer->spectranet_w5100, get_w5100_register(page, address), value);
}

void spectranet_flash_write(struct computer_t* computer, uint8_t page, uint16_t address, uint8_t val)
{
    address &= 0xfff;
    
    uint16_t masked_addr = address & 0x7ff;
    
    if (computer->state.spectranet_flash_state == 0)
    {
        if( masked_addr == 0x555 && val == 0xaa )
        {
            computer->state.spectranet_flash_state = 1;
        }
    }
    else
    {
        switch (computer->state.spectranet_flash_state)
        {
            case 1:
            {
                if (masked_addr == 0x2aa && val == 0x55)
                {
                    computer->state.spectranet_flash_state = 2;
                }
                break;
            }
            case 2:
            {
                if (masked_addr == 0x555)
                {
                    if( val == 0xa0 )
                    {
                        computer->state.spectranet_flash_state = 6;
                    }
                    else if( val == 0x80 )
                    {
                        computer->state.spectranet_flash_state = 3;
                    }
                    else
                    {
                        computer->state.spectranet_flash_state = 0;
                    }
                }
                break;
            }
            case 3:
            {
                if (masked_addr == 0x555 && val == 0xaa)
                {
                    computer->state.spectranet_flash_state = 4;
                }
                break;
            }
            case 4:
            {
                if (masked_addr == 0x2aa && val == 0x55)
                {
                    computer->state.spectranet_flash_state = 5;
                }
                break;
            }
            case 5:
            {
                if (masked_addr == 0x555 && val == 0x10) {
                    erase_spectranet_rom(computer, 0, 32 * 0x1000, 0xFF);
                    computer->state.spectranet_flash_state = 0;
                } else if (val == 0x30) {
                    erase_spectranet_rom(computer, (page / 4) * 0x4000, 0x4000, 0xFF);
                    computer->state.spectranet_flash_state = 0;
                }
                break;
            }
            case 6:
            {
                write_spectranet_rom(computer, page * 0x1000 + (address & 0x0fff), val);
                computer->state.spectranet_flash_state = 0;
            }
        }

        if (val == 0x0f)
        {
            computer->state.spectranet_flash_state = 0;
        }
    }
}

void spectranet_check_pc_pre_fetch(struct computer_t* computer, zuint16 address)
{
    if (computer->cpu.pc.uint16_value == 0x0008)
    {
        spectranet_page_in(computer, 0);
    }

    if ((computer->cpu.pc.uint16_value & 0xfff8) == 0x3ff8)
    {
        spectranet_page_in(computer, 0);
    }

    if ((computer->cpu.pc.uint16_value == 0x0066) && computer->state.spectranet_nmi_flip_flop)
    {
        spectranet_page_in(computer, 0);
    }
}

void spectranet_check_pc_post_fetch(struct computer_t* computer)
{
}

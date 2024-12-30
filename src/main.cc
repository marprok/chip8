#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;

static constexpr std::size_t MAX_ADDR = 0x1000;

std::uint16_t read_program(const fs::path& program_file, std::array<std::uint8_t, MAX_ADDR>& RAM, const std::uint16_t base_addr = 0x200)
{
    std::fstream in { program_file, in.binary | in.in };
    if (!in.is_open())
        return 0;
    const auto file_size = fs::file_size(program_file);
    if (base_addr + file_size >= RAM.size())
        return 0;
    if (!in.read(reinterpret_cast<char*>(RAM.data() + base_addr), file_size))
        return 0;

    return base_addr + file_size;
}

inline void TODO(const std::string_view instruction)
{
    std::cout << "TODO: " << instruction << '\n';
}

int main(int argc, char** argv)
{
    static_cast<void>(argc);
    static_cast<void>(argv);

    std::array<std::uint8_t, MAX_ADDR> RAM {};
    std::array<std::uint8_t, 0x10>     V {};
    std::array<std::uint16_t, 0x10>    STACK {};
    std::uint8_t                       I {}, delay {}, sound {}, SP {};
    std::uint16_t                      PC {};

    const std::uint16_t program_base = 0x200;
    const std::uint16_t program_end  = read_program("../1-chip8-logo.ch8", RAM, program_base) - 1;
    if (program_end == 0)
    {
        std::cerr << "Could not read the program file\n";
        return 1;
    }
    std::cout << "Program base: " << std::hex << std::showbase << program_base << ", Program end: " << program_end << std::endl;
    std::cout << "Total: " << std::dec << program_end - program_base + 1 << " bytes" << std::endl;

    PC = program_base;

    while (PC <= program_end)
    {
        // 0000 0000
        const std::uint8_t opcode = RAM[PC] >> 4;
        switch (opcode)
        {
        case 0x0:
        {
            switch (RAM[PC + 1])
            {
            case 0xE0: // clear
                TODO("Clear the display");
                break;
            case 0xEE:
                TODO("Return from a subroutine");
                break;
            default:
                std::cout << std::noshowbase << std::hex << std::setfill('0') << std::setw(4)
                          << PC << ": "
                          << std::setw(2)
                          << (std::uint16_t)RAM[PC]
                          << " " << std::setw(2)
                          << (std::uint16_t)RAM[PC + 1]
                          << std::endl;
                break;
            }
            break;
        }
        case 0xA:
        {
            TODO("Set I = nnn");
            break;
        }
        case 0x6:
        {
            TODO("Set Vx = kk");
            break;
        }
        case 0xD:
        {
            TODO("Display n-byte sprite starting at memory location I at (Vx, Vy), set VF = collision");
            break;
        }
        case 0x1:
        {
            TODO("Jump to location nnn");
            break;
        }
        default:
        {
            std::cout << std::noshowbase << std::hex << std::setfill('0') << std::setw(4)
                      << PC << ": "
                      << std::setw(2)
                      << (std::uint16_t)RAM[PC]
                      << " " << std::setw(2)
                      << (std::uint16_t)RAM[PC + 1]
                      << std::endl;
            break;
        }
        }
        PC += 2;
    }
    return 0;
}

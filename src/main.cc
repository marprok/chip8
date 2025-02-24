#include <SDL2/SDL.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;

static constexpr std::size_t  MAX_ADDR = 0x1000;
static constexpr std::int32_t WIDTH    = 64;
static constexpr std::int32_t HEIGHT   = 32;

template <std::int32_t W = 64, std::int32_t H = 32>
struct Display
{
    ~Display()
    {
        /* Frees memory */
        SDL_FreeSurface(m_surf);
        SDL_DestroyTexture(m_ttex);
        SDL_DestroyRenderer(m_rend);
        SDL_DestroyWindow(m_win);
    }

    bool init() noexcept
    {
        m_win = SDL_CreateWindow("CHIP-8",                /* Title of the SDL window */
                                 SDL_WINDOWPOS_UNDEFINED, /* Position x of the window */
                                 SDL_WINDOWPOS_UNDEFINED, /* Position y of the window */
                                 W,                       /* Width of the window in pixels */
                                 H,                       /* Height of the window in pixels */
                                 0);                      /* Additional flag(s) */
        if (!m_win)
        {
            std::cerr << "SDL window failed to initialise: " << SDL_GetError() << '\n';
            return false;
        }
        // TODO: frame cap flag??
        m_rend = SDL_CreateRenderer(m_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_PRESENTVSYNC);
        if (m_rend == NULL)
        {
            std::cerr << "SDL renderer failed to initialise: " << SDL_GetError() << '\n';
            return false;
        }
        // TODO: endianes check
        m_surf = SDL_CreateRGBSurfaceFrom(m_memory.data(), W, H, 32, 4 * W, 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
        if (m_surf == NULL)
        {
            std::cerr << "SDL surface cration failed: " << SDL_GetError() << '\n';
            return false;
        }

        m_ttex = SDL_CreateTexture(m_rend, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, WIDTH, HEIGHT);
        if (m_ttex == NULL)
        {
            std::cerr << "SDL could not create texturetarget: " << SDL_GetError() << '\n';
            return false;
        }

        return true;
    }

    inline bool set_pixels(std::int32_t x, std::int32_t y, std::uint8_t sprite_row)
    {
        bool ret = false;
        for (std::uint8_t i = 0; i < 8u && (x + i) < W; ++i)
        {
            const std::uint8_t old_value          = m_memory[y * W * 4 + (x + i) * 4];
            m_memory[y * W * 4 + (x + i) * 4 + 2] = m_memory[y * W * 4 + (x + i) * 4 + 1] = m_memory[y * W * 4 + (x + i) * 4] ^= ((sprite_row >> (7 - i)) & 0x1) * 255;
            // collision happened?
            ret |= !m_memory[y * W * 4 + (x + i) * 4] && old_value;
        }

        return ret;
    }

    inline void draw()
    {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(m_rend, m_surf);
        if (tex == NULL)
        {
            std::cerr << "SDL could not create temp texture: " << SDL_GetError() << '\n';
            return;
        }
        SDL_RenderClear(m_rend);

        SDL_SetRenderTarget(m_rend, m_ttex);
        SDL_RenderClear(m_rend);
        SDL_RenderCopy(m_rend, tex, NULL, NULL);

        SDL_SetRenderTarget(m_rend, NULL);
        SDL_RenderCopy(m_rend, m_ttex, NULL, NULL);
        SDL_RenderPresent(m_rend);
        SDL_DestroyTexture(tex);
    }

    inline void reset(std::uint8_t value)
    {
        for (std::int32_t i = 0; i < W * H * 4; i += 4)
        {
            m_memory[i] = m_memory[i + 1] = m_memory[i + 2] = value;
            m_memory[i + 3]                                 = 0xFF;
        }
    }

private:
    SDL_Window*   m_win {};
    SDL_Renderer* m_rend {};
    SDL_Surface*  m_surf {};
    SDL_Texture*  m_ttex {}; // target texture

    std::array<std::uint8_t, W * 4 * H> m_memory {};
};

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

int run(std::string_view chip8_img)
{
    Display display;
    if (!display.init())
        return 1;

    std::array<std::uint8_t, MAX_ADDR> RAM {};
    std::array<std::uint8_t, 0x10>     V {};
    // std::array<std::uint16_t, 0x10>    STACK {};
    // std::uint8_t  delay {}, sound {}, SP {};
    std::uint16_t PC {}, I {};

    const std::uint16_t program_base = 0x200;
    const std::uint16_t program_end  = read_program(chip8_img, RAM, program_base) - 1;
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
        //  instructions are stored in big endian order in RAM
        const std::uint16_t operation = (static_cast<std::uint16_t>(RAM[PC]) << 8) | RAM[PC + 1];
        PC += 2;
        switch (operation >> 12)
        {
        case 0x0:
        {
            switch (operation & 0xFF)
            {
            case 0xE0: // clear
            {
                display.reset(0);
            }
            break;
            case 0xEE:
                TODO("Return from a subroutine");
                break;
            default:
                break;
            }
            break;
        }
        case 0xA:
        {
            I = operation & 0x0FFF;
            break;
        }
        case 0x6:
        {
            V[(operation >> 8) & 0x0F] = operation & 0xFF;
            break;
        }
        case 0xD:
        {
            const std::uint8_t x = V[(operation >> 8) & 0x0F] % WIDTH;
            const std::uint8_t y = V[(operation >> 4) & 0x0F] % HEIGHT;
            const std::uint8_t n = operation & 0x0F; // sprite height
            for (std::uint8_t i = 0; i < n && (y + i) < HEIGHT; ++i)
            {
                // TODO: check if out of RAM bounds?
                if (display.set_pixels(x, y + i, RAM[I + i]))
                    V[0xF] = 1;
            }
            display.draw();
            break;
        }
        case 0x1:
        {
            PC = operation & 0x0FFF;
            break;
        }
        case 0x7:
        {
            V[(operation >> 8) & 0x0F] += operation & 0xFF;
            break;
        }
        case 0x3:
        {
            PC += (V[(operation >> 8) & 0x0F] == (operation & 0xFF)) * 2;
            break;
        }
        case 0x4:
        {
            PC += (V[(operation >> 8) & 0x0F] != (operation & 0xFF)) * 2;
            break;
        }
        case 0x5:
        {
            if (!(operation & 0xF))
            {
                PC += 2 * (V[(operation >> 8) & 0x0F] == V[(operation >> 4) & 0x0F]);
            }
            break;
        }
        case 0x8:
        {
            switch (operation & 0xF)
            {
            case 0x0:
            {
                V[(operation >> 8) & 0x0F] = V[(operation >> 4) & 0x0F];
                break;
            }
            case 0x1:
            {
                V[(operation >> 8) & 0x0F] |= V[(operation >> 4) & 0x0F];
                break;
            }
            case 0x2:
            {
                V[(operation >> 8) & 0x0F] &= V[(operation >> 4) & 0x0F];
                break;
            }
            case 0x3:
            {
                V[(operation >> 8) & 0x0F] ^= V[(operation >> 4) & 0x0F];
                break;
            }
            case 0x4:
            {
                const std::uint16_t sum    = V[(operation >> 8) & 0x0F] + V[(operation >> 4) & 0x0F];
                V[0xF]                     = sum > 0xFF;
                V[(operation >> 8) & 0x0F] = sum & 0xFF;
                break;
            }
            case 0x5:
            {
                V[0xF] = V[(operation >> 8) & 0x0F] > V[(operation >> 4) & 0x0F];
                V[(operation >> 8) & 0x0F] -= V[(operation >> 4) & 0x0F];
                break;
            }
            case 0x6:
            {
                V[0xF] = V[(operation >> 8) & 0x0F] & 0x1;
                V[(operation >> 8) & 0x0F] >>= 1;
                break;
            }
            case 0x7:
            {
                V[0xF]                     = V[(operation >> 4) & 0x0F] > V[(operation >> 8) & 0x0F];
                V[(operation >> 8) & 0x0F] = V[(operation >> 4) & 0x0F] - V[(operation >> 8) & 0x0F];
                break;
            }
            case 0xE:
            {
                V[0xF] = V[(operation >> 8) & 0x0F] >> 7;
                V[(operation >> 8) & 0x0F] <<= 1;
            }
            }
            break;
        }
        default:
        {
            break;
        }
        }

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
            case SDL_QUIT:
                SDL_Log("Quite event detected!");
                return 0;
            case SDL_WINDOWEVENT:
            {
                if (e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_RESIZED)
                    display.draw();
                break;
            }
            default:
                break;
            }
        }
    }

    return 0;
}

int main(int argc, char** argv)
{
    static_cast<void>(argc);
    static_cast<void>(argv);

    if (argc != 2)
    {
        std::cerr << "Error: no chip8 image given!\n";
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cerr << "SDL failed to initialise: " << SDL_GetError() << '\n';
        return 1;
    }

    int ret = run(argv[1]);
    SDL_Quit();
    return ret;
}

#include <SDL2/SDL.h>
#include <array>
#include <cstdint>
#include <cstring>
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
        m_win = SDL_CreateWindow("CHIP-8",
                                 SDL_WINDOWPOS_UNDEFINED,
                                 SDL_WINDOWPOS_UNDEFINED,
                                 W * 10,
                                 H * 10,
                                 0);
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

std::uint16_t load(const fs::path& program_file, std::array<std::uint8_t, MAX_ADDR>& RAM, const std::uint16_t base_addr = 0x200)
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
    std::array<std::uint16_t, 0x10>    STACK {};
    std::uint8_t                       SP {}; //, delay {}, sound {} ;
    std::uint16_t                      PC {}, I {};

    const std::uint16_t program_base = 0x200;
    const std::uint16_t program_end  = load(chip8_img, RAM, program_base) - 1;
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
        const std::uint8_t  n         = operation & 0xF;
        const std::uint8_t  kk        = operation & 0xFF;
        const std::uint16_t nnn       = operation & 0xFFF;
        const std::uint8_t  x         = (operation >> 8) & 0x0F;
        const std::uint8_t  y         = (operation >> 4) & 0x0F;
        PC += 2;
        switch (operation >> 12)
        {
        case 0x0:
        {
            switch (kk)
            {
            case 0xE0: // clear
            {
                display.reset(0);
                break;
            }
            case 0xEE:
            {
                // TODO: bounds checking
                if (SP == 0)
                {
                    std::cerr << "Stack underflow!\n";
                    return 1;
                }
                PC = STACK[--SP];
                break;
            }
            default:
            {
                break;
            }
            }
            break;
        }
        case 0xA:
        {
            I = nnn;
            break;
        }
        case 0x6:
        {
            V[x] = kk;
            break;
        }
        case 0xD:
        {
            const std::uint8_t cx = V[x] % WIDTH;
            const std::uint8_t cy = V[y] % HEIGHT;
            for (std::uint8_t i = 0; i < n && (cy + i) < HEIGHT; ++i)
            {
                if ((I + i) >= RAM.size())
                {
                    std::cerr << "Invalid memory access! " << (I + i) << '\n';
                    return 1;
                }
                if (display.set_pixels(cx, cy + i, RAM[I + i]))
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
        case 0x2:
        {
            if ((SP + 1u) == STACK.size())
            {
                std::cerr << "Stack overflow!\n";
                return 1;
            }
            STACK[SP++] = PC;
            PC          = nnn;
            break;
        }
        case 0x7:
        {
            V[x] += kk;
            break;
        }
        case 0x3:
        {
            PC += (V[x] == kk) * 2;
            break;
        }
        case 0x4:
        {
            PC += (V[x] != kk) * 2;
            break;
        }
        case 0x5:
        {
            if (!n)
            {
                PC += (V[x] == V[y]) * 2;
            }
            break;
        }
        case 0x8:
        {
            switch (n)
            {
            case 0x0:
            {
                V[x] = V[y];
                break;
            }
            case 0x1:
            {
                V[x] |= V[y];
                break;
            }
            case 0x2:
            {
                V[x] &= V[y];
                break;
            }
            case 0x3:
            {
                V[x] ^= V[y];
                break;
            }
            case 0x4:
            {
                const std::uint16_t sum = V[x] + V[y];
                V[0xF]                  = sum > 0xFF;
                V[x]                    = sum & 0xFF;
                break;
            }
            case 0x5:
            {
                V[0xF] = V[x] > V[y];
                V[x] -= V[y];
                break;
            }
            case 0x6:
            {
                V[0xF] = V[x] & 0x1;
                V[x] >>= 1;
                break;
            }
            case 0x7:
            {
                V[0xF] = V[y] > V[x];
                V[x]   = V[y] - V[x];
                break;
            }
            case 0xE:
            {
                V[0xF] = V[x] >> 7;
                V[x] <<= 1;
            }
            }
            break;
        }
        case 0xF:
        {
            switch (kk)
            {
            case 0x1E:
            {
                I += V[x];
                break;
            }
            case 0x33:
            {
                // TODO: this is ugly
                RAM[I]     = V[x] / 100;
                RAM[I + 1] = (V[x] % 100) / 10;
                RAM[I + 2] = V[x] % 10;
                break;
            }
            case 0x55:
            {
                std::memcpy(RAM.data() + I, V.data(), ((operation >> 8) & 0x0F) + 1);
                break;
            }
            case 0x65:
            {
                std::memcpy(V.data(), RAM.data() + I, ((operation >> 8) & 0x0F) + 1);
                break;
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

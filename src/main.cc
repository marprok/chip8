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

    /* Initialises data */
    SDL_Window* window = NULL;

    /*
     * Initialises the SDL video subsystem (as well as the events subsystem).
     * Returns 0 on success or a negative error code on failure using SDL_GetError().
     */
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cerr << "SDL failed to initialise: " << SDL_GetError() << '\n';
        return 1;
    }

    /* Creates a SDL window */
    window = SDL_CreateWindow("SDL Example",           /* Title of the SDL window */
                              SDL_WINDOWPOS_UNDEFINED, /* Position x of the window */
                              SDL_WINDOWPOS_UNDEFINED, /* Position y of the window */
                              WIDTH,                   /* Width of the window in pixels */
                              HEIGHT,                  /* Height of the window in pixels */
                              0);                      /* Additional flag(s) */

    /* Checks if window has been created; if not, exits program */
    if (window == NULL)
    {
        std::cerr << "SDL window failed to initialise: " << SDL_GetError() << '\n';
        return 1;
    }

    std::array<std::uint8_t, WIDTH * 4 * HEIGHT> DISPLAY {};
    SDL_Renderer*                                renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL)
    {
        std::cerr << "SDL renderer failed to initialise: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(DISPLAY.data(), WIDTH, HEIGHT, 32, 4 * WIDTH, 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
    if (surf == NULL)
    {
        std::cerr << "SDL surface cration failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Texture* texTarget = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, WIDTH, HEIGHT);
    if (texTarget == NULL)
    {
        std::cerr << "SDL could not create texturetarget: " << SDL_GetError() << '\n';
        return 1;
    }

    std::array<std::uint8_t, MAX_ADDR> RAM {};
    std::array<std::uint8_t, 0x10>     V {};
    std::array<std::uint16_t, 0x10>    STACK {};
    // consider making the rgb to point on a single byte?
    std::uint8_t  delay {}, sound {}, SP {};
    std::uint16_t PC {}, I {};

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
    for (std::size_t i = 0; i < DISPLAY.size(); ++i)
    {
        DISPLAY[i] = 0xFF;
    }

    while (PC <= program_end)
    {
        bool inc_pc = true;
        // instructions are stored in big endian order in RAM
        const std::uint16_t operation = (static_cast<std::uint16_t>(RAM[PC]) << 8) | RAM[PC + 1];
        switch (RAM[PC] >> 4)
        {
        case 0x0:
        {
            switch (RAM[PC + 1])
            {
            case 0xE0: // clear
            {
                for (std::size_t i = 0; i < HEIGHT; ++i)
                {
                    for (std::size_t j = 0; j < WIDTH; ++j)
                    {
                        DISPLAY[i * WIDTH * 4 + j * 4 + 2] = DISPLAY[i * WIDTH * 4 + j * 4 + 1] = DISPLAY[i * WIDTH * 4 + j * 4] = 0x00;
                    }
                }
                // TODO("Clear the display");
            }
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
            std::uint8_t       x = V[(operation >> 8) & 0x0F] % WIDTH;
            std::uint8_t       y = V[(operation >> 4) & 0x0F] % HEIGHT;
            const std::uint8_t n = operation & 0x0F; // sprite height

            for (std::uint8_t i = 0; i < n && (y + i) < HEIGHT; ++i)
            {
                // check if out of bounds?
                std::uint8_t sprite_row = RAM[I + i];
                for (std::uint8_t j = 0; j < 8u && (x + j) < WIDTH; ++j)
                {
                    const std::uint8_t pixel_value                 = ((sprite_row >> (7 - j)) & 0x1) * 255;
                    DISPLAY[(y + i) * WIDTH * 4 + (x + j) * 4 + 2] = DISPLAY[(y + i) * WIDTH * 4 + (x + j) * 4 + 1] = DISPLAY[(y + i) * WIDTH * 4 + (x + j) * 4] ^= pixel_value;
                }
            }
            std::cout << (int)x << " " << (int)y << " " << (int)n << std::endl;
            // TODO("Display n-byte sprite starting at memory location I at (Vx, Vy), set VF = collision");
            break;
        }
        case 0x1:
        {
            PC     = operation & 0x0FFF;
            inc_pc = false;
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

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
            case SDL_QUIT:
                SDL_Log("Quite event detected!");
                goto END;
                break;
            }
        }

        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        if (tex == NULL)
        {
            std::cerr << "SDL could not create texture: " << SDL_GetError() << '\n';
            return 1;
        }
        SDL_RenderClear(renderer);

        SDL_SetRenderTarget(renderer, texTarget);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, tex, NULL, NULL);

        SDL_SetRenderTarget(renderer, NULL);
        // SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texTarget, NULL, NULL);
        SDL_RenderPresent(renderer);

        SDL_DestroyTexture(tex);
        if (inc_pc)
            PC += 2;
    }

END:
    /* Frees memory */
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(texTarget);
    // SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    /* Shuts down all SDL subsystems */
    SDL_Quit();
    return 0;
}

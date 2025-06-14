#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <string_view>
#include <thread>

namespace fs = std::filesystem;

static constexpr std::size_t  MAX_ADDR = 0x1000;
static constexpr std::int32_t WIDTH    = 64;
static constexpr std::int32_t HEIGHT   = 32;

template <std::int32_t W = 64, std::int32_t H = 32>
struct Display
{
    ~Display()
    {
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
            //  collision happened?
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

template <std::int32_t AMPLITUDE = 28000, std::int32_t SAMPLES_PER_SEC = 44100>
struct AudioDevice
{
    bool init()
    {
        auto audio_callback = [](void* userdata, std::uint8_t* stream, std::int32_t len) -> void
        {
            const std::int32_t length    = len / 2; // AUDIO_S16SYS
            std::int16_t*      buffer    = reinterpret_cast<std::int16_t*>(stream);
            std::int32_t&      sample_nr = *reinterpret_cast<std::int32_t*>(userdata);

            for (std::int32_t i = 0; i < length; i++, sample_nr++)
            {
                double time = (double)sample_nr / (double)SAMPLES_PER_SEC;
                buffer[i]   = (std::int16_t)(AMPLITUDE * std::sin(2.0f * std::numbers::pi * 441.0f * time)); // render 441 HZ sine wave
            }
        };

        m_spec.freq     = SAMPLES_PER_SEC; // number of samples per second
        m_spec.format   = AUDIO_S16SYS;    // sample type (here: signed short i.e. 16 bit)
        m_spec.channels = 1;               // only one channel
        m_spec.samples  = 2048;            // buffer-size
        m_spec.callback = audio_callback;  // function SDL calls periodically to refill the buffer
        m_spec.userdata = &m_sample_nr;    // counter, keeping track of current sample number
        // TODO: clean up the audio lib
        SDL_AudioSpec have;
        if (SDL_OpenAudio(&m_spec, &have) != 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to open audio: %s", SDL_GetError());
            return false;
        }
        if (m_spec.format != have.format)
        {
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "Failed to get the desired AudioSpec");
            return false;
        }

        return true;
    }

private:
    SDL_AudioSpec m_spec {};
    std::uint32_t m_sample_nr {};
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

std::map<SDL_Keycode, std::uint8_t> key_mapping { { SDLK_1, 0x1 }, { SDLK_2, 0x2 }, { SDLK_3, 0x3 }, { SDLK_4, 0xC },\
                                                  { SDLK_q, 0x4 }, { SDLK_w, 0x5 }, { SDLK_e, 0x6 }, { SDLK_r, 0xD },\
                                                  { SDLK_a, 0x7 }, { SDLK_s, 0x8 }, { SDLK_d, 0x9 }, { SDLK_f, 0xE },\
                                                  { SDLK_z, 0xA }, { SDLK_x, 0x0 }, { SDLK_c, 0xB }, { SDLK_v, 0xF } };

// TODO: This is ugly but I am too tired do something
inline void load_font(std::array<std::uint8_t, MAX_ADDR>& RAM, const std::uint16_t base_addr = 0x0)
{
    const std::array<std::array<const std::uint8_t, 5>, 0x10> hex_sprite_font { { { 0xF0, 0x90, 0x90, 0x90, 0xF0 },     // 0
                                                                                  { 0x20, 0x60, 0x20, 0x20, 0x70 },     // 1
                                                                                  { 0xF0, 0x10, 0xF0, 0x80, 0xF0 },     // 2
                                                                                  { 0xF0, 0x10, 0xF0, 0x10, 0xF0 },     // 3
                                                                                  { 0x90, 0x90, 0xF0, 0x10, 0x10 },     // 4
                                                                                  { 0xF0, 0x80, 0xF0, 0x10, 0xF0 },     // 5
                                                                                  { 0xF0, 0x80, 0xF0, 0x90, 0xF0 },     // 6
                                                                                  { 0xF0, 0x10, 0x20, 0x40, 0x40 },     // 7
                                                                                  { 0xF0, 0x90, 0xF0, 0x90, 0xF0 },     // 8
                                                                                  { 0xF0, 0x90, 0xF0, 0x10, 0xF0 },     // 9
                                                                                  { 0xF0, 0x90, 0xF0, 0x90, 0x90 },     // A
                                                                                  { 0xE0, 0x90, 0xE0, 0x90, 0xE0 },     // B
                                                                                  { 0xF0, 0x80, 0x80, 0x80, 0xF0 },     // C
                                                                                  { 0xE0, 0x90, 0x90, 0x90, 0xE0 },     // D
                                                                                  { 0xF0, 0x80, 0xF0, 0x80, 0xF0 },     // E
                                                                                  { 0xF0, 0x80, 0xF0, 0x80, 0x80 } } }; // F

    // TODO: This should not fail but we should check that the address of the last sprite is within bounds(0x000 - 0x1FF)
    std::memcpy(RAM.data() + base_addr, hex_sprite_font.data(), hex_sprite_font.size() * 5);
}

int run(std::string_view chip8_img)
{
    Display display;
    if (!display.init())
        return 1;
    // clear the screen
    display.reset(0);

    AudioDevice device;
    if (!device.init())
        return 1;

    std::random_device                 rd("default");
    std::uniform_int_distribution      uid(0, 255);
    std::array<std::uint8_t, MAX_ADDR> RAM {};
    std::array<std::uint8_t, 0x10>     V {};
    std::array<std::uint16_t, 0x10>    STACK {};
    std::array<bool, 0x10>             key_pressed {};
    std::uint8_t                       SP {}, delay {}, sound {};
    std::uint16_t                      PC {}, I {};

    const std::uint16_t program_base = 0x200;
    const std::uint16_t program_end  = load(chip8_img, RAM, program_base) - 1;
    if (program_end == 0)
    {
        std::cerr << "Could not read the program file\n";
        return 1;
    }
    load_font(RAM);
    std::cout << "Program base: " << std::hex << std::showbase << program_base << ", Program end: " << program_end << std::endl;
    std::cout << "Total: " << std::dec << program_end - program_base + 1 << " bytes" << std::endl;
    constexpr auto FRAME_DURATION = std::chrono::milliseconds(2);
    constexpr auto TIMER_TICK     = std::chrono::microseconds(16700);

    PC                  = program_base;
    auto sound_accum_mc = std::chrono::microseconds(0);
    auto delay_accum_mc = std::chrono::microseconds(0);
    bool halt           = false;
    // indicate the id of the register in which to store the first key that is pressed and then released on a frame-by-frame basis
    std::uint8_t v_key = 0;
    while (PC <= program_end)
    {
        const auto start = std::chrono::steady_clock::now();
        if (!halt)
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
                case 0xE0:
                {
                    display.reset(0);
                    break;
                }
                case 0xEE:
                {
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
                    std::printf("operation %04X\n", operation);
                    break;
                }
                }
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
                PC += (V[x] == V[y]) * 2;
                break;
            }
            case 0x6:
            {
                V[x] = kk;
                break;
            }
            case 0x7:
            {
                V[x] += kk;
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
                    V[0xF] = 0;
                    break;
                }
                case 0x2:
                {
                    V[x] &= V[y];
                    V[0xF] = 0;
                    break;
                }
                case 0x3:
                {
                    V[x] ^= V[y];
                    V[0xF] = 0;
                    break;
                }
                case 0x4:
                {
                    const std::uint16_t sum = V[x] + V[y];
                    V[x]                    = sum & 0xFF;
                    V[0xF]                  = sum > 0xFF;
                    break;
                }
                case 0x5:
                {
                    const std::uint8_t flag = V[x] >= V[y];
                    V[x] -= V[y];
                    V[0xF] = flag;
                    break;
                }
                case 0x6:
                {
                    V[x]                    = V[y];
                    const std::uint8_t flag = V[x] & 0x1;
                    V[x] >>= 1;
                    V[0xF] = flag;
                    break;
                }
                case 0x7:
                {
                    const std::uint8_t flag = V[y] >= V[x];
                    V[x]                    = V[y] - V[x];
                    V[0xF]                  = flag;
                    break;
                }
                case 0xE:
                {
                    V[x]                    = V[y];
                    const std::uint8_t flag = V[x] >> 7;
                    V[x] <<= 1;
                    V[0xF] = flag;
                    break;
                }
                default:
                {
                    std::printf("operation %04X\n", operation);
                    break;
                }
                }
                break;
            }
            case 0x9:
            {
                PC += (V[x] != V[y]) * 2;
                break;
            }
            case 0xA:
            {
                I = nnn;
                break;
            }
            case 0xB:
            {
                PC = nnn + V[0];
                break;
            }
            case 0xC:
            {
                V[x] = uid(rd) & kk;
                break;
            }
            case 0xD:
            {
                V[0xF]                = 0;
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
            case 0xE:
            {
                switch (kk)
                {
                case 0xA1:
                {
                    PC += (!key_pressed[V[x]]) * 2;
                    break;
                }
                case 0x9E:
                {
                    PC += (key_pressed[V[x]]) * 2;
                    break;
                }
                default:
                {
                    std::printf("operation %04X\n", operation);
                    break;
                }
                }
                break;
            }
            case 0xF:
            {
                switch (kk)
                {
                case 0x7:
                {
                    V[x] = delay;
                    break;
                }
                case 0xA:
                {
                    halt  = true;
                    v_key = x;
                    for (auto& pressed : key_pressed)
                        pressed = false;
                    break;
                }
                case 0x15:
                {
                    delay          = V[x];
                    delay_accum_mc = std::chrono::microseconds(0);
                    break;
                }
                case 0x18:
                {
                    sound          = V[x];
                    sound_accum_mc = std::chrono::microseconds(0);
                    // start playing sound if we have a positive value in the sound reginster
                    if (sound > 0)
                        SDL_PauseAudio(0);
                    break;
                }
                case 0x1E:
                {
                    I += V[x];
                    break;
                }
                case 0x29:
                {
                    // TODO: take care of the base address
                    I = V[x] * 5;
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
                    I += ((operation >> 8) & 0x0F) + 1;
                    break;
                }
                case 0x65:
                {
                    std::memcpy(V.data(), RAM.data() + I, ((operation >> 8) & 0x0F) + 1);
                    I += ((operation >> 8) & 0x0F) + 1;
                    break;
                }
                default:
                {
                    std::printf("operation %04X\n", operation);
                    break;
                }
                }
                break;
            }
            default:
            {
                std::printf("operation %04X\n", operation);
                break;
            }
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
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            {
                if (auto key_code = key_mapping.find(e.key.keysym.sym); key_code != key_mapping.end())
                {
                    const bool new_state = (e.type == SDL_KEYDOWN);
                    if (halt && key_pressed[key_code->second] && !new_state)
                    {
                        V[v_key] = key_code->second;
                        halt     = false;
                        v_key    = 0;
                    }
                    key_pressed[key_code->second] = new_state;
                }
                break;
            }
            default:
                break;
            }
        }
        const std::chrono::duration<double, std::milli> diff = std::chrono::steady_clock::now() - start;
        if (diff < FRAME_DURATION)
            std::this_thread::sleep_for(FRAME_DURATION - diff);
        if (sound > 0)
        {
            sound_accum_mc += std::chrono::duration_cast<std::chrono::microseconds>(FRAME_DURATION);
            if (sound_accum_mc >= TIMER_TICK)
            {
                sound--;
                if (sound == 0)
                {
                    SDL_PauseAudio(1);
                }
                sound_accum_mc = std::chrono::microseconds(0);
            }
        }

        if (delay > 0)
        {
            delay_accum_mc += std::chrono::duration_cast<std::chrono::microseconds>(FRAME_DURATION);
            if (delay_accum_mc >= TIMER_TICK)
            {
                delay--;
                delay_accum_mc = std::chrono::microseconds(0);
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

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
    {
        std::cerr << "SDL failed to initialise: " << SDL_GetError() << '\n';
        return 1;
    }

    int ret = run(argv[1]);
    SDL_Quit();
    return ret;
}

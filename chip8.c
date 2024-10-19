#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "SDL2/SDL.h"

typedef struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID  dev;
} sdl_t;

typedef struct
{
    uint32_t window_width;
    uint32_t window_height;
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t scale_factor;
    bool pixelated;
    uint32_t insts_per_second;  //CPUU clock rate
    uint32_t square_wave_freq;
    uint32_t audio_sample_rate;
    int16_t volume;
} config_t;

typedef enum
{
    QUIT,
    RUNNING,
    PAUSED
} emu_state_t;

typedef struct
{
    uint16_t opcode;
    uint16_t NNN; // 12-bit address
    uint8_t NN;   // 8-bit address
    uint8_t N;    // 4-bit address
    uint8_t X;    // 4-bit register
    uint8_t Y;    // 4-bit register
} instruction_t;

typedef struct
{
    emu_state_t state;
    uint8_t ram[4096];     // total ram
    bool display[64 * 32]; // display matrix
    uint16_t stack[12];     // subroutine/callback stack
    uint16_t *stack_ptr;   // points to top of stack
    uint8_t V[16];         // data registers V[0] - V[F]
    uint16_t I;            // index register
    uint16_t PC;           // program counter
    uint8_t delay_timer;
    uint8_t sound_timer;
    bool keypad[16];      // hexadecimal keypad 0x0 - 0xF
    const char *rom_name; // current rom file
    instruction_t inst;   // current instruction
    bool draw;            // update the screen; Yes/No
} chip8_t;

// SDL audio callback
void audio_callback(void *userdata, uint8_t *stream, int len)
{
    config_t *config = (config_t *)userdata; 

    int16_t *audio_data = (int16_t *)stream;
    static uint32_t running_sample_index = 0;
    const int32_t square_wave_period = config->audio_sample_rate / config->square_wave_freq;
    const int32_t half_square_wave_period = square_wave_period / 2;

    memset(stream, 0, len);

    // checks whether volume should should be increased or decreased
    for(int i = 0; i < len / 2; i++)
    {
        audio_data[i] = (running_sample_index++ / half_square_wave_period) % 2 ? config->volume : -config->volume;
    }
}

// initialise SDL
bool init_sdl(sdl_t *sdl, config_t *config)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
    {
        SDL_Log("Could not initialise SDL subsystems! %s\n", SDL_GetError());
        return false;
    }

    // create window
    sdl->window = SDL_CreateWindow("CHIP-8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, config->window_width * config->scale_factor, config->window_height * config->scale_factor, 0);

    if (!sdl->window)
    {
        SDL_Log("Could not create window %s\n", SDL_GetError());
        return false;
    }

    // create renderer
    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);

    if (!sdl->renderer)
    {
        SDL_Log("Could not create renderer %s\n", SDL_GetError());
        return false;
    }

    // default configuration of sdl->want 
    sdl->want.freq = 44100;
    sdl->want.format = AUDIO_S16LSB;
    sdl->want.channels = 1;
    sdl->want.samples = 512;
    sdl->want.callback = audio_callback;
    sdl->want.userdata = config;

    sdl->dev = SDL_OpenAudioDevice(NULL, 0, &sdl->want, &sdl->have, 0);

    if(sdl->dev == 0)
    {
        SDL_Log("Could not get an audio device %s\n", SDL_GetError());
        return false;
    }

    if(sdl->want.format != sdl->have.format || sdl->want.channels != sdl->have.channels)
    {
        SDL_Log("Could not get desired audio spec\n");
        return false;
    }

    SDL_PauseAudioDevice(sdl->dev, 1);

    return true;
}

// Set up configurations
bool set_config(config_t *config, int argc, char **argv)
{
    // default values
    config->window_width = 64;  // original x res
    config->window_height = 32; // original y res
    config->fg_color = 0xFFFFFFFF;
    config->bg_color = 0x000000FF;
    config->scale_factor = 20;
    config->pixelated = true;
    config->insts_per_second = 700;
    config->square_wave_freq = 440;
    config->audio_sample_rate = 44100;
    config->volume = 3000;

    return true;
}

bool init_chip8(chip8_t *chip8, const char *rom_name)
{
    const uint32_t entry_point = 0x200;
    const uint8_t font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };

    //clear display array
    memset(chip8, 0, sizeof(chip8_t));

    // load font
    memcpy(&chip8->ram[0], font, sizeof(font));

    // load rom
    FILE *rom = fopen(rom_name, "rb");
    if (!rom)
    {
        SDL_Log("Rom file '%s' is invalid or does not exist!\n", rom_name);
        return false;
    }

    fseek(rom, 0, SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof chip8->ram - entry_point;
    rewind(rom);

    if (rom_size > max_size)
    {
        SDL_Log("Rom file '%s' is too big!\nRom size: %zu\nMax size: %zu", rom_name, rom_size, max_size);
        return false;
    }

    if (fread(&chip8->ram[entry_point], rom_size, 1, rom) != 1)
    {
        SDL_Log("Could not read rom file '%s' into memory!\n", rom_name);
        return false;
    }

    fclose(rom);

    chip8->state = RUNNING; // default machine state
    chip8->PC = entry_point;
    chip8->rom_name = rom_name;
    chip8->stack_ptr = &chip8->stack[0];
    chip8->sound_timer = 0;
    chip8->delay_timer = 0;
    chip8->draw = false;

    return true;
}

// cleanup SDL
void final_cleanup(sdl_t sdl)
{
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_CloseAudioDevice(sdl.dev);
    SDL_Quit();
    printf("cleaned!");
}

// clear screen to draw color
void clear_screen(const config_t config, const sdl_t sdl)
{
    uint8_t r = (config.bg_color >> 24) & 0xFF;
    uint8_t g = (config.bg_color >> 16) & 0xFF;
    uint8_t b = (config.bg_color >> 8) & 0xFF;
    uint8_t a = (config.bg_color >> 0) & 0xFF;

    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

// update screen with changes
void update_screen(const sdl_t sdl, const config_t config, const chip8_t chip8)
{
    SDL_Rect rect = {.x = 0, .y = 0, .w = (int)config.scale_factor, .h = (int)config.scale_factor};

    // color values to draw
    uint8_t fg_r = (config.fg_color >> 24) & 0xFF;
    uint8_t fg_g = (config.fg_color >> 16) & 0xFF;
    uint8_t fg_b = (config.fg_color >> 8) & 0xFF;
    uint8_t fg_a = (config.fg_color >> 0) & 0xFF;

    uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
    uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
    uint8_t bg_b = (config.bg_color >> 8) & 0xFF;
    uint8_t bg_a = (config.bg_color >> 0) & 0xFF;

    // loop through the display array and draw a rec per pixel
    for (uint32_t i = 0; i < sizeof chip8.display; i++)
    {
        // convert 1-D index i to 2-D index
        rect.x = (i % config.window_width) * config.scale_factor;
        rect.y = (i / config.window_width) * config.scale_factor;

        if (chip8.display[i]) // pixel is on
        {
            SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);

            if (config.pixelated)
            {
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
                SDL_RenderDrawRect(sdl.renderer, &rect);
            }
        }
        else // pixel is off
        {
            SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);
        }
    }

    SDL_RenderPresent(sdl.renderer);
}

// chip8 Keypad     QWERTY keypad
// 123C             1234
// 456D             qwer
// 789E             asdf
// A0BF             zxcv

// handle user inputs
void handle_inputs(chip8_t *chip8)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            chip8->state = QUIT;
            break;

        case SDL_KEYDOWN:
            switch (event.key.keysym.sym)
            {
            case SDLK_ESCAPE:
                chip8->state = QUIT;
                break;

            case SDLK_SPACE:
                if (chip8->state == RUNNING)
                {
                    chip8->state = PAUSED;
                    puts("====== PAUSED ======");
                }
                else
                    chip8->state = RUNNING;
                break;

            case SDLK_EQUALS:   //reset CHIP8 for current rom
                init_chip8(chip8, chip8->rom_name);
                break;

            // Map chip8 keypad
            case SDLK_1: chip8->keypad[0x1] = true; break;
            case SDLK_2: chip8->keypad[0x2] = true; break;
            case SDLK_3: chip8->keypad[0x3] = true; break;
            case SDLK_4: chip8->keypad[0xC] = true; break;

            case SDLK_q: chip8->keypad[0x4] = true; break;
            case SDLK_w: chip8->keypad[0x5] = true; break;
            case SDLK_e: chip8->keypad[0x6] = true; break;
            case SDLK_r: chip8->keypad[0xD] = true; break;

            case SDLK_a: chip8->keypad[0x7] = true; break;
            case SDLK_s: chip8->keypad[0x8] = true; break;
            case SDLK_d: chip8->keypad[0x9] = true; break;
            case SDLK_f: chip8->keypad[0xE] = true; break;

            case SDLK_z: chip8->keypad[0xA] = true; break;
            case SDLK_x: chip8->keypad[0x0] = true; break;
            case SDLK_c: chip8->keypad[0xB] = true; break;
            case SDLK_v: chip8->keypad[0xF] = true; break;

            default:
                break;
            }

            break;

        case SDL_KEYUP:
            switch (event.key.keysym.sym)
            {

            // Map chip8 keypad
            case SDLK_1: chip8->keypad[0x1] = false; break;
            case SDLK_2: chip8->keypad[0x2] = false; break;
            case SDLK_3: chip8->keypad[0x3] = false; break;
            case SDLK_4: chip8->keypad[0xC] = false; break;

            case SDLK_q: chip8->keypad[0x4] = false; break;
            case SDLK_w: chip8->keypad[0x5] = false; break;
            case SDLK_e: chip8->keypad[0x6] = false; break;
            case SDLK_r: chip8->keypad[0xD] = false; break;

            case SDLK_a: chip8->keypad[0x7] = false; break;
            case SDLK_s: chip8->keypad[0x8] = false; break;
            case SDLK_d: chip8->keypad[0x9] = false; break;
            case SDLK_f: chip8->keypad[0xE] = false; break;

            case SDLK_z: chip8->keypad[0xA] = false; break;
            case SDLK_x: chip8->keypad[0x0] = false; break;
            case SDLK_c: chip8->keypad[0xB] = false; break;
            case SDLK_v: chip8->keypad[0xF] = false; break;
                
            default:
                break;
            }

            break;

        default:
            break;
        }
    }
}

#ifdef DEBUG
void print_debug_info(chip8_t *chip8)
{
    printf("Address: 0x%04X, Opcode: 0x%04X, Desc: ", chip8->PC - 2, chip8->inst.opcode);
    switch ((chip8->inst.opcode >> 12) & 0x0F)
    {
    case 0x00:
        if (chip8->inst.NN == 0xE0) 
            printf("Clear screen\n");
        else if (chip8->inst.NN == 0xEE) 
            printf("Return from subroutine to address 0x%04X\n", *(chip8->stack_ptr - 1));
        else
            printf("Unimplemented opcode\n");
        break;

    case 0x01:
        printf("Jump to NNN (0x%03X)\n", chip8->inst.NNN);
        break;

    case 0x02:
        printf("Call subroutine at NNN (0x%03X)\n", chip8->inst.NNN);
        break;

    case 0x03:
        printf("Skip next instruction if V[%X] (0x%02X) == NN (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
        break;

    case 0x04:
        printf("Skip next instruction if V[%X] (0x%02X) != NN (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
        break;

    case 0x05:
        printf("Skip next instruction if V[%X] (0x%02X) = V[%X] (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y]);
        break;

    case 0x06:
        printf("Set V[%X] to NN (0x%02X)\n", chip8->inst.X, chip8->inst.NN);
        break;

    case 0x07:
        printf("Add NN (0x%02X) to V[%X]\n", chip8->inst.NN, chip8->inst.X);
        break;

    case 0x08:
        switch (chip8->inst.N)
        {
        case 0x0:
            printf("set V[%X] = V[%X] (0x%02X)\n", chip8->inst.X, chip8->inst.Y, chip8->V[chip8->inst.Y]);
            break;
        case 0x1:
            printf("set V[%X] (0x%02X) |= V[%X] (0x%02X) Result: 0x%02X\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y], chip8->V[chip8->inst.X] | chip8->V[chip8->inst.Y]);
            break;
        case 0x2:
            printf("set V[%X] (0x%02X) &= V[%X] (0x%02X) Result: 0x%02X\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y], chip8->V[chip8->inst.X] & chip8->V[chip8->inst.Y]);
            break;
        case 0x3:
            printf("set V[%X] (0x%02X) ^= V[%X] (0x%02X) Result: 0x%02X\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y], chip8->V[chip8->inst.X] ^ chip8->V[chip8->inst.Y]);
            break;
        case 0x4: // set V[F] to 1 if carry
            printf("set V[%X] (0x%02X) += V[%X] (0x%02X) Result: 0x%02X\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y], chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]);
            break;
        case 0x5: // set V[F] to 1 if no borrow
            printf("set V[%X] (0x%02X) -= V[%X] (0x%02X) Result: 0x%02X\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y], chip8->V[chip8->inst.X] - chip8->V[chip8->inst.Y]);
            break;
        case 0x6: // stores the LSB of V[X] in V[F]
            printf("set V[%X] (0x%02X) >>= 1 Result: 0x%02X\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->V[chip8->inst.X] >> 1);
            break;
        case 0x7: // set V[X] = V[Y] - V[X]
            printf("set V[%X] (0x%02X) = V[%X] (0x%02X) - V[%X] Result: 0x%02X\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y], chip8->inst.X, chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X]);
            break;
        case 0xE: // stores the MSB of V[X] in V[F]
            printf("set V[%X] (0x%02X) <<= 1 Result: 0x%02X\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->V[chip8->inst.X] << 1);
            break;
        default:
            break;
        }

        break;

    case 0x09:
        printf("Skip next instruction if V[%X] (0x%02X) != V[%X] (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y, chip8->V[chip8->inst.Y]);
        break;

    case 0x0A:
        printf("Set I to NNN (0x%03X)\n", chip8->inst.NNN);
        break;

    case 0x0B:
        printf("Set PC to V[0] (0x%02X) + NNN (0x%03X) Result: %04X\n", chip8->V[0x0], chip8->inst.NNN, chip8->V[0x0] + chip8->inst.NNN);
        break;

    case 0xC:
        printf("Set V[%X] to rand(0-255) & NN (%02X) Result: %02X\n", chip8->inst.X, chip8->inst.NN, (rand() % 256) & chip8->inst.NN);
        break;

    case 0x0D:
        printf("Draw %X height sprite at at coords (V[%X], V[%X])\n", chip8->inst.N, chip8->inst.X, chip8->inst.Y);
        break;

    case 0x0E:
        if(chip8->inst.NN == 0x9E){
            printf("Skip next instruction if key stored in V[%X] is pressed; Keypad Value: %d\n", chip8->inst.X, chip8->keypad[chip8->V[chip8->inst.X]]);
        }
        else if(chip8->inst.NN == 0xA1){
            printf("Skip next instruction if key stored in V[%X] is NOT pressed; Keypad Value: %d\n", chip8->inst.X, chip8->keypad[chip8->V[chip8->inst.X]]);
        }
        break;

    case 0x0F:
        switch(chip8->inst.NN)
        {
            case 0x0A:
                printf("Set V[%X] to the key pressed; Await a key press\n", chip8->inst.X);
                break;
            case 0x1E:
                printf("Set I (0x%04X) += V[%X] (0x%02X) Result: 0x%04X\n", chip8->I, chip8->inst.X, chip8->V[chip8->inst.X], chip8->I + chip8->V[chip8->inst.X]);
                break;
            case 0x07:
                printf("Set V[%X] = delay timer (0x%02X)\n", chip8->inst.X, chip8->delay_timer);
                break;
            case 0x15:
                printf("set delay timer = V[%X] (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X]);
                break;
            case 0x18:
                printf("set sound timer = V[%X] (0x%02X)\n", chip8->inst.X, chip8->V[chip8->inst.X]);
                break;
            case 0x29: 
                printf("Set I = location of sprite of character stored in V[%X] (0x%02X); Result(V[%X] * 5): 0x%02X\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.X, chip8->V[chip8->inst.X] * 5);
                break;
            case 0x33:
                printf("Store BCD representation of V[%X] (0x%02X) in memory from index I (0x%04X) onwards\n", chip8->inst.X, chip8->V[chip8->inst.X], chip8->I);
                break;
            case 0x55:
                printf("Store values of registers V[0] - V[%X] in memory from index I (0x%04X) onwards\n", chip8->inst.X, chip8->I);
                break;
            case 0x65:
                printf("Load values of registers V[0] - V[%X] in memory from index I (0x%04X) onwards\n", chip8->inst.X, chip8->I);
                break;
            default:
                break;
        }
        break;

    default:
        printf("Unimplemented opcode\n");
        break; // unimplemented or invalid opcode
    }
}
#endif

// emulates instructions for chip8
void emulate_instructions(chip8_t *chip8, const config_t config)
{
    // get the next 16-bit opcode from ram
    chip8->inst.opcode = (chip8->ram[chip8->PC] << 8) | (chip8->ram[chip8->PC + 1]);
    chip8->PC += 2;

    // fill out registers and constants for the opcode
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0x00FF;
    chip8->inst.N = chip8->inst.opcode & 0x000F;
    chip8->inst.X = (chip8->inst.opcode & 0x0F00) >> 8;
    chip8->inst.Y = (chip8->inst.opcode & 0x00F0) >> 4;

#ifdef DEBUG
    print_debug_info(chip8);
#endif

    // emulate opcode
    switch ((chip8->inst.opcode >> 12) & 0xF)
    {
    case 0x0:
        if (chip8->inst.NN == 0xE0){ // clear screen
            memset(&chip8->display[0], false, sizeof chip8->display);
            chip8->draw = true;
        }
        else if (chip8->inst.NN == 0xEE) // return from subroutine
            chip8->PC = *--chip8->stack_ptr;
        break;

    case 0x1: // jump to address NNN
        chip8->PC = chip8->inst.NNN;
        break;

    case 0x2: // call subroutine at NNN
        *chip8->stack_ptr++ = chip8->PC;
        chip8->PC = chip8->inst.NNN;
        break;

    case 0x3: // skip the next instruction if V[X] = NN
        if (chip8->V[chip8->inst.X] == chip8->inst.NN)
            chip8->PC += 2;
        break;

    case 0x4: // skip the next instruction if V[X] = NN
        if (chip8->V[chip8->inst.X] != chip8->inst.NN)
            chip8->PC += 2;
        break;

    case 0x5: // skip next instruction if V[X] = V[Y] (5XY0)
        if ((chip8->inst.N == 0) && (chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]))
            chip8->PC += 2;
        break;

    case 0x6: // set V[X] to NN
        chip8->V[chip8->inst.X] = chip8->inst.NN;
        break;

    case 0x7: // add NN to V[X] (carry flag is not changed)
        chip8->V[chip8->inst.X] += chip8->inst.NN;
        break;

    case 0x8:
        switch (chip8->inst.N)
        {
        case 0x0: // set V[X] = V[Y]
            chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
            break;
        case 0x1: // set V[X] |= V[Y]
            chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
            break;
        case 0x2: // set V[X] &= V[Y]
            chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
            break;
        case 0x3: // set V[X] ^= V[Y]
            chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
            break;
        case 0x4: // set V[X] += V[Y], set V[F] to 1 if carry
            chip8->V[0xF] =  ((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255);
            chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
            break;
        case 0x5: // set V[X] -= V[Y], set V[F] to 1 if no borrow
            chip8->V[0xF] = (chip8->V[chip8->inst.X] >= chip8->V[chip8->inst.Y]);
            chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
            break;
        case 0x6: // stores the LSB of V[X] in V[F] and sets V[X] >>= 1
            chip8->V[0xF] = chip8->V[chip8->inst.X] & 0x01;
            chip8->V[chip8->inst.X] >>= 1;
            break;
        case 0x7: // set V[X] = V[Y] - V[X]
            chip8->V[0xF] = chip8->V[chip8->inst.Y] >= chip8->V[chip8->inst.X];

            chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
            break;
        case 0xE: // stores the MSB of V[X] in V[F] and sets V[X] <<= 1
            chip8->V[0xF] = (chip8->V[chip8->inst.X] & 0x80) >> 7;
            chip8->V[chip8->inst.X] <<= 1;
            break;
        default:
            break;
        }
        break;

    case 0x9: // skip next instruction if V[X] != V[Y] (9XY0)
        if ((chip8->inst.N == 0) && (chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y]))
            chip8->PC += 2;
        break;

    case 0xA: // set index reg to NNN
        chip8->I = chip8->inst.NNN;
        break;

    case 0xB: // set PC to V[0] + NNN
        chip8->PC = chip8->V[0x0] + chip8->inst.NNN;
        break;

    case 0xC: // set V[X] = rand(0-255) & NN
        chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
        break;

    case 0xD: // draw N height sprites (stored at location I) at coords (V[X], V[Y])
    {
        uint8_t x_coord = chip8->V[chip8->inst.X] % config.window_width;
        uint8_t y_coord = chip8->V[chip8->inst.Y] % config.window_height;
        const uint8_t org_X = x_coord;

        chip8->V[0xF] = 0; // set carry flags to 0

        for (uint8_t i = 0; i < chip8->inst.N; i++) // traverse all rows of the sprite
        {
            // get next byte/data of the sprite
            const uint8_t sprite_data = chip8->ram[chip8->I + i];
            x_coord = org_X;

            // traverse the 8-bit sprite data left to right
            for (uint8_t j = 0; j < 8; j++)
            {
                bool *pixel = &chip8->display[y_coord * config.window_width + x_coord];
                //const bool sprite_bit = sprite_data & (1 << j);
                const bool sprite_bit = (sprite_data & (0x80 >> j)) != 0; 

                if (sprite_bit && *pixel)
                    chip8->V[0xF] = 1;

                *pixel ^= sprite_bit;

                if (++x_coord >= config.window_width)
                    break;
            }

            if (++y_coord >= config.window_height)
                break;
        }
        chip8->draw = true;
        break;
    }

    case 0xE:
        if(chip8->inst.NN == 0x9E){ // skip next instruction if key stored in V[X] is pressed
            if(chip8->keypad[chip8->V[chip8->inst.X]] == true)
                chip8->PC += 2;
        }
        else if(chip8->inst.NN == 0xA1){ // skip next instruction if key stored in V[X] is NOT pressed
            if(chip8->keypad[chip8->V[chip8->inst.X]] == false)
                chip8->PC += 2;
        }
        break;

    case 0xF:
        switch(chip8->inst.NN)
        {
            case 0x0A: // V[X] = get_key(); Await a key press
            {    
                bool key_pressed = false;
                for(uint8_t i = 0; i < sizeof chip8->keypad; i++)
                {
                    if(chip8->keypad[i]){
                        chip8->V[chip8->inst.X] = i;
                        key_pressed = true;
                        break;
                    }
                }
                if(!key_pressed)
                    chip8->PC -= 2; // stay here until a key is pressed
                break;
            }
            case 0x1E: // set I += V[X]; V[F] is not affected
                chip8->I += chip8->V[chip8->inst.X];
                break;
            case 0x07: // set V[X] = delay timer
                chip8->V[chip8->inst.X] = chip8->delay_timer;
                break;
            case 0x15: // set delay timer = V[X]
                chip8->delay_timer = chip8->V[chip8->inst.X];
                break;
            case 0x18: // set sound timer = V[X]
                chip8->sound_timer = chip8->V[chip8->inst.X];
                break;
            case 0x29: // set I to the location of sprite of character stored in V[X]
                chip8->I = chip8->V[chip8->inst.X] * 5;
                break;
            case 0x33: // store BCD rep pf V[X] from index I onwards(I -> hundred's, I+1 -> ten's, I+2 -> one's)
            {   
                uint8_t bcd = chip8->V[chip8->inst.X];
                for(int i = 2; i >= 0; i--){
                    chip8->ram[chip8->I+i] = bcd % 10;
                    bcd /= 10;
                }
                break;
            }
            case 0x55: // store values from V[0] to V[X] in memory from index I onwards
            {
                for(uint8_t i = 0; i <= chip8->inst.X; i++)
                {
                    chip8->ram[chip8->I + i] = chip8->V[i];
                }
                break;
            }
            case 0x65: // store values from V[0] to V[X] in memory from index I onwards
            {
                for(uint8_t i = 0; i <= chip8->inst.X; i++)
                {
                    chip8->V[i] = chip8->ram[chip8->I + i];
                }
                break;
            }
            default:
                break;
        }
        break;

    default:
        break; // unimplemented or invalid opcode
    }
}

// update delay and sound timers
void update_timers(const sdl_t sdl, chip8_t *chip8)
{
    if(chip8->delay_timer > 0)
        chip8->delay_timer--;

    if(chip8->sound_timer > 0)
    {
        chip8->sound_timer--;
        SDL_PauseAudioDevice(sdl.dev, 0); // play sound
    }
    else
    {
        SDL_PauseAudioDevice(sdl.dev, 1); // stop playing sound
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <rom_name>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // check config setup
    config_t config = {0};
    if (set_config(&config, argc, argv) == false)
        exit(EXIT_FAILURE);

    // check SDL inititalisation
    sdl_t sdl = {0};
    if (!init_sdl(&sdl, &config))
        exit(EXIT_FAILURE);

    // check chip8 initialisation
    chip8_t chip8;
    const char *rom_name = argv[1];
    if (!init_chip8(&chip8, rom_name))
        exit(EXIT_FAILURE);

    // clear the window to bg-color
    clear_screen(config, sdl);

    // seed random number generator
    srand(time(NULL));

    // main emulator loop
    while (chip8.state != QUIT)
    {
        // handle user inputs
        handle_inputs(&chip8);

        if (chip8.state == PAUSED)
            continue;

        // get time before running instructions
        const uint64_t start_time = SDL_GetPerformanceCounter();

        // emulate chip8 instructions for this frame (60hz)
        for(uint32_t i = 0; i < config.insts_per_second / 60; i++)
            emulate_instructions(&chip8, config);

        // get time after running instructions
        const uint64_t end_time = SDL_GetPerformanceCounter();

        // delay to maintain 60 fps
        const double time_elapsed = (double)((end_time - start_time) * 1000) / SDL_GetPerformanceFrequency();
        const double actual_delay = time_elapsed < 16.67f ? 16.67f - time_elapsed : 0;
        SDL_Delay(actual_delay);

        // update window with changes
        update_screen(sdl, config, chip8);
        if(chip8.draw){
            update_screen(sdl, config, chip8);
            chip8.draw = false;
        }

        // upadate sound timer
        update_timers(sdl, &chip8);
    }

    final_cleanup(sdl);

    exit(EXIT_SUCCESS);
}
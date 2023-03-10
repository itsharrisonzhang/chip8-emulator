#include "emulator.hh"
#include <SDL2/SDL.h>

SDL_Event s;

int fetch (Emulator &e, uint16_t i) {
    // combine two 8-bit instructions into a 16-bit instruction
    uint16_t instr = e.membuf[i] << 8 | e.membuf[i+1];
    e.PC += 2;
    // read 16-bit instruction
    int r = exec(e, instr);
    if (r != 0) {
        return -1;
    }
    return 0;
}


int exec (Emulator &e, uint16_t instr) {
    uint16_t fn  = (0xF000 & instr) >> 12;   // first nibble to obtain op
    uint16_t sn  = (0xF00 & instr) >> 8;     // second nibble to find first reg
    uint16_t tn  = (0xF0 & instr) >> 4;      // third nibble to find second reg
    uint16_t pn  = (0xF & instr);            // fourth nibble

    uint16_t nn  = 0x00FF & instr;           // 8-bit number
    uint16_t nnn = 0x0FFF & instr;           // 12-bit address

    switch (fn) {
        case 0x0: {
            // 00E0: clear display
            if (instr == 0x00E0) {
                for (auto i = 0; i < DISPLAY_HEIGHT; ++i) {
                    for (auto j = 0; j < DISPLAY_WIDTH; ++j) {
                        e.display[i][j] = 0;
                    }
                }
                SDL_Rect crect = {0, 0, DISPLAY_WIDTH*TEXEL_SCALE, DISPLAY_HEIGHT*TEXEL_SCALE};
                SDL_SetRenderDrawColor((SDL_Renderer*)e.renderer, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
                SDL_RenderFillRect((SDL_Renderer*)e.renderer, &crect);
                SDL_RenderPresent((SDL_Renderer*)e.renderer);
            }
            // 00EE: return from subroutine
            else if (instr == 0x00EE) {
                for (auto i = 0; i != STACKSIZE; ++i) {
                    if (e.stack[i] != 0xFFFF) {
                        // set PC equal to last address in stack
                        e.PC = e.stack[i];
                        // pop last address from stack
                        e.stack[i] = 0xFFFF;
                        break;
                    }
                }
            }
            break;
        }
        case 0x1: {
            // 1NNN: set PC to NNN
            e.PC = nnn;
            break;
        }
        case 0x2: {
            // 2NNN: call subroutine
            int i = findstackspace(e);
            assert(i >= 0);
            // push PC to stack
            e.stack[i] = e.PC;
            // set PC to NNN
            e.PC = nnn;
            break;
        }
        case 0x3: {
            // 3XNN: skip one instruction if VX == NN
            if (e.regs[sn] == nn) {
                e.PC += 2;
            }
            break;
        }
        case 0x4: {
            // 4XNN: skip one instruction if VX != NN
            if (e.regs[sn] != nn) {
                e.PC += 2;
            }
            break;
        }
        case 0x5: {
            // 5XY0: skip one instruction if VX == VY
            if (pn == 0x0) {
                if (e.regs[sn] == e.regs[tn]) {
                    e.PC += 2;
                }
            }
            break;
        }
        case 0x6: {
            // 6XNN: set VX to NN
            e.regs[sn] = nn;
            break;
        }
        case 0x7: {
            // 7XNN: add NN to VX
            // overflow does not change VF
            e.regs[sn] += nn;
            break;
        }
        case 0x8: {
            // 8NNN: parse externally
            int r = parse_8NNN(e, instr);
            assert(r == 0);
            break;
        }
        case 0x9: {
            // 9XY0: skip one instruction if VX != VY
            if (pn == 0x0) {
                if (e.regs[sn] != e.regs[tn]) {
                    e.PC += 2;
                }
            }
            break;
        }
        case 0xA: {
            // ANNN: set I to NNN
            e.I = nnn;
            break;
        }
        case 0xB: {
            // BNNN: jump to address (NNN + VX)
            e.PC = nnn + e.regs[sn];
            break;
        }
        case 0xC: {
            // CXNN: generates random number rn in [0, NN], 
            // then set VX to rn & NN
            std::default_random_engine gen;
            std::uniform_int_distribution<int> dist(0, nn);
            uint16_t rand = (uint16_t)dist(gen);
            e.regs[sn] = rand & nn;
            break;
        }
        case 0xD: {
            // DXYN: Draws an N-pixel tall sprite from where I is
            // currently pointing on the screen, as well as from
            // VX and VY on the screen

            // find coordinates
            uint8_t x = e.regs[sn] % DISPLAY_WIDTH;
            uint8_t y = e.regs[tn] % DISPLAY_HEIGHT;
            assert(x < DISPLAY_WIDTH);
            assert(y < DISPLAY_HEIGHT);
            e.regs[0xF] = 0;

            SDL_Rect prect = {0, 0, TEXEL_SCALE, TEXEL_SCALE};
            for (auto i = 0; i != pn; ++i) {
                for (auto j = 0; j != 8; ++j) {
                    prect.y = (y*TEXEL_SCALE + i*TEXEL_SCALE) % (DISPLAY_HEIGHT*TEXEL_SCALE);
                    prect.x = (x*TEXEL_SCALE + j*TEXEL_SCALE) % (DISPLAY_WIDTH*TEXEL_SCALE);

                    // handle display pixel updates
                    int pixel = (e.membuf[e.I+i] >> (7-j)) & 1;
                    assert(pixel == 0 || pixel == 1);

                    if (pixel == 1) {
                        if (e.display[y+i][x+j] == 1) {
                            e.regs[0xF] = 1;
                        }
                        e.display[y+i][x+j] = e.display[y+i][x+j] ^ pixel;
                        if (e.display[y+i][x+j] == 0) {
                            SDL_SetRenderDrawColor((SDL_Renderer*)e.renderer, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
                        }
                        else if (e.display[y+i][x+j] == 1) {
                            SDL_SetRenderDrawColor((SDL_Renderer*)e.renderer, 0xFF, 0xFF, 0xFF, SDL_ALPHA_OPAQUE);
                        }
                        SDL_RenderFillRect((SDL_Renderer*)e.renderer, &prect);
                    }
                }
                SDL_RenderPresent((SDL_Renderer*)e.renderer);
            }
            break;
        }
        case 0xE: {
            // EX9E: skip one instruction if key corresponding
            //       to the value in VX is pressed
            if (tn == 0x9 && pn == 0xE) {
                if (e.keystates[e.regs[sn]] == 1) {
                    e.PC += 2;
                }
            }
            else if (tn == 0xA && pn == 0x1) {
                if (e.keystates[e.regs[sn]] == 0) {
                    e.PC += 2;
                }
            }
            break;
        }
        case 0xF: {
            int s = parse_FNNN(e, instr);
            assert(s == 0);
            break;
        }
        default: {
            // invalid instruction
            return -1;
        }
    }
    return 0;
}


int parse_8NNN (Emulator &e, uint16_t instr) {
    uint16_t fn  = (0xF000 & instr) >> 12;   // first nibble to obtain op
    uint16_t sn  = (0xF00 & instr) >> 8;     // second nibble to find first reg
    uint16_t tn  = (0xF0 & instr) >> 4;      // third nibble to find second reg
    uint16_t pn  = (0xF & instr);            // fourth nibble
    assert(fn == 0x8);

    switch (pn) {
        case 0x0: {
            // 8XY0: set VX to VY   
            e.regs[sn] = e.regs[tn];
            break;
        }
        case 0x1: {
            // 8XY1: set VX to VX | VY
            e.regs[sn] = e.regs[sn] | e.regs[tn];
            break;
        }
        case 0x2: {
            // 8XY2: set VX to VX & VY
            e.regs[sn] = e.regs[sn] & e.regs[tn];
            break;
        }
        case 0x3: {
            // 8XY3: set VX to VX ^ VY
            e.regs[sn] = e.regs[sn] ^ e.regs[tn];
            break;
        }
        case 0x4: {
            // 8XY4: set VX to VX + VY
            e.regs[0xF] = 0;
            // set VF = 1 if 'carry' (overflow)
            if (e.regs[tn] > UINT8_MAX - e.regs[sn]
             || e.regs[sn] > UINT8_MAX - e.regs[tn]) {
                e.regs[0xF] = 1;
            }
            e.regs[sn] += e.regs[tn];
            break;
        }
        case 0x5: {
            // 8XY5: set VX to VX - VY
            // set VF = 0 if `borrow` and 1 otherwise
            e.regs[0xF] = 1;
            if (e.regs[sn] < e.regs[tn]) {
                e.regs[0xF] = 0;
            }
            e.regs[sn] = e.regs[sn] - e.regs[tn];
            break;
        }
        case 0x6: {
            // 8XY6: Right bitshift VX and set VF 
            //       equal to the value bitshifted out
            //e.regs[sn] = e.regs[tn];
            e.regs[0xF] = e.regs[sn] & 0b1;
            e.regs[sn] = e.regs[sn] >> 1;
            break;
        }
        case 0x7: {
            // 8XY7: set VX to VY - VX
            // set VF = 0 if `borrow` and 1 otherwise
            e.regs[0xF] = 1;
            if (e.regs[tn] < e.regs[sn]) {
                e.regs[0xF] = 0;
            }
            e.regs[sn] = e.regs[tn] - e.regs[sn];
            break;
        }
        case 0xE: {
            // 8XYE: Left bitshift VX and set VF
            //       equal to the value bitshifted out
            e.regs[0xF] = (e.regs[sn] & 0b1000'0000) >> 7;
            e.regs[sn] = e.regs[sn] << 1;
            break;
        }
        default: {
            return -1;
        }
    }
    return 0;
}


int parse_FNNN (Emulator &e, uint16_t instr) {
    uint16_t fn  = (0xF000 & instr) >> 12;   // first nibble to obtain op
    uint16_t sn  = (0xF00 & instr) >> 8;     // second nibble to find first reg
    uint16_t tn  = (0xF0 & instr) >> 4;      // third nibble to find second reg
    uint16_t pn  = (0xF & instr);            // fourth nibble
    assert(fn == 0xF);

    // FX07: set VX to current val of delay timer
    if (tn == 0x0 && pn == 0x7) {
        e.regs[sn] = e.delay_timer;
    }
    // FX15: set delay timer to VX
    else if (tn == 0x1 && pn == 0x5) {
        e.delay_timer = e.regs[sn];
    }
    // FX18: set sound timer to VX
    else if (tn == 0x1 && pn == 0x8) {
        e.sound_timer = e.regs[sn];
    }
    // FX1E: set I to I+VX and set carry flag to 1 if I > 0xFFF
    else if (tn == 0x1 && pn == 0xE) {
        e.I += e.regs[sn];
        if (e.I > 0xFFF) {
            e.regs[0xF] = 1;
        }
    }
    // FX0A: blocks until key is pressed, then when
    //       key is pressed, store its hex in VX
    else if (tn == 0x0 && pn == 0xA) {
        // decrement initially
        assert(e.PC >= 2);
        e.PC -= 2;
        unsigned long loops = 0;
        while (1) {
            SDL_PollEvent(&s);
            if (s.type == SDL_QUIT) {
                SDL_DestroyRenderer((SDL_Renderer*)e.renderer);
                SDL_DestroyWindow((SDL_Window*)e.window);
                SDL_Quit();
            }
            if (s.type == SDL_KEYDOWN) {
                int r = check_keyboard();
                if (r != -1) {
                    e.regs[sn] = r;
                    // increment so no net change if key pressed
                    e.PC += 2;
                    break;
                }
            }
            // update timers at 60 Hz
            if (loops % 8 == 0) {
                updatedelaytimer(e);
                updatesoundtimer(e);
            }
            ++loops;
        }
    }
    // FX29:
    else if (tn == 0x2 && pn == 0x9) {
        uint8_t vx_n = e.regs[sn] & 0xF;
        e.I = 0x050 + 5*vx_n;
    }
    // FX33: store digits of VX (decimal form) at I, I+1, I+2
    else if (tn == 0x3 && pn == 0x3) {
        e.membuf[e.I]   = (e.regs[sn] / 100) % 10;
        e.membuf[e.I+1] = (e.regs[sn] / 10) % 10;
        e.membuf[e.I+2] = (e.regs[sn]) % 10;
    }
    // FX55: store VX at index I+X in memory
    else if (tn == 0x5 && pn == 0x5) {
        for (auto i = 0; i <= sn; ++i) {
            e.membuf[e.I+i] = e.regs[i];
        }
    }
    // FX65: store the value at I+X in VX
    else if (tn == 0x6 && pn == 0x5) {
        for (auto i = 0; i <= sn; ++i) {
            e.regs[i] = e.membuf[e.I+i];
        }
    }
    else {
        return -1;
    }
    return 0;
}


int check_keyboard () {
    switch (s.key.keysym.sym) {
        case SDLK_1: {
            return 0x1;
        }
        case SDLK_2: {
            return 0x2;
        }
        case SDLK_3: {
            return 0x3;
        }
        case SDLK_4: {
            return 0xC;
        }
        case SDLK_q: {
            return 0x4;
        }
        case SDLK_w: {
            return 0x5;
        }
        case SDLK_e: {
            return 0x6;
        }
        case SDLK_r: {
            return 0xD;
        }
        case SDLK_a: {
            return 0x7;
        }
        case SDLK_s: {
            return 0x8;
        }
        case SDLK_d: {
            return 0x9;
        }
        case SDLK_f: {
            return 0xE;
        }
        case SDLK_z: {
            return 0xA;
        }
        case SDLK_x: {
            return 0x0;
        }
        case SDLK_c: {
            return 0xB;
        }
        case SDLK_v: {
            return 0xF;
        }
        default: {
            return -1;
        }
    }
    return -1;
}

int main () {
    // create emulator and display
    Emulator e;
    SDL_Init(SDL_INIT_VIDEO);
    e.window = SDL_CreateWindow("", 
                                SDL_WINDOWPOS_CENTERED, 
                                SDL_WINDOWPOS_CENTERED,
                                DISPLAY_WIDTH*TEXEL_SCALE, 
                                DISPLAY_HEIGHT*TEXEL_SCALE, 0);
    assert(e.window);
    e.renderer = SDL_CreateRenderer((SDL_Window*)e.window, -1, SDL_RENDERER_SOFTWARE);
    assert(e.renderer);

    // load stack with 0xFFFF as default
    for (auto i = 0; i != STACKSIZE; ++i) {
        e.stack[i] = 0xFFFF;
    }

    // load font data into 0x050-0x09F in membuf
    for (auto j = 0; j != 0x09F-0x050+1; ++j) {
        e.membuf[0x050+j] = e.fontdata[j];
    }

    // load game data into 0x200-0xFFF in membuf
    FILE* fptr;
    uint8_t tbuf[MEMSIZE-ROM_START_ADDR] = {0};
    fptr = fopen(GAME_PATH, "r");
    if (!fptr) {
        return -1;
    }
    fread(tbuf, MEMSIZE-ROM_START_ADDR, 1, fptr);
    for (auto k = 0; k != MEMSIZE-ROM_START_ADDR; ++k) {
        e.membuf[ROM_START_ADDR+k] = tbuf[k];
    }

    // run game
    while (1) {
        unsigned long loops = 0;
        while (e.PC < MEMSIZE-1) {
            // fetch and execute instruction at membuf[PC]
            int r = fetch(e, e.PC);
            assert(r == 0);
            if (SDL_PollEvent(&s) != 0) {
                switch (s.type) {
                    case SDL_QUIT: {
                        goto quit;
                    }
                    case SDL_KEYDOWN: {
                        int d = check_keyboard();
                        e.keystates[d] = 1;
                        break;
                    }
                    case SDL_KEYUP: {
                        int u = check_keyboard();
                        e.keystates[u] = 0;
                        break;
                    }
                }
            }
            // update timers at 60 Hz
            if (loops % 8 == 0) {
                updatedelaytimer(e);
                updatesoundtimer(e);
            }
            ++loops;
        }
        if (SDL_PollEvent(&s) != 0) {
            switch (s.type) {
                case SDL_QUIT: {
                    goto quit;
                }
                case SDL_KEYDOWN: {
                    int d = check_keyboard();
                    e.keystates[d] = 1;
                    break;
                }
                case SDL_KEYUP: {
                    int u = check_keyboard();
                    e.keystates[u] = 0;
                    break;
                }
            }
        }
    }
    quit: {
        SDL_DestroyRenderer((SDL_Renderer*)e.renderer);
        SDL_DestroyWindow((SDL_Window*)e.window);
        SDL_Quit();
    }

    return 0;
}
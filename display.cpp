#include "display.hpp"
#include <stdexcept>

namespace {
constexpr int WINDOW_WIDTH = 1024;
constexpr int WINDOW_HEIGHT = 768;
constexpr SDL_Color TEXT_COLOR{255, 255, 255, 255};
constexpr SDL_Color BG_COLOR{0, 0, 0, 255};
constexpr int LINE_HEIGHT = 20;
constexpr int LEFT_MARGIN = 12;
constexpr int TOP_MARGIN = 12;
}

Display::Display(const std::string& title) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    if (TTF_Init() != 0) {
        SDL_Quit();
        throw std::runtime_error(std::string("TTF_Init failed: ") + TTF_GetError());
    }
    m_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!m_window) {
        TTF_Quit();
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }
    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) {
        SDL_DestroyWindow(m_window);
        TTF_Quit();
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    }
    m_font = TTF_OpenFont("/System/Library/Fonts/Menlo.ttc", 16);
    if (!m_font) m_font = TTF_OpenFont("/System/Library/Fonts/Supplemental/Courier New.ttf", 16);
    if (!m_font) {
        SDL_DestroyRenderer(m_renderer);
        SDL_DestroyWindow(m_window);
        TTF_Quit();
        SDL_Quit();
        throw std::runtime_error(std::string("TTF_OpenFont failed: ") + TTF_GetError());
    }
}

Display::~Display() {
    if (m_font) TTF_CloseFont(m_font);
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
    TTF_Quit();
    SDL_Quit();
}

bool Display::processEvents(DebugAction& action) {
    action = DebugAction::None;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) return false;
        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
            switch (event.key.keysym.sym) {
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    action = DebugAction::TogglePause;
                    break;
                case SDLK_SPACE:
                    action = DebugAction::StepOne;
                    break;
            }
        }
    }
    return true;
}

void Display::clear() {
    SDL_SetRenderDrawColor(m_renderer, BG_COLOR.r, BG_COLOR.g, BG_COLOR.b, BG_COLOR.a);
    SDL_RenderClear(m_renderer);
}

void Display::present(const std::vector<std::string>& lines) {
    int y = TOP_MARGIN;
    for (const auto& line : lines) {
        SDL_Surface* surface = TTF_RenderUTF8_Blended(m_font, line.c_str(), TEXT_COLOR);
        if (!surface) continue;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
        if (!texture) {
            SDL_FreeSurface(surface);
            continue;
        }
        SDL_Rect dst{LEFT_MARGIN, y, surface->w, surface->h};
        SDL_RenderCopy(m_renderer, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
        SDL_FreeSurface(surface);
        y += LINE_HEIGHT;
        if (y > WINDOW_HEIGHT - LINE_HEIGHT) break;
    }
    SDL_RenderPresent(m_renderer);
}

void Display::delay(unsigned ms) {
    SDL_Delay(ms);
}

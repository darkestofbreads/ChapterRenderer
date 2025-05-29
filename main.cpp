#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(__linux__)
#define VK_USE_PLATFORM_XLIB_KHR
#elif defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#define SDL_MAIN_HANDLED

#include "InputHandler.h"
#include "Renderer.h"

#include <SDL3/SDL.h>

#include <iostream>
#include <atomic>
#include <thread>

void doRendering(std::atomic<bool>* stillRunning, SDL_Window* windowOut, std::atomic<bool>* ready) {
    // Window creation.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cout << "Could not initialize SDL. Please ensure your system detects any monitor before continuing." << std::endl;
    }
    SDL_Window* window = SDL_CreateWindow("Chapter One", 1280, 720, SDL_WINDOW_VULKAN);
    if (window == NULL) {
        std::cout << "Could not create SDL window." << std::endl;
    }
    SDL_SetWindowBordered(window, false);
    SDL_SetWindowFullscreen(window, true);
    SDL_SetWindowMouseGrab(window, true);
    SDL_SetWindowRelativeMouseMode(window, true);
    Renderer renderer(window, ready);
    windowOut = window;

    bool doFullscreenNextChange = false;
    bool isExclusive = false;
    bool grabMouse   = true;
    float sensitivity = 0.1f;
    float forward  = 0;
    float sideward = 0;
    float yaw = 0;

    InputHandler input;
    auto& isPressed = input.IsPressed;
    while (*stillRunning) {
        // Poll events.
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            auto keyCode = event.key.key;
            float yOffset;
            float xOffset;
            switch (event.type) {

            case SDL_EVENT_KEY_DOWN:
                isPressed[keyCode] = true;
                break;
            case SDL_EVENT_KEY_UP:
                isPressed[keyCode] = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (grabMouse) {
                    auto rely = event.motion.yrel;
                    auto rel = event.motion.xrel;
                    std::cout << rel << " " << rely << "\n";
                    xOffset = rel * sensitivity;
                    yOffset = rely * sensitivity;
                    renderer.pitch += yOffset;
                    yaw = yaw + xOffset;
                    if (renderer.pitch < -89.0f)
                        renderer.pitch = -89.0f;
                    if (renderer.pitch >  89.0f)
                        renderer.pitch =  89.0f;
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                grabMouse = false;
                SDL_SetWindowMouseGrab(window, grabMouse);
                SDL_SetWindowRelativeMouseMode(window, grabMouse);
                break;
            case SDL_EVENT_QUIT:
                *stillRunning = false;
                break;

            default:
                // Do nothing.
                break;
            }
        }
        *stillRunning = !isPressed[SDLK_ESCAPE];
        // Movement.
        float velocity = isPressed[SDLK_LSHIFT] ? 0.5f : 0.1f;
        forward  = isPressed[SDLK_W] ? velocity : isPressed[SDLK_S] ? -velocity : 0;
        sideward = isPressed[SDLK_A] ? velocity : isPressed[SDLK_D] ? -velocity : 0;
        renderer.yaw = yaw;
        renderer.Move(forward, sideward);

        if (isPressed[SDLK_F]) {
            grabMouse = !grabMouse;
            SDL_SetWindowMouseGrab(window, grabMouse);
            SDL_SetWindowRelativeMouseMode(window, grabMouse);
        }
        // Send position and rotation to logic handler.

        renderer.Draw();
    }
}

int main()
{
    std::atomic<bool> stillRunning = true;
    std::atomic<bool> ready        = false;

    // Start rendering
    SDL_Window* window;
    std::thread renderThread(doRendering, &stillRunning, std::ref(window), &ready);
    while (!ready) {}
    std::cout << "Ready!\n";

    while (stillRunning) {}
    renderThread.join();
	return 0;
}
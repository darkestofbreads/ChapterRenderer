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

void doRendering(std::atomic<bool>* stillRunning, SDL_Window* window, std::atomic<bool>* ready
    , std::atomic<float>* forward, std::atomic<float>* sidewards, std::atomic<float>* yaw, std::atomic<float>* pitch) {
    Renderer renderer(window, ready);
    while (*stillRunning) {
        renderer.pitch = *pitch;
        renderer.yaw = *yaw;
        renderer.Move(*forward, *sidewards);

        // Send position and rotation to logic handler.

        renderer.Draw();
    }
}

int main()
{
    // Window creation.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cout << "Could not initialize SDL. Please ensure your system detects any monitor before continuing." << std::endl;
    }
    SDL_Window* window = SDL_CreateWindow("Chapter One", 1280, 720, SDL_WINDOW_VULKAN);
    if (window == NULL) {
        std::cout << "Could not create SDL window." << std::endl;
    }
    SDL_SetWindowBordered(window, false);

    bool doFullscreenNextChange = false;
    bool isExclusive = false;
    SDL_SetWindowFullscreen(window, true);
    SDL_SetWindowMouseGrab(window, true);
    SDL_SetWindowRelativeMouseMode(window, true);

    std::atomic<bool> stillRunning = true;
    std::atomic<bool> ready = false;

    std::atomic<float> forward = 0, sidewards = 0;
    std::atomic<float> yaw = 0, pitch = 0;
    float localPitch = 0;
    float sensitivity = 0.1f;
    bool hasMoved = false;

    // Start rendering
    std::thread renderThread(doRendering, &stillRunning, std::ref(window), &ready, &forward, &sidewards, &yaw, &pitch);
    while (!ready) {}
    ready = false;
    std::cout << "Ready!\n";

    InputHandler input;
    auto& isPressed = input.IsPressed;
    while (stillRunning) {
        // Poll for user input.
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
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
                yOffset = event.motion.yrel * sensitivity;
                xOffset = event.motion.xrel * sensitivity;
                yaw = yaw + xOffset;
                localPitch += yOffset;
                if (localPitch < -89.0f)
                    localPitch = -89.0f;
                if (localPitch > 89.0f)
                    localPitch = 89.0f;
                pitch = localPitch;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                // Open pause menu.
                // A pause menu should be a requirement for every game.
                // The reason is to simply not have the mouse stuck in the middle for some reason
                // despite capturing it.
                break;
            case SDL_EVENT_QUIT:
                stillRunning = false;
                break;

            default:
                // Do nothing.
                break;
            }
        }
        // Perform input action.

        stillRunning = !isPressed[SDLK_ESCAPE];

        // Movement.
        float velocity = isPressed[SDLK_LSHIFT] ? 0.1f : 0.01f;
        forward = isPressed[SDLK_W] ? velocity : isPressed[SDLK_S] ? -velocity : 0;
        sidewards = isPressed[SDLK_A] ? velocity : isPressed[SDLK_D] ? -velocity : 0;

        // Fullscreen management.
        if (isPressed[SDLK_F11] && input.IsDelayOver()) {
            if (doFullscreenNextChange) {
                if (isExclusive)
                    SDL_SetWindowFullscreen(window, true);
                else
                    SDL_SetWindowFullscreen(window, true);
                doFullscreenNextChange = false;
            }
            else {
                SDL_SetWindowFullscreen(window, false);
                doFullscreenNextChange = true;
            }
            input.SetDelay(15);
        }
        if (isPressed[SDLK_F10] && input.IsDelayOver()) {
            if (isExclusive) {
                SDL_SetWindowBordered(window, false);
                isExclusive = false;
            }
            else {
                SDL_SetWindowBordered(window, true);
                isExclusive = true;
            }
            input.SetDelay(15);
        }
        // Tick internal counter.
        input.Tick();
    }
    renderThread.join();
	return 0;
}
#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(__linux__)
#define VK_USE_PLATFORM_WAYLAND_KHR //VK_USE_PLATFORM_XCB_KHR
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

static SDL_Window* CreateVulkanWindow(const char* title, int width = 1280, int height = 720) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cout << "Could not initialize SDL. Please ensure your system detects any monitor before continuing." << std::endl;
    }
    SDL_Window* window = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN);
    if (window == nullptr)
        std::cout << "Could not create SDL window." << std::endl;
    SDL_SetWindowBordered(window, false);
    SDL_SetWindowFullscreen(window, true);
    SDL_SetWindowMouseGrab(window, true);
    SDL_SetWindowRelativeMouseMode(window, true);
    SDL_ShowWindow(window);

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        // Poll initial events
    }
    return window;
}

static void doRendering(std::atomic<bool>* stillRunning, std::atomic<bool>* ready) {
    // Window creation.
    SDL_Window* window = CreateVulkanWindow("Chapter One");
    Renderer renderer(window, ready);
    bool grabMouse   = true;

    SDL_Event event;
    InputHandler input(window);
    while (*stillRunning) {
        *stillRunning = input.PollEvents(event, grabMouse, renderer.pitch, renderer.yaw);
        *stillRunning = !input.IsPressed(SDL_SCANCODE_ESCAPE);

        // Movement.
        float velocity = input.IsHeld(SDL_SCANCODE_LSHIFT) ? 0.5f : 0.1f;
        float forward  = input.IsHeld(SDL_SCANCODE_W) ? velocity : input.IsHeld(SDL_SCANCODE_S) ? -velocity : 0;
        float sideward = input.IsHeld(SDL_SCANCODE_A) ? velocity : input.IsHeld(SDL_SCANCODE_D) ? -velocity : 0;
        renderer.Move(forward, sideward);
        
        if (input.IsPressed(SDL_SCANCODE_F)) {
            grabMouse = !grabMouse;
            SDL_SetWindowMouseGrab(window, grabMouse);
            SDL_SetWindowRelativeMouseMode(window, grabMouse);
        }
        if (input.IsPressed(SDL_SCANCODE_R))
            renderer.Teleport(glm::vec3(0, 0, 0));

        if (input.IsPressed(SDL_SCANCODE_T))
            renderer.Teleport(glm::vec3(1, 1, 1));
        // Send position and rotation to logic handler.

        renderer.Draw();
    }
}

int main()
{
    std::atomic stillRunning = true;
    std::atomic ready        = false;

    // Start rendering
    std::thread renderThread(doRendering, &stillRunning, &ready);
    while (!ready) {}
    std::cout << "Ready!\n";

    while (stillRunning) {}
    renderThread.join();
	return 0;
}
#pragma once
#include "imgui/imgui_impl_sdl3.h"

#include <SDL3/SDL.h>

#include <map>
#include <vector>

class InputHandler
{
public:
	InputHandler(SDL_Window* Window, float Sensitivity = 0.1f);
	bool PollEvents(SDL_Event& Event, bool& GrabMouse, float& Pitch, float& Yaw);

	// Call after PollEvents.
	bool IsPressed(SDL_Scancode key);
	bool IsHeld(SDL_Scancode key);
private:
	SDL_Window* window;
	const bool* keyboardStates;
	std::map<SDL_Scancode, bool> isHeld;

	float sensitivity = 0.1f;
};
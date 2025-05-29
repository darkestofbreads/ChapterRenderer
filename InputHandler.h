#pragma once

#include <SDL3/SDL.h>

#include <map>
#include <vector>

class InputHandler
{
public:
	InputHandler();

	// Only necessary when not calling SDL_PollEvent.
	void HandleInput();

	bool IsPressed(SDL_Scancode key);
	bool IsHeld(SDL_Scancode key);
private:
	std::map<SDL_Scancode, bool> isHeld;
	const bool* keyboardStates;
};
#pragma once

#include <SDL3/SDL.h>

#include <map>

class InputHandler
{
public:
	InputHandler();
	void Tick();
	void SetDelay(uint32_t ticks);
	bool IsDelayOver();

	std::map<SDL_Keycode, bool> IsPressed;
private:
	uint32_t delay;
};


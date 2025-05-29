#include "InputHandler.h"
InputHandler::InputHandler() {
	keyboardStates = SDL_GetKeyboardState(NULL);
}
void InputHandler::HandleInput() {
	SDL_PumpEvents();
}
bool InputHandler::IsPressed(SDL_Scancode key) {
	auto& held = isHeld[key];
	if (keyboardStates[key]) {
		if (!held) {
			held = true;
			return true;
		}
		return false;
	}

	held = false;
	return false;
}
bool InputHandler::IsHeld(SDL_Scancode key) {
	return keyboardStates[key];
}
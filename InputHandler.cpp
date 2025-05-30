#include "InputHandler.h"
InputHandler::InputHandler(SDL_Window* Window, float Sensitivity) {
	keyboardStates = SDL_GetKeyboardState(NULL);
    sensitivity    = Sensitivity;
    window         = Window;
}
bool InputHandler::PollEvents(SDL_Event& Event, bool& GrabMouse, float& Pitch, float& Yaw) {
    while (SDL_PollEvent(&Event)) {
        ImGui_ImplSDL3_ProcessEvent(&Event);
        switch (Event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            if (GrabMouse) {
                float xOffset = Event.motion.xrel * sensitivity;
                float yOffset = Event.motion.yrel * sensitivity;
                Pitch += yOffset;
                Yaw = Yaw + xOffset;
                if (Pitch < -89.0f)
                    Pitch = -89.0f;
                if (Pitch > 89.0f)
                    Pitch = 89.0f;
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            GrabMouse = false;
            SDL_SetWindowMouseGrab(window, GrabMouse);
            SDL_SetWindowRelativeMouseMode(window, GrabMouse);
            break;
        case SDL_EVENT_QUIT:
            return false;
            break;
        default:
            break;
        }
    }
    return true;
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
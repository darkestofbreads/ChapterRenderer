#include "InputHandler.h"

InputHandler::InputHandler() {
	delay = 0;
}
void InputHandler::Tick() {
	if (delay > 0)
		delay--;
}
void InputHandler::SetDelay(uint32_t ticks) {
	delay = ticks;
}
bool InputHandler::IsDelayOver() {
	return (delay == 0);
}
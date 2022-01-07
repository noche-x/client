#include "api.hpp"

#include <cstdarg>
#include <string>

#include "game.hpp"

namespace api {
void drawText(const char *text, float x, float y, float scale, int params,
              float r, float g, float b, float a, ...) {
	char buffer[1024];
	va_list list;
	va_start(list, a);
	vsnprintf(buffer, 1024, text, list);
#ifdef _WIN32
	g_game->drawTextFunc(buffer, x, y, scale, params, r, g, b, a);
#else
	g_game->drawTextFunc(buffer, params, x, y, scale, r, g, b, a);
#endif
	va_end(list);
}
}  // namespace api
/* wheel.h - the game wheel: the chosen game's cover large in the middle over its dimmed
 * screenshot, neighbours smaller above and below. Ported from FIESTACADE's menu. */
#pragma once
#include <stdbool.h>
typedef enum { WHEEL_PLAY, WHEEL_CREDITS, WHEEL_IDLE, WHEEL_CONTROLLER } wheel_result_t;
void wheel_init(int games);              /* games + one Credits entry at the end */
void wheel_select(int game);
int  wheel_selected(void);
wheel_result_t wheel_run(int *game);     /* interactive; returns when something is chosen or it idles out */
bool wheel_showcase(int seconds_per_game);   /* attract mode: turns by itself; true if a button was pressed */

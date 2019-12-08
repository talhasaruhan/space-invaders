#ifndef CONFIG_H
#define CONFIG_H

#include "Engine.h"
#include <stdint.h>

/* GAMEPLAY CONFIGURATION */

#define DISPLAY_CONFIGURATION 1

#define PLAYER_START_HEALTH 3
#define PLAYER_MOVE_SPEED_PX_PER_SEC 150.0f

#define ROCKET_MOVE_SPEED_PX_PER_SEC 350.0f
#define LOG_MAX_NUMBER_ROCKETS 3

#define ALIENS_SPEED_PX_PER_SEC 100.0f
#define ALIENS_Y_JUMP_PX 50
#define ALIEN_INITIAL_POS_X 0
#define ALIEN_INITIAL_POS_Y 0
#define ALIEN_INITIAL_DIRECTION 1
#define ALIEN_FORMATION_NUM_ROWS 4
#define ALIEN_FORMATION_NUM_COLS 8
#define ALIEN_FORMATION_INNER_PADDING_X 10
#define ALIEN_FORMATION_INNER_PADDING_Y 10
#define ALIEN_BORDER_CORRECTION_MARGIN 5

#define BOMB_MOVE_SPEED_PX_PER_SEC 80.0f
#define ALIEN_BOMB_DROP_CHANCE_EACH_SEC 0.1f
#define BOMB_SPAWN_OFFSET_X 0
#define BOMB_SPAWN_OFFSET_Y 24
#define LOG_MAX_NUMBER_BOMBS 6

#define PLAYER_DEATH_GHOST_NUMBER_OF_BLINKS 5u
#define PLAYER_DEATH_GHOST_BLINK_PERIOD 0.2f

/******************************************************/
/* PICK THE RIGHT UNDERLYING CONTAINER DEPENDING ON THE
 * MAXIMUM NUMBER OF ALIENS THAT CAN EXIST SIDE BY SIDE.
 * -------------- DON'T TOUCH THESE ------------------*/
#if (ALIEN_FORMATION_NUM_COLS > 32)
static_assert(false);
#elif (ALIEN_FORMATION_NUM_COLS > 16)
#define ALIEN_MASK_T uint32_t
#define ALIEN_MASK_BITS_LOG 5
#elif (ALIEN_FORMATION_NUM_COLS > 8)
#define ALIEN_MASK_T uint16_t
#define ALIEN_MASK_BITS_LOG 4
#else
#define ALIEN_MASK_T uint8_t
#define ALIEN_MASK_BITS_LOG 3
#endif
/******************************************************/

/* ALIEN SPAWN FORMATION SETTINGS. */
#define ALIEN_RANDOM_FORMATION 0
#define ALIEN_RANDOM_ENEMY_TYPE 0

/* If random is set to 0, these values are going to be used cyclically. */
#if (!ALIEN_RANDOM_FORMATION || !ALIEN_RANDOM_ENEMY_TYPE)
#define ALIEN_NUM_PREDETERMINED_FORMATIONS 4
static const Engine::Sprite ALIEN_PREDETERMINED_TYPE[ALIEN_NUM_PREDETERMINED_FORMATIONS] = {
    Engine::Sprite::Enemy1,
    Engine::Sprite::Enemy2,
    Engine::Sprite::Enemy2,
    Engine::Sprite::Enemy1};
static const ALIEN_MASK_T ALIEN_PREDETERMINED_FORMATIONS[ALIEN_NUM_PREDETERMINED_FORMATIONS]
                                                        [ALIEN_FORMATION_NUM_ROWS] = {
                                                            {0x01, 0x20, 0x03, 0x6D},
                                                            {0x41, 0x2D, 0xAA, 0x2E},
                                                            {0x0A, 0xE7, 0xF1, 0x4F},
                                                            {0x63, 0xB1, 0x23, 0x18}};
#endif

/* COLLISION THRESHOLDS -- DON'T TOUCH THESE. */
#define ROCKET_ALIEN_COLLISION_X_DIST 16
#define ROCKET_ALIEN_COLLISION_Y_DIST 20

#define ALIEN_PLAYER_COLLISION_X_DIST 28
#define ALIEN_PLAYER_COLLISION_Y_DIST 22

#define PLAYER_BOMB_COLLISION_X_DIST 20
#define PLAYER_BOMB_COLLISION_Y_DIST 16

#endif

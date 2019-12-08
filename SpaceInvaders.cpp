#include "intrin.h"
#include <cmath>
#include <malloc.h>
#include <memory.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Config.h"

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t i8;

typedef i16 pixel_t;
typedef float pos_t;
typedef i32 pixel_wide_t;

/* Define a macro to allocate memory on stack and zero the given memory as a shorthand. */
#define ALLOC_ON_STACK(TYPE, N) (TYPE*)_malloca(N * sizeof(TYPE))
#define ZERO_MEM(DEST, N)   \
	if (DEST)               \
	{                       \
		memset(DEST, 0, N); \
	}                       \
	else                    \
	{                       \
		exit(1);            \
	}

/* These are the constants dependent on the defines in Config.h */
namespace game_constants
{
static const pixel_t player_position_y = Engine::CanvasHeight - Engine::SpriteSize;
static const u8 max_num_rockets = 1 << LOG_MAX_NUMBER_ROCKETS;
static const u8 max_num_bombs = 1 << LOG_MAX_NUMBER_BOMBS;
static const float rocket_firing_cooldown =
    (float)Engine::CanvasHeight / ((float)max_num_rockets * ROCKET_MOVE_SPEED_PX_PER_SEC);
static const pos_t rocket_start_y =
    (pos_t)game_constants::player_position_y - ((pos_t)Engine::SpriteSize * 0.5f);
static const pos_t player_initial_position_x = (Engine::CanvasWidth - Engine::SpriteSize) * 0.5;
}; // namespace game_constants

/* XorShift with 32 bit state word, taken from Wikipedia. */
u32 xorshift32()
{
	/* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
	static u32 x = rand();
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

/* Returns a float between 0 and 1, uses XorShift to generate a random mantissa. */
inline float UnitRandom()
{
	/* The resulting float will be between 1.0-1.999f, so subtract one to return [0, 1]. */
	u32 r = (xorshift32() & 0x007fffff) | 0x3f800000;
	return *(float*)&r - 1.0f;
}

/* Simple particle system that's being used for bombs and rockets. */
struct ParticleAttributes
{
	u8 alive = 0;
	pixel_t pos_x = 0;
	pos_t pos_y = 0;
};
struct ParticleSystem
{
	i8 num_particles = 0;
	ParticleAttributes* attributes;
};

inline void AddRocket(ParticleSystem* rocket_system, pixel_t rocket_x)
{
	i8 end = (rocket_system->num_particles++) & ((1 << LOG_MAX_NUMBER_ROCKETS) - 1);
	ParticleAttributes* attributes = &rocket_system->attributes[end];
	attributes->alive = true;
	attributes->pos_x = rocket_x;
	attributes->pos_y = game_constants::rocket_start_y;
}

inline void AddBomb(ParticleSystem* bomb_system, pixel_t bomb_x, pixel_t bomb_y)
{
	i8 end = (bomb_system->num_particles++) & ((1 << LOG_MAX_NUMBER_BOMBS) - 1);
	ParticleAttributes* attributes = &bomb_system->attributes[end];
	attributes->alive = true;
	attributes->pos_x = bomb_x;
	attributes->pos_y = (pos_t)bomb_y;
}

struct AlienSystem
{
	/* Position of the AlienSystem corresponds to top left of the grid. */
	pos_t pos_x = 0;
	pos_t pos_y = 0;
	ALIEN_MASK_T* aliens_mask = NULL;
	Engine::Sprite alien_sprite;

	u16 _width = 0;
#if (0)
	u16 _height = 0;
#endif
	i8 _direction = 1;
};

inline void ResetAlienSystem(AlienSystem* alien_system)
{
	alien_system->pos_x = ALIEN_INITIAL_POS_X;
	alien_system->pos_y = ALIEN_INITIAL_POS_Y;
	alien_system->_direction = ALIEN_INITIAL_DIRECTION;
	alien_system->_width =
	    ALIEN_FORMATION_NUM_COLS * (Engine::SpriteSize + ALIEN_FORMATION_INNER_PADDING_X) -
	    ALIEN_FORMATION_INNER_PADDING_X;
#if (0)
	alien_system->_height =
	    ALIEN_FORMATION_NUM_ROWS * (Engine::SpriteSize + ALIEN_FORMATION_INNER_PADDING_Y) -
	    ALIEN_FORMATION_INNER_PADDING_Y;
#endif
	ZERO_MEM(alien_system->aliens_mask, ALIEN_FORMATION_NUM_ROWS * sizeof(ALIEN_MASK_T));

	/* Since we're storing each row in a word of length equal to the next power of 2,
	 * we need to mask the unused bits. */
	const ALIEN_MASK_T col_mask = (ALIEN_MASK_T)(((u32)1 << ALIEN_FORMATION_NUM_COLS) - 1);

#if (!ALIEN_RANDOM_FORMATION || !ALIEN_RANDOM_ENEMY_TYPE)
	/* If we're randoming either formation or the type of the aliens,
	 * we need a counter to pick the next predetermined formation/type. */
	static u8 predetermined_formation = 0;
#endif

#if (ALIEN_RANDOM_FORMATION)
	for (int i = 0; i < ALIEN_FORMATION_NUM_ROWS; ++i)
	{
		u32 r = xorshift32();
		alien_system->aliens_mask[i] = (ALIEN_MASK_T)r & col_mask;
	}
#else
	for (int i = 0; i < ALIEN_FORMATION_NUM_ROWS; ++i)
	{
		alien_system->aliens_mask[i] =
		    ALIEN_PREDETERMINED_FORMATIONS[predetermined_formation][i] & col_mask;
	}
#endif

#if (ALIEN_RANDOM_ENEMY_TYPE)
	/* If random enemy type flag is set, pick Enemy1 or Enemy2 by 50/50 chance. */
	float r = UnitRandom();
	if (r < 0.5f)
	{
		alien_system->alien_sprite = Engine::Sprite::Enemy1;
	}
	else
	{
		alien_system->alien_sprite = Engine::Sprite::Enemy2;
	}
#else
	/* If random enemy type flag is unset, set the enemy type to the next predetermined one. */
	alien_system->alien_sprite = ALIEN_PREDETERMINED_TYPE[predetermined_formation];
#endif

#if (!ALIEN_RANDOM_FORMATION || !ALIEN_RANDOM_ENEMY_TYPE)
	predetermined_formation++;
	/* If the define is a power of 2, compiler should optimize this division into a simple and. */
	predetermined_formation %= ALIEN_NUM_PREDETERMINED_FORMATIONS;
#endif
}

/* This function moves the alien system on the canvas. It doesn't contain any loops,
 * rather, it takes the cumulative or of the row masks from the main loop as input
 * to calculate leftmost and rightmost set bits (leftmost and rightmost existing aliens). */
void MoveAlienSystem(AlienSystem* alien_system, ALIEN_MASK_T cor, float dt)
{
	{
		/* Update the position of the alien system. */
		alien_system->pos_x += dt * ALIENS_SPEED_PX_PER_SEC * alien_system->_direction;
	}

	{
		/* If pos_x < 0:
		 *   check if the leftmost alien touches the border.
		 * If pos_x > CanvasWidth - AlienSystemWidth:
		 *   check if the rightmost alien touches the border. */
		const pos_t pos_x = alien_system->pos_x;

		if (pos_x < 0)
		{
			unsigned long leftmost_alien;
			/* Find the leftmost aliens position within the grid. */
			_BitScanForward(&leftmost_alien, cor);

			pixel_t margin = -1 * (pixel_t)leftmost_alien *
			                 (Engine::SpriteSize + ALIEN_FORMATION_INNER_PADDING_X);

			if (pos_x < margin)
			{
				alien_system->pos_y += ALIENS_Y_JUMP_PX;
				alien_system->_direction = 1;
				alien_system->pos_x = (pos_t)margin + ALIEN_BORDER_CORRECTION_MARGIN;
			}
		}
		else
		{
			pixel_t margin = Engine::CanvasWidth - alien_system->_width;
			if (pos_x > margin)
			{
				unsigned long rightmost_alien;
				/* Find the rightmost aliens position within the grid. */
				_BitScanReverse(&rightmost_alien, cor);

				margin += (pixel_t)(ALIEN_FORMATION_NUM_COLS - rightmost_alien - 1) *
				          (Engine::SpriteSize + ALIEN_FORMATION_INNER_PADDING_X);
				if (pos_x > margin)
				{
					alien_system->pos_y += ALIENS_Y_JUMP_PX;
					alien_system->_direction = -1;
					alien_system->pos_x = (pos_t)margin - ALIEN_BORDER_CORRECTION_MARGIN;
				}
			}
		}
	}
}

inline u8 CollisionTest(pixel_t x1,
                        pixel_t y1,
                        pixel_t x2,
                        pixel_t y2,
                        pixel_t x_threshold,
                        pixel_t y_threshold)
{
	/* abs implementation should be branchless. */
	pixel_t xdif = (pixel_t)abs(x1 - x2);
	pixel_t ydif = (pixel_t)abs(y1 - y2);
	return (xdif <= x_threshold) & (ydif <= y_threshold);
}

struct GameState
{
	u64 bombs_dropped : 16;
	u64 rockets_fired : 16;
	u64 aliens_killed : 16;

	u64 game_over : 4;
	u64 player_health : 6;

	/* Player temporarily becomes a ghost after dying and respawning.
	 * In the ghost state, player is invincible yet it still
	 * destroys the bombs and aliens coming in contact with him.
	 * Player knows he is in the ghost state because the sprite blinks.
	 * 0 if player is not in the ghost state,
	 * == blink counter + 1 if it is.*/
	u64 player_ghost : 6;

	pos_t player_position_x;
	/* Time since the player entered into the ghost state. */
	float player_ghost_timer;
	/* The timestamp last rocket was fired at. */
	double rocket_last_fired;
};

inline void PlayerKilled(GameState* game_state)
{
	game_state->player_health--;
	game_state->game_over = !game_state->player_health;
	game_state->player_position_x = game_constants::player_initial_position_x;
	game_state->player_ghost = 1;
	game_state->player_ghost_timer = 0.0f;
}

void EngineMain()
{
	Engine engine;

	/* Greeting part. */
	/* Scopes help get rid of unnecessary character buffers when the game starts
	 * AND it provides code clarity. */
	{
		/* Display greeting message, controls scheme and optionally, the configuration message. */
		const char greeting_message[] = "Good Luck!";
		pixel_wide_t greeting_text_x =
		    (Engine::CanvasWidth - (sizeof(greeting_message) - 1) * Engine::FontWidth) / 2;
		pixel_wide_t greeting_text_y = (Engine::CanvasHeight - Engine::FontRowHeight) / 2 - 150;

		char controls_text[128];
		sprintf_s(controls_text, "Controls: Left Arrow, Right Arrow, Space.");
		pixel_wide_t controls_text_x =
		    (Engine::CanvasWidth - (strlen(controls_text) - 1) * Engine::FontWidth) / 2;
		pixel_wide_t controls_text_y = (Engine::CanvasHeight - Engine::FontRowHeight) / 2 - 50;

#if (DISPLAY_CONFIGURATION)
		char config_text[160];
		sprintf_s(config_text,
		          "Lives: %d\nRandom Formation: %d\nRandom Enemy: %d\nPlayer SPD: %.0f\n"
		          "Alien SPD: %.0f\nRocket SPD: %.0f\nBomb SPD: %.0f\n"
		          "Bomb Chance (/Alien/Sec): %.2f",
		          PLAYER_START_HEALTH,
		          ALIEN_RANDOM_FORMATION,
		          ALIEN_RANDOM_ENEMY_TYPE,
		          PLAYER_MOVE_SPEED_PX_PER_SEC,
		          ALIENS_SPEED_PX_PER_SEC,
		          ROCKET_MOVE_SPEED_PX_PER_SEC,
		          BOMB_MOVE_SPEED_PX_PER_SEC,
		          ALIEN_BOMB_DROP_CHANCE_EACH_SEC);
		pixel_wide_t config_text_x =
		    (Engine::CanvasWidth - (strlen(config_text) - 1) / 6 * Engine::FontWidth) / 2;
		pixel_wide_t config_text_y = (Engine::CanvasHeight - Engine::FontRowHeight) / 2;
#endif

		while (true)
		{
			bool keep_going = engine.startFrame();
			if (!keep_going)
			{
				/* If the player hits the ESC key or closes the window, exit the game. */
				return;
			}
			Engine::PlayerInput keys = engine.getPlayerInput();
			if (keys.left || keys.right || keys.fire)
			{
				/* Start the actual game when player gives an input. */
				break;
			}
			engine.drawText(greeting_message, greeting_text_x, greeting_text_y);
			engine.drawText(controls_text, controls_text_x, controls_text_y);
#if (DISPLAY_CONFIGURATION)
			engine.drawText(config_text, config_text_x, config_text_y);
#endif
		}
	}

	/* The actual game. */

	/* Set up game systems. */

	GameState game_state;
	memset(&game_state, 0x00, sizeof(GameState));
	game_state.player_health = PLAYER_START_HEALTH;
	game_state.player_position_x = game_constants::player_initial_position_x;

	ParticleSystem rocket_system;
	rocket_system.attributes = ALLOC_ON_STACK(ParticleAttributes, game_constants::max_num_rockets);
	ZERO_MEM(rocket_system.attributes, game_constants::max_num_rockets * sizeof(ParticleAttributes))

	ParticleSystem bomb_system;
	bomb_system.attributes = ALLOC_ON_STACK(ParticleAttributes, game_constants::max_num_bombs);
	ZERO_MEM(bomb_system.attributes, game_constants::max_num_bombs * sizeof(ParticleAttributes))

	AlienSystem alien_system;
	alien_system.aliens_mask = ALLOC_ON_STACK(ALIEN_MASK_T, ALIEN_FORMATION_NUM_ROWS);
	ResetAlienSystem(&alien_system);

#if (PLAYER_START_HEALTH < 10)
	/* If start health (max possible health value) is less than 10,
	 * just put the appropriate character into the string.
	 * So let's allocate the health text here. */
	char health_text[] = "Lives left:  ";
	/* Otherwise we'll use sprintf with a local buffer. */
#endif

	double previous_timestamp = engine.getStopwatchElapsedSeconds();
	double timestamp;
	float delta_t;

	while (engine.startFrame() && !game_state.game_over)
	{
		/* Get the frame timing. */
		timestamp = engine.getStopwatchElapsedSeconds();
		delta_t = (float)(timestamp - previous_timestamp);

		/* Check for the player input. */
		Engine::PlayerInput keys = engine.getPlayerInput();
		pos_t pos_dif = delta_t * PLAYER_MOVE_SPEED_PX_PER_SEC;
		if (keys.left)
		{
			game_state.player_position_x -= pos_dif;
		}
		else if (keys.right)
		{
			game_state.player_position_x += pos_dif;
		}
		else if (keys.fire)
		{
			/* Fire only if your guns are not on cooldown. */
			if ((timestamp - game_state.rocket_last_fired) >=
			    game_constants::rocket_firing_cooldown)
			{
				game_state.rocket_last_fired = timestamp;
				game_state.rockets_fired++;
				AddRocket(&rocket_system, (pixel_t)game_state.player_position_x);
			}
		}

		/* Before drawing the player, check if the player is in 'ghost' state. */
		if (game_state.player_ghost)
		{
			/* If the player IS in ghost state, draw it blinking so the player knows it. */
			game_state.player_ghost_timer += delta_t;
			/* Draw the player blinking. */
			if (game_state.player_ghost_timer -
			        (game_state.player_ghost - 1) * PLAYER_DEATH_GHOST_BLINK_PERIOD >
			    PLAYER_DEATH_GHOST_BLINK_PERIOD * 0.5f)
			{
				game_state.player_ghost++;
			}
			if (game_state.player_ghost & 0x01)
			{
				/* Draw the player */
				engine.drawSprite(Engine::Sprite::Player,
				                  (pixel_wide_t)game_state.player_position_x,
				                  (pixel_wide_t)game_constants::player_position_y);
			}
			game_state.player_ghost *=
			    (game_state.player_ghost < PLAYER_DEATH_GHOST_NUMBER_OF_BLINKS + 1);
		}
		else
		{
			/* Draw the player */
			engine.drawSprite(Engine::Sprite::Player,
			                  (pixel_wide_t)game_state.player_position_x,
			                  (pixel_wide_t)game_constants::player_position_y);
		}

		/* Update rocket positions and draw rockets in the same loop.
		 * Normally, having two separate loops over same addresses is completely fine,
		 * BUT since our target platform doesn't have branch predictor, we would be doing
		 * whole lot of unnecessary comparisons. */
		for (i8 i = 0; i < game_constants::max_num_rockets; ++i)
		{
			ParticleAttributes* attributes = &rocket_system.attributes[i];
			u8* alive = &attributes->alive;
			if (*alive)
			{
				/* Update the position of the rocket. */
				attributes->pos_y -= delta_t * ROCKET_MOVE_SPEED_PX_PER_SEC;
				/* Set 'alive' to 0 if rocket passes the top of the screen. */
				pos_t p_y = attributes->pos_y;
				*alive = (p_y >= 0);
				/* Draw the rocket. */
				engine.drawSprite(Engine::Sprite::Rocket,
				                  (pixel_wide_t)attributes->pos_x,
				                  (pixel_wide_t)p_y);
			}
		}

		/* Draw and update the aliens and check for the collisions,
		 * also make the decision to drop a bomb or not. */
		{
			/* To be used to find the leftmost and rightmost aliens. */
			ALIEN_MASK_T alien_mask_cumulative_or = 0;

			i8 bottommost_alien_row = 0;

			/* pos_y corresponds to the y coordinate of the row being processed. */
			pixel_t pos_y = (pixel_t)alien_system.pos_y;

			/* Bomb drop chance this frame. */
			float bomb_drop_chance = ALIEN_BOMB_DROP_CHANCE_EACH_SEC * delta_t;

			/* Reused over and over again. */
			const pixel_t x_stride = (pixel_t)Engine::SpriteSize + ALIEN_FORMATION_INNER_PADDING_X;
			const pixel_t y_stride = (pixel_t)Engine::SpriteSize + ALIEN_FORMATION_INNER_PADDING_Y;

			for (i8 i = 0; i < ALIEN_FORMATION_NUM_ROWS; ++i)
			{
				ALIEN_MASK_T* row = &alien_system.aliens_mask[i];

				/* Update the cumulative or of masks. */
				alien_mask_cumulative_or |= *row;

				bottommost_alien_row |= (u8)(*row != 0) << i;

				/* pos_y corresponds to the x coordinate of the current alien being processed. */
				pixel_t pos_x = (pixel_t)alien_system.pos_x;

				for (i8 j = 0; j < ALIEN_FORMATION_NUM_COLS; ++j)
				{
					/* If the bit is set in the row, an alien at the position of the bit
					 * exists at that row. */
					if (*row & ((ALIEN_MASK_T)1 << j))
					{
						/* Draw the alien. */
						engine.drawSprite(alien_system.alien_sprite,
						                  (pixel_wide_t)pos_x,
						                  (pixel_wide_t)pos_y);
						/* Is used to store the results of the collision tests. */
						u8 is_destroyed = 0;

						/* Make the decision to drop a bomb or not. */
						{
							float r = UnitRandom();
							if (r < bomb_drop_chance)
							{
								game_state.bombs_dropped++;
								pixel_t bomb_x = (pixel_t)(pos_x + BOMB_SPAWN_OFFSET_X);
								pixel_t bomb_y = (pixel_t)(pos_y + BOMB_SPAWN_OFFSET_Y);
								AddBomb(&bomb_system, bomb_x, bomb_y);
							}
						}

						/* Check collision with the rockets. */
						for (i8 k = 0; k < game_constants::max_num_rockets; ++k)
						{
							ParticleAttributes* attributes = &rocket_system.attributes[k];
							u8* rocket_exists = &attributes->alive;
							pixel_t rocket_x = attributes->pos_x;
							pixel_t rocket_y = (pixel_t)attributes->pos_y;
							u8 collision_test = CollisionTest((pixel_t)pos_x,
							                                  (pixel_t)pos_y,
							                                  rocket_x,
							                                  rocket_y,
							                                  ROCKET_ALIEN_COLLISION_X_DIST,
							                                  ROCKET_ALIEN_COLLISION_Y_DIST);
							/* Say no to branches. */
							is_destroyed |= *rocket_exists & collision_test;
							*rocket_exists &= (collision_test ^ 0x01);
						}

						/* Check collision against the player. */
						if (!game_state.player_ghost)
						{
							u8 collision_test = CollisionTest((pixel_t)pos_x,
							                                  (pixel_t)pos_y,
							                                  (pixel_t)game_state.player_position_x,
							                                  game_constants::player_position_y,
							                                  ALIEN_PLAYER_COLLISION_X_DIST,
							                                  ALIEN_PLAYER_COLLISION_Y_DIST);
							/* We could just eliminate this branch but in this case it'd run slower
							 * since the PlayerKilled function has 5-6 writes in it. */
							if (collision_test & !game_state.player_ghost)
							{
								PlayerKilled(&game_state);
							}
							/* Destroy the alien even if the player is in the ghost state.
							 * This is just a design preference, not a bug. */
							is_destroyed |= collision_test;
						}

						/* Say no to branches. */
						game_state.aliens_killed += is_destroyed;
						*row &= ~((ALIEN_MASK_T)is_destroyed << j);
					}
					pos_x += x_stride;
				}
				pos_y += y_stride;
			}

			/* If all the aliens are killed create a new one. */
			if (!alien_mask_cumulative_or)
			{
				ResetAlienSystem(&alien_system);
			}
			else
			{
				/* Find the bottommost row with at least one alien in it. */
				{
					unsigned long bottom_row;
					_BitScanReverse(&bottom_row, bottommost_alien_row);

					/* If the aliens in the bottommost row cross the bottom edge of the screen,
					 * end the game. */

					pixel_t bottom_line = (pixel_t)alien_system.pos_y + Engine::SpriteSize +
					                      (pixel_t)bottom_row * (Engine::SpriteSize +
					                                             ALIEN_FORMATION_INNER_PADDING_Y);

					game_state.game_over |= (bottom_line > Engine::CanvasHeight);
				}

				/* Update the Alien System. This function contains no loops,
				 * instead it uses the cumulative or of alien masks from the loop above. */
				MoveAlienSystem(&alien_system, alien_mask_cumulative_or, delta_t);
			}
		}

		/* Draw and update the bombs */
		for (i8 i = 0; i < game_constants::max_num_bombs; ++i)
		{
			ParticleAttributes* attributes = &bomb_system.attributes[i];
			u8 alive = attributes->alive;
			if (alive)
			{
				/* Update the position.*/
				attributes->pos_y += delta_t * BOMB_MOVE_SPEED_PX_PER_SEC;
				pixel_t bomb_x = attributes->pos_x;
				pixel_t bomb_y = (pixel_t)attributes->pos_y;
				/* Set 'alive' to 0 if bombs passes the bottom of the screen. */
				alive = (bomb_y >= Engine::CanvasHeight);

				/* Draw the bomb. */
				engine.drawSprite(Engine::Sprite::Bomb, (pixel_wide_t)bomb_x, (pixel_wide_t)bomb_y);

				/* Check collision against the player. */
				u8 collision_test = CollisionTest(bomb_x,
				                                  bomb_y,
				                                  (pixel_t)game_state.player_position_x,
				                                  game_constants::player_position_y,
				                                  PLAYER_BOMB_COLLISION_X_DIST,
				                                  PLAYER_BOMB_COLLISION_Y_DIST);
				/* To avoid lots of writes, test the branch instead. */
				if (collision_test & !game_state.player_ghost)
				{
					PlayerKilled(&game_state);
				}
				/* Destroy the rocket even if the player is in the ghost state. */
				attributes->alive &= ~collision_test;
			}
		}

		/* Draw the text. */
#if (PLAYER_START_HEALTH < 10)
		/* If start health (max possible health value) is less than 10,
		 * just put the appropriate character into the string. */
		health_text[sizeof(health_text) - 2] = (u8)game_state.player_health + '0';
		engine.drawText(health_text, 5, 5);
#else
		/* Otherwise, use sprintf */
		char health_text_buf[32];
		sprintf_s(health_text_buf, "Lives left: %d", (i32)game_state.player_health);
		engine.drawText(health_text_buf, 5, 5);
#endif

		char score_text_buf[16];
		sprintf_s(score_text_buf, "Score: %d", (i32)game_state.aliens_killed);
		engine.drawText(score_text_buf,
		                Engine::CanvasWidth - strlen(score_text_buf) * Engine::FontWidth - 5,
		                5);

		previous_timestamp = timestamp;
	}

	/* Game over screen. */

	const char game_over_message[] = "Game Over!";
	pixel_wide_t game_over_text_x =
	    (Engine::CanvasWidth - (sizeof(game_over_message) - 1) * Engine::FontWidth) / 2;
	pixel_wide_t game_over_text_y = (Engine::CanvasHeight - Engine::FontRowHeight) / 2 - 50;

	char stats_text[128];
	sprintf_s(stats_text,
	          "#Aliens killed: %d\n#Rockets fired: %d\n#Bombs dropped: %d",
	          (i32)game_state.aliens_killed,
	          (i32)game_state.rockets_fired,
	          (i32)game_state.bombs_dropped);
	pixel_wide_t stats_text_x =
	    (Engine::CanvasWidth - (strlen(stats_text) - 1) * Engine::FontWidth / 3) / 2;
	pixel_wide_t stats_text_y = (Engine::CanvasHeight - Engine::FontRowHeight) / 2;

	while (engine.startFrame() && game_state.game_over)
	{
		engine.drawText(game_over_message, game_over_text_x, game_over_text_y);
		engine.drawText(stats_text, stats_text_x, stats_text_y);
	}

	return;
}

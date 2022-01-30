#include "wasm4.h"

#include "images.h"

#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

#define BALL_RADIUS 4
#define BALL_GRAVITY 0.1
#define BALL_BOUNCE 4
#define BALL_START_X 30
#define BALL_START_HEIGHT 40
#define BALL_START_VELOCITY 3

#define PLAYER_RADIUS 10
#define PLAYER_GRAVITY 0.2
#define PLAYER_JUMP_VELOCITY 5
#define PLAYER_WALK_VELOCITY 2
#define PLAYER_MASS 0.25
#define PLAYER_START_X 30
#define PLAYER_SPIKE_POWER 1.2

#define NET_HEIGHT 25
#define NET_WIDTH 4
#define GROUND_HEIGHT 4
#define SCORE_LIMIT 4

typedef struct {
    float x, y;
    float velX, velY;

    // The X position that the AI wants to be in to catch the ball
    float cpuTargetX;
} Ball;

typedef struct  {
    float x, y;
    float velX, velY;
} Player;

static Ball ball;
static Player players[4];

static int playerCount = 2;
static int gameMode;

static int scoreLeft, scoreRight;
static bool rightSideServes;

static int pauseTime;
static enum { STATE_TITLE, STATE_PLAYING } state;
static uint8_t lastGamepadState;

void resetRally () {
    ball.x = rightSideServes ? 160-BALL_START_X : BALL_START_X;
    ball.y = 160-GROUND_HEIGHT-BALL_START_HEIGHT;
    ball.velX = 0;
    ball.velY = -BALL_START_VELOCITY;

    ball.cpuTargetX = rightSideServes ? ball.x + (0.2+rand()/(float)RAND_MAX) * PLAYER_RADIUS : 160/2 + 160/4;

    for (int ii = 0; ii < playerCount; ++ii) {
        bool rightSide = ii & 1;
        Player* player = &players[ii];
        player->x = rightSide ? 160-PLAYER_START_X : PLAYER_START_X;
        if (ii > 1) {
            player->x += (rightSide ? -3 : 3)*PLAYER_RADIUS;
        }
        player->y = 160-GROUND_HEIGHT-PLAYER_RADIUS;
        player->velX = 0;
        player->velY = 0;
    }
}

void resetGame () {
    scoreLeft = 0;
    scoreRight = 0;
    resetRally();
}

// void start () {
//     resetGame();
// }

void updateBall (Ball* ball) {
    ball->x += ball->velX;
    ball->y += ball->velY;

    // Check ball-on-ground collision
    if (ball->y > 160-GROUND_HEIGHT-BALL_RADIUS && ball->velY > 0) {
        bool rightSideScored = ball->x < 160/2;
        if (rightSideScored) {
            ++scoreRight;
        } else {
            ++scoreLeft;
        }
        rightSideServes = rightSideScored;
        pauseTime = (scoreLeft >= SCORE_LIMIT || scoreRight >= SCORE_LIMIT) ? 120 : 60;
        ball->y = 160-GROUND_HEIGHT-BALL_RADIUS;
        tone(140, 24 << 8, 100, TONE_NOISE);
    }

    // Ball-on-sidewall collisions
    if (ball->x < BALL_RADIUS && ball->velX < 0) {
        ball->velX = -ball->velX;
    }
    if (ball->x > 160-BALL_RADIUS && ball->velX > 0) {
        ball->velX = -ball->velX;
    }

    // TODO(2022-01-20): Move to after or before all collision handling?
    ball->velY += BALL_GRAVITY;

    bool velYChanged = false;

    // Check ball-on-player collisions
    for (int ii = 0; ii < playerCount; ++ii) {
        Player* player = &players[ii];
        float dx = ball->x - player->x;
        float dy = ball->y - player->y;
        float dist2 = dx*dx + dy*dy;
        float minDist2 = (PLAYER_RADIUS+BALL_RADIUS) * (PLAYER_RADIUS+BALL_RADIUS);
        if (dist2 < minDist2) {
            float dist = sqrt(dist2);
            ball->x = player->x + (PLAYER_RADIUS+BALL_RADIUS) * dx/dist;
            ball->y = player->y + (PLAYER_RADIUS+BALL_RADIUS) * dy/dist;

            ball->velX = PLAYER_MASS*player->velX + BALL_BOUNCE*dx/(PLAYER_RADIUS+BALL_RADIUS);
            ball->velY = PLAYER_MASS*player->velY + BALL_BOUNCE*dy/(PLAYER_RADIUS+BALL_RADIUS);

            if (ball->velY > 1 && ball->y < 160-GROUND_HEIGHT-2*PLAYER_RADIUS) {
                // Spike it!
                ball->velX *= PLAYER_SPIKE_POWER;
                ball->velY *= PLAYER_SPIKE_POWER;
                tone(100 | (600 << 16), 16 << 8, 100, TONE_NOISE);
            } else {
                tone(ball->x < 160/2 ? 65 : 62, 4 << 8, 100, TONE_PULSE1 | TONE_MODE3);
            }

            velYChanged = true;

        }
    }

    // Check ball-on-net collision
    if (ball->y > 160-GROUND_HEIGHT-NET_HEIGHT-BALL_RADIUS
            && ball->x > 160/2-NET_WIDTH/2-BALL_RADIUS
            && ball->x < 160/2+NET_WIDTH/2+BALL_RADIUS) {
        if (ball->y > 160-GROUND_HEIGHT-NET_HEIGHT) {
            ball->velX = -ball->velX;
        } else if (ball->velY > 0) {
            ball->velY = -ball->velY;
            velYChanged = true;
        }
    }

    // Update cpuTargetX if needed
    if (gameMode == 0 && velYChanged) {
        float h = 160-GROUND_HEIGHT-2*PLAYER_RADIUS;
        float b = ball->velY;
        float a = BALL_GRAVITY;
        float c = h-ball->y;
        float t = (-b + sqrt(b*b + 2*a*c)) / a;

        // Calculate the position the ball will land
        float x = ball->x + t*ball->velX;
        if (x < 0) {
            // Reflect off left wall
            x = -x;
        }
        if (x > 160) {
            // Reflect off right wall
            x = 160 - fmod(x, 160);
        }

        if (x < 160/2) {
            // The ball will land on the left (human) side, so move to the center of the right side
            x = 160/2 + 160/4;
        } else if (fabs(ball->velX) < 2) {
            // Nudge right a bit so that the AI hits the ball to the left
            x += PLAYER_RADIUS/8;
        }
        ball->cpuTargetX = x;
    }
}

void updatePlayer (Player* player, uint8_t gamepad, bool rightSide) {
    if (gamepad & BUTTON_LEFT) {
        player->velX = -PLAYER_WALK_VELOCITY;
    } else if (gamepad & BUTTON_RIGHT) {
        player->velX = PLAYER_WALK_VELOCITY;
    } else {
        player->velX = 0;
    }

    player->x += player->velX;
    player->y += player->velY;

    // Left/right wall collision
    float minX = rightSide ? 160/2 + PLAYER_RADIUS : PLAYER_RADIUS;
    float maxX = rightSide ? 160-PLAYER_RADIUS : 160/2-PLAYER_RADIUS;
    if (player->x < minX) {
        player->x = minX;
    } else if (player->x > maxX) {
        player->x = maxX;
    }

    if (player->y < 160-GROUND_HEIGHT-PLAYER_RADIUS) {
        // In the air
        player->velY += PLAYER_GRAVITY;
    } else {
        // On the ground
        if (gamepad & (BUTTON_1 | BUTTON_2)) {
            player->velY = -PLAYER_JUMP_VELOCITY;
        } else {
            player->y = 160-GROUND_HEIGHT-PLAYER_RADIUS;
            player->velY = 0;
        }
    }
}

void updateAIPlayer (Player* player) {
    uint8_t gamepad = 0;

    // Move towards the target position
    float d = ball.cpuTargetX - player->x;
    if (d > 1) {
        gamepad |= BUTTON_RIGHT;
    } else if (d < -1) {
        gamepad |= BUTTON_LEFT;
    }

    // If the ball is on our side, within height, and not moving too fast, jump
    if (ball.x > 160/2 && ball.y > 160-GROUND_HEIGHT-5*PLAYER_RADIUS && fabs(ball.velX) < 1 && ball.velY < 2) {
        gamepad |= BUTTON_1;
    }

    updatePlayer(player, gamepad, true);
}

void drawPlayer (const Player* player, int playerIdx) {
    bool alternateColor = playerIdx > 1;
    bool rightSide = playerIdx & 1;

    // Draw body
    *DRAW_COLORS = alternateColor ? 0x1230 : 0x1340;
    blit(imgPlayer, player->x-PLAYER_RADIUS, player->y-PLAYER_RADIUS,
        imgPlayerWidth, imgPlayerHeight, imgPlayerFlags | (rightSide ? BLIT_FLIP_X : 0));

    int eyeX = player->x + (rightSide ? -4 : 3);
    int eyeY = player->y - 4;

    int eyeRadius = 4;
    int pupilRadius = 2;

    float dx = ball.x - player->x;
    float dy = ball.y - player->y;
    float dist = sqrt(dx*dx + dy*dy);

    // Draw pupil
    *DRAW_COLORS = 0x40;
    blit(imgCircle4x4,
        eyeX + (eyeRadius-pupilRadius)*dx/dist - pupilRadius + 1,
        eyeY + (eyeRadius-pupilRadius)*dy/dist - pupilRadius + 1,
        imgCircle4x4Width, imgCircle4x4Height, imgCircle4x4Flags);
}

void drawBall (const Ball* ball) {
    *DRAW_COLORS = 0x4230;
    blit(imgBall, ball->x-BALL_RADIUS, ball->y-BALL_RADIUS, imgBallWidth, imgBallHeight, imgBallFlags);
}

void drawNet () {
    rect(160/2 - NET_WIDTH/2, 160 - NET_HEIGHT - GROUND_HEIGHT, NET_WIDTH, NET_HEIGHT);
}

void drawGround () {
    rect(0, 160-GROUND_HEIGHT, 160, GROUND_HEIGHT);
}

void drawHUD () {
    // Draw filled balls
    *DRAW_COLORS = 0x231;
    for (int ii = 0; ii < scoreLeft; ++ii) {
        blit(imgBall, 1 + ii*(imgBallWidth+1), 1, imgBallWidth, imgBallHeight, imgBallFlags);
    }
    for (int ii = 0; ii < scoreRight; ++ii) {
        blit(imgBall, 160 - (ii+1)*(imgBallWidth+1), 1, imgBallWidth, imgBallHeight, imgBallFlags);
    }

    // Draw outlined balls
    *DRAW_COLORS = 0x20;
    for (int ii = scoreLeft; ii < SCORE_LIMIT; ++ii) {
        blit(imgBall, 1 + ii*(imgBallWidth+1), 1, imgBallWidth, imgBallHeight, imgBallFlags);
    }
    for (int ii = scoreRight; ii < SCORE_LIMIT; ++ii) {
        blit(imgBall, 160 - (ii+1)*(imgBallWidth+1), 1, imgBallWidth, imgBallHeight, imgBallFlags);
    }
}

void updateTitle () {
    uint8_t gamepad = *GAMEPAD1;
    char pressedThisFrame = gamepad & (gamepad ^ lastGamepadState);
    lastGamepadState = gamepad;

    if (pressedThisFrame & BUTTON_UP && gameMode > 0) {
        --gameMode;
    }
    if (pressedThisFrame & BUTTON_DOWN && gameMode < 2) {
        ++gameMode;
    }
    if (pressedThisFrame & (BUTTON_1 | BUTTON_2)) {
        state = STATE_PLAYING;
        playerCount = (gameMode == 2) ? 4 : 2;
        *((uint8_t*)GAMEPAD1) = 0; // Hack to prevent jumping on game start
        resetGame();
    }
}

void drawTitle () {
    Player player = {
        .x = 40,
        .y = 56,
    };
    drawPlayer(&player, 2);

    *DRAW_COLORS = 4;
    text("LIME VOLLEYBALL", 24, 72);
    int menuX = 58;
    int menuY = 100;

    *DRAW_COLORS = 0x40;
    blit(imgArrow, menuX-imgArrowWidth-4, menuY+gameMode*8, imgArrowWidth, imgArrowHeight, imgArrowFlags);

    *DRAW_COLORS = 0x03;
    text("1 vs CPU", menuX, menuY);
    text("1 vs 1", menuX, menuY+8);
    text("2 vs 2", menuX, menuY+16);
}

void update () {
    switch (state) {
    case STATE_TITLE:
        updateTitle();
        drawTitle();
        break;

    case STATE_PLAYING:
        if (pauseTime > 1) {
            --pauseTime;

            if (scoreLeft >= SCORE_LIMIT || scoreRight >= SCORE_LIMIT) {
                *DRAW_COLORS = 0x4;
                text("GAME!", 160/2-20, 160/2-4);
            }

        } else {
            if (pauseTime) {
                pauseTime = 0;
                if (scoreLeft >= SCORE_LIMIT || scoreRight >= SCORE_LIMIT) {
                    resetGame();
                } else {
                    resetRally();
                }
            }

            if (gameMode == 0) {
                updatePlayer(&players[0], *GAMEPAD1, false);
                updateAIPlayer(&players[1]);

                // *DRAW_COLORS = 4;
                // rect(ball.cpuTargetX, 160-30, 4, 4);
            } else {
                for (int ii = 0; ii < playerCount; ++ii) {
                    updatePlayer(&players[ii], GAMEPAD1[ii], ii & 1);
                }
            }

            updateBall(&ball);
        }

        *DRAW_COLORS = 2;
        drawNet();
        drawGround();
        drawHUD();

        for (int ii = playerCount-1; ii >= 0; --ii) {
            drawPlayer(&players[ii], ii);
        }
        drawBall(&ball);

        break;
    }
}

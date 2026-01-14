#include "PmodJSTK2.h"
#include "sleep.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include <stdio.h>
#include "PmodOLED.h"
#include "xparameters.h"

#define CPU_CLOCK_FREQ_HZ (XPAR_PS7_CORTEXA9_0_CPU_CLK_FREQ_HZ)

PmodJSTK2 joystick;
PmodOLED myDevice;

void DemoInitialize();
void DemoRun();

#define OLED_WIDTH 128
#define OLED_HEIGHT 32

#define SHIP_HEIGHT 4
#define SHIP_WIDTH  4


#define SHIP_STEP_Y      1     // pixels per move
#define JOY_DEADZONE     20    // ignore small movement near center
#define JOY_CENTER       128
#define LOOP_DELAY_US    15000 // controls repeat rate

typedef struct {
    int x, y;
} Spaceship;

Spaceship ship;

// --- Function prototypes you use later ---
void drawSpaceship(Spaceship *s);
void updateShipFromJoystickY(Spaceship *s, JSTK2_Position pos);

#define MAX_BULLETS     8
#define BULLET_W        2
#define BULLET_H        1
#define BULLET_SPEED    2   // pixels per tick

typedef struct {
    int x, y;
    int active;
} Bullet;

Bullet bullets[MAX_BULLETS];

void initBullets();
void spawnBulletFromShip(const Spaceship *s);
void updateBullets();
void drawBullets();

#define ENEMY_W 4
#define ENEMY_H 3
#define MAX_ENEMIES 5

#define WAVE_MIN 3
#define WAVE_MAX 6
#define WAVE_SPAWN_GAP_TICKS 4

#define ENEMY_BASE_SPEED 1      // starting x speed
#define ENEMY_SPEED_STEP 1      // increase amount
#define WAVES_PER_SPEEDUP 5     // every 5 cleared waves speed up

#define WAVE_Y_STEP 1           // how fast wave moves up/down
#define WAVE_Y_AMPLITUDE 6      // max up/down offset from base (pixels)

#define ENEMY_DEATH_FRAMES 10
#define ENEMY_DEATH_PAT_COUNT 8
typedef struct {
    int baseY;
    int active;
    int tickDelay;

    int dying;        // 0/1
    int deathTicks;   // frames remaining
    int deathPat;     // which pattern to show
} Enemy;
Enemy enemies[MAX_ENEMIES];
int waveAlive = 0;

// shared wave movement
int waveX = 0;
int waveSpeedX = ENEMY_BASE_SPEED;

// shared vertical oscillation
int waveYOffset = 0;
int waveYDir = 1;   // +1 down, -1 up

// difficulty scaling
int wavesCleared = 0;
int speedupCounter = 0;
int waveXTickCounter = 0;
int waveXTickDiv = 4;
int waveYTickCounter = 0;
int waveYTickDiv = 2;

void initEnemies();
void spawnWave();
void updateEnemies();
void drawEnemies();
int  rectsOverlap(int ax0,int ay0,int aw,int ah,int bx0,int by0,int bw,int bh);
void handleBulletEnemyCollisions();

#define MAX_LIVES 3

int lives = MAX_LIVES;

// Heart sprite size (sideways)
#define HEART_W 5
#define HEART_H 4

#define HEART_START_X 0
#define HEART_START_Y 2
#define HEART_SPACING 6

void resetGame();
void gameOverLoop();

static const u8 heartBitmap[HEART_H][HEART_W] = {
    {0,1,1,0,0},
    {1,1,1,1,0},
    {1,1,1,1,0},
    {0,1,1,0,0}
};


const u8 orientation = 0x0; // Normal PmodOLED
const u8 invert      = 0x0; // black bg / white pixels
static unsigned int rng_state = 1;
int score = 0;

int main() {
    DemoInitialize();
    DemoRun();
    return 0;
}

void DemoInitialize() {

    JSTK2_begin(
        &joystick,
        XPAR_PMODJSTK2_0_AXI_LITE_SPI_BASEADDR,
        XPAR_PMODJSTK2_0_AXI_LITE_GPIO_BASEADDR
    );

    OLED_Begin(&myDevice,
        XPAR_PMODOLED_0_AXI_LITE_GPIO_BASEADDR,
        XPAR_PMODOLED_0_AXI_LITE_SPI_BASEADDR,
        orientation, invert
    );

    // Invert only Y axis (if movement direction feels wrong, change to 0,0)
    JSTK2_setInversion(&joystick, 0, 1);

    ship.x = OLED_WIDTH - SHIP_WIDTH - 1;   // right margin
    ship.y = (OLED_HEIGHT / 2) - (SHIP_HEIGHT / 2);

    JSTK2_Position p = JSTK2_getPosition(&joystick);
    rng_state = (p.XData << 16) ^ (p.YData << 8) ^ 0xA5U;
    initBullets();
    initEnemies();
    spawnWave();   // start with first group
}

void DemoRun() {
    JSTK2_Position position;
    JSTK2_DataPacket rawdata;
    int prevTrigger = 0;
    OLED_SetCharUpdate(&myDevice, 0);

    xil_printf("UART and SPI opened for PmodOLED Demo\n\r");
    xil_printf("\r\nJoystick Demo\r\n");

    while (1) {
        position = JSTK2_getPosition(&joystick);
        rawdata  = JSTK2_getDataPacket(&joystick);
        u8 *pat;
        score += 1;
        int triggerNow = (rawdata.Trigger != 0);
        // fire only on rising edge (not continuously while held)
        if (triggerNow && !prevTrigger) {
            spawnBulletFromShip(&ship);
        }
        prevTrigger = triggerNow;

          	// Choose fill pattern
        pat = OLED_GetStdPattern(1);
        OLED_SetFillPattern(&myDevice, pat);
        // Map joystick Y into 0..(OLED_HEIGHT - SHIP_HEIGHT)
        xil_printf(
             	"X:%d\tY:%d%s%s\r\n",
             	position.XData,
             	position.YData,
             	(rawdata.Jstk != 0) ? "\tJoystick pressed" : "",
             	(rawdata.Trigger != 0) ? "\tTrigger pressed" : ""
        );
        updateShipFromJoystickY(&ship, position);
        updateBullets();
        updateEnemies();
        handleBulletEnemyCollisions();
        // Clear OLED buffer
        OLED_ClearBuffer(&myDevice);

        // Draw
        drawLives();
        drawSpaceship(&ship);
        drawBullets();
        drawEnemies();
        // Push buffer to display
        OLED_Update(&myDevice);

        // LED feedback (green when pressed, else show axis color)
        if (rawdata.Trigger != 0) {
            JSTK2_setLedRGB(&joystick, 0, 255, 0);
        } else {
            JSTK2_setLedRGB(&joystick, position.XData, 0, position.YData);
        }

        usleep(LOOP_DELAY_US); // ~10ms, smoother than 1ms
    }
}

// Scales joystick Y (0..255) to screen Y (0..OLED_HEIGHT-SHIP_HEIGHT)
void updateShipFromJoystickY(Spaceship *s, JSTK2_Position pos) {
    int yMax = OLED_HEIGHT - SHIP_HEIGHT;

    int dy = 0;

    if (pos.XData > (JOY_CENTER + JOY_DEADZONE)) {
        dy = -SHIP_STEP_Y;
    }
    else if (pos.XData < (JOY_CENTER - JOY_DEADZONE)) {
        dy = +SHIP_STEP_Y;
    }

    s->y += dy;

    // clamp
    if (s->y < 0) s->y = 0;
    if (s->y > yMax) s->y = yMax;
}

// Draw a filled 4x4 block (most compatible approach)
void drawSpaceship(Spaceship *s) {
    int x0 = s->x;
    int y0 = s->y;

    // bottom-right corner (inclusive)
    int x1 = x0 + (SHIP_WIDTH  - 1);
    int y1 = y0 + (SHIP_HEIGHT - 1);

    // clamp to screen bounds
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= OLED_WIDTH)  x1 = OLED_WIDTH - 1;
    if (y1 >= OLED_HEIGHT) y1 = OLED_HEIGHT - 1;

    OLED_MoveTo(&myDevice, x0, y0 + 1);
    OLED_FillRect(&myDevice, x1 - 2, y1 -1);
    OLED_MoveTo(&myDevice, x0+2, y0);
    OLED_FillRect(&myDevice, x1, y1);
}
void initBullets() {
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullets[i].active = 0;
        bullets[i].x = 0;
        bullets[i].y = 0;
    }
}
void spawnBulletFromShip(const Spaceship *s) {
    // find an inactive slot
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            bullets[i].active = 1;

            // spawn at ship nose (left side of ship)
            bullets[i].x = s->x - BULLET_W - 1;

            // spawn roughly centered vertically on ship
            bullets[i].y = s->y + (SHIP_HEIGHT / 2);

            // clamp y
            if (bullets[i].y < 0) bullets[i].y = 0;
            if (bullets[i].y >= OLED_HEIGHT) bullets[i].y = OLED_HEIGHT - 1;

            return;
        }
    }
    // no free slot: do nothing (pool full)
}
void updateBullets() {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) {
            bullets[i].x -= BULLET_SPEED;

            // if bullet fully left of screen, deactivate
            if (bullets[i].x + BULLET_W - 1 < 0) {
                bullets[i].active = 0;
            }
        }
    }
}
void drawBullets() {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) {
            int x0 = bullets[i].x;
            int y0 = bullets[i].y;
            int x1 = x0 + (BULLET_W - 1);
            int y1 = y0 + (BULLET_H - 1);

            // basic bounds check/clamp
            if (y0 < 0 || y0 >= OLED_HEIGHT) continue;
            if (x1 < 0 || x0 >= OLED_WIDTH) continue;

            if (x0 < 0) x0 = 0;
            if (x1 >= OLED_WIDTH) x1 = OLED_WIDTH - 1;

            OLED_MoveTo(&myDevice, x0, y0);
            OLED_FillRect(&myDevice, x1, y1);
        }
    }
}
static unsigned int rng32() {
    // simple LCG
    rng_state = (1103515245U * rng_state + 12345U);
    return rng_state;
}

static int randRange(int lo, int hi) {
    // inclusive range
    unsigned int r = rng32();
    int span = hi - lo + 1;
    return lo + (int)(r % (unsigned int)span);
}
void initEnemies() {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = 0;
        enemies[i].baseY = 0;
        enemies[i].tickDelay = 0;
        enemies[i].dying = 0;
        enemies[i].deathTicks = 0;
        enemies[i].deathPat = 0;
    }

    waveAlive = 0;

    waveX = 0;
    waveSpeedX = ENEMY_BASE_SPEED;

    waveYOffset = 0;
    waveYDir = 1;

    wavesCleared = 0;
}
void spawnWave() {
    for (int i = 0; i < MAX_ENEMIES; i++) {
    	enemies[i].active = 0;
    	enemies[i].dying = 0;
    	enemies[i].deathTicks = 0;
    	enemies[i].deathPat = 0;
    }
    int count = randRange(WAVE_MIN, WAVE_MAX);

    // start just off-screen left
    waveX = -randRange(1, 12);

    // reset vertical oscillation each wave (optional)
    waveYOffset = randRange(-WAVE_Y_AMPLITUDE, WAVE_Y_AMPLITUDE);
    waveYDir = (randRange(0, 1) == 0) ? 1 : -1;

    int yMaxBase = OLED_HEIGHT - ENEMY_H;
    if (yMaxBase < 0) yMaxBase = 0;

    int activated = 0;
    for (int i = 0; i < MAX_ENEMIES && activated < count; i++) {
        enemies[i].active = 1;

        // random base Y
        enemies[i].baseY = randRange(0, yMaxBase);

        // stagger entry so it looks like a group spawning in
        enemies[i].tickDelay = activated * WAVE_SPAWN_GAP_TICKS;

        activated++;
    }

    waveAlive = activated;
}
void clearWaveAndCount() {
    // wave cleared
    wavesCleared++;

    // every 5 waves cleared, increase X speed a bit
    if ((wavesCleared % WAVES_PER_SPEEDUP) == 0) {
        if (waveXTickDiv > 1) {
            waveXTickDiv--;           // faster updates (still 1px moves)
        } else {
            waveSpeedX += ENEMY_SPEED_STEP;  // once div hits 1, increase px/tick
            if (waveSpeedX > 6) waveSpeedX = 6;
        }
    }
    if ((wavesCleared % WAVES_PER_SPEEDUP) == 0 && waveYTickDiv > 1) {
        waveYTickDiv--;
    }
    waveAlive = 0;
}

void updateEnemies() {
    if (waveAlive <= 0) {
        spawnWave();
        return;
    }

    // move wave right
    waveXTickCounter++;
    if (waveXTickCounter >= waveXTickDiv) {
        waveX += waveSpeedX;
        waveXTickCounter = 0;
    }

    waveYTickCounter++;
    if (waveYTickCounter >= waveYTickDiv) {
        waveYOffset += waveYDir * WAVE_Y_STEP;

        if (waveYOffset >= WAVE_Y_AMPLITUDE) {
            waveYOffset = WAVE_Y_AMPLITUDE;
            waveYDir = -1;
        } else if (waveYOffset <= -WAVE_Y_AMPLITUDE) {
            waveYOffset = -WAVE_Y_AMPLITUDE;
            waveYDir = 1;
        }

        waveYTickCounter = 0;
    }


    // tick down delays
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        if (enemies[i].tickDelay > 0) enemies[i].tickDelay--;
        if (enemies[i].dying) {
            enemies[i].deathTicks--;
            enemies[i].deathPat = (enemies[i].deathPat + 1) % ENEMY_DEATH_PAT_COUNT;

            if (enemies[i].deathTicks <= 0) {
                enemies[i].active = 0;
                enemies[i].dying = 0;
                waveAlive--;               // waveAlive decreases ONLY when animation finishes
            }
        }
    }

    if (waveX > OLED_WIDTH) {
        for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = 0;

        // lose a life
        if (lives > 0) lives--;
        if (lives <= 0) {
            gameOverLoop();
            return;
        }
        clearWaveAndCount();
        return;
    }

    if (waveAlive <= 0) {
        clearWaveAndCount();
        return;
    }

}
void drawEnemies() {
    // default pattern for normal drawing
    u8 *defaultPat = OLED_GetStdPattern(1);

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        if (enemies[i].tickDelay > 0) continue;

        int x0 = waveX;
        int y0 = enemies[i].baseY + waveYOffset;

        if (y0 < 0) y0 = 0;
        if (y0 > OLED_HEIGHT - ENEMY_H) y0 = OLED_HEIGHT - ENEMY_H;

        int x1 = x0 + (ENEMY_W - 1);
        int y1 = y0 + (ENEMY_H - 1);

        if (x1 < 0 || x0 >= OLED_WIDTH) continue;
        if (x0 < 0) x0 = 0;
        if (x1 >= OLED_WIDTH) x1 = OLED_WIDTH - 1;

        // choose pattern
        if (enemies[i].dying) {
            u8 *pat = OLED_GetStdPattern(enemies[i].deathPat);
            OLED_SetFillPattern(&myDevice, pat);
        } else {
            OLED_SetFillPattern(&myDevice, defaultPat);
        }

        OLED_MoveTo(&myDevice, x0, y0);
        OLED_FillRect(&myDevice, x1, y1);
    }

    // restore default so other drawing stays consistent
    OLED_SetFillPattern(&myDevice, defaultPat);
}
int rectsOverlap(int ax0,int ay0,int aw,int ah,int bx0,int by0,int bw,int bh) {
    int ax1 = ax0 + aw - 1;
    int ay1 = ay0 + ah - 1;
    int bx1 = bx0 + bw - 1;
    int by1 = by0 + bh - 1;

    if (ax1 < bx0) return 0;
    if (bx1 < ax0) return 0;
    if (ay1 < by0) return 0;
    if (by1 < ay0) return 0;
    return 1;
}

void handleBulletEnemyCollisions() {
    for (int b = 0; b < MAX_BULLETS; b++) {
        if (!bullets[b].active) continue;

        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!enemies[e].active) continue;
            if (enemies[e].tickDelay > 0) continue;
            if (enemies[e].dying) continue;

            int ex = waveX;
            int ey = enemies[e].baseY + waveYOffset;

            if (ey < 0) ey = 0;
            if (ey > OLED_HEIGHT - ENEMY_H) ey = OLED_HEIGHT - ENEMY_H;

            if (rectsOverlap(bullets[b].x, bullets[b].y, BULLET_W, BULLET_H,
                             ex, ey, ENEMY_W, ENEMY_H)) {
            	score += 500;
                bullets[b].active = 0;
                // start death animation (do NOT remove yet)
                enemies[e].dying = 1;
                enemies[e].deathTicks = ENEMY_DEATH_FRAMES;
                enemies[e].deathPat = 0;

                if (waveAlive <= 0) {
                    clearWaveAndCount();
                }

                break;
            }
        }
    }
}

void drawHeart(int x, int y) {
    for (int r = 0; r < HEART_H; r++) {
        for (int c = 0; c < HEART_W; c++) {
            if (heartBitmap[r][c]) {
            	OLED_MoveTo(&myDevice, x + c, y + r + 1);
                OLED_DrawPixel(&myDevice);
            }
        }
    }
}
void drawLives() {
    for (int i = 0; i < lives; i++) {
        int y = HEART_START_Y + i * HEART_SPACING;
        drawHeart(HEART_START_X, y);
    }
}
void resetGame() {
    // reset ship
    ship.x = OLED_WIDTH - SHIP_WIDTH - 1;
    ship.y = (OLED_HEIGHT / 2) - (SHIP_HEIGHT / 2);

    // reset lives
    lives = MAX_LIVES;

    //reset score
    score = 0;

    // reset bullets/enemies/waves
    initBullets();
    initEnemies();

    // reset wave pacing
    waveXTickCounter = 0;
    waveYTickCounter = 0;
    waveXTickDiv = 2;   // whatever you want as starting slow rate
    waveYTickDiv = 2;

    spawnWave();
}
void gameOverLoop() {
    char buf[32];
    sleep(1);
    while (1) {
        OLED_ClearBuffer(&myDevice);

        OLED_SetCursor(&myDevice, 3, 1);
        OLED_PutString(&myDevice, "GAME OVER");

        // Score line
        snprintf(buf, sizeof(buf), "SCORE: %d", score);
        OLED_SetCursor(&myDevice, 2, 2);
        OLED_PutString(&myDevice, buf);

        OLED_SetCursor(&myDevice, 0, 3);
        OLED_PutString(&myDevice, "Press Trigger");
        OLED_Update(&myDevice);

        JSTK2_DataPacket raw = JSTK2_getDataPacket(&joystick);
        if (raw.Trigger != 0 || raw.Jstk != 0) {
            resetGame();
            return;
        }

        usleep(50000);
    }
}

void deathTransition() {
    // Cycle through a few patterns a couple times
    for (int pass = 0; pass < 2; pass++) {
        for (int p = 0; p < 8; p++) {   // if 8 is too many, try 4
            u8 *pat = OLED_GetStdPattern(p);
            OLED_SetFillPattern(&myDevice, pat);

            OLED_ClearBuffer(&myDevice);

            // draw current scene using the pattern (ship/enemies/bullets)
            drawSpaceship(&ship);
            drawBullets();
            drawEnemies();
            drawLives();

            OLED_Update(&myDevice);
            usleep(60000);
        }
    }

    // Optional: quick flash inverse-looking effect
    for (int i = 0; i < 3; i++) {
        u8 *pat = OLED_GetStdPattern(0);
        OLED_SetFillPattern(&myDevice, pat);
        OLED_ClearBuffer(&myDevice);
        OLED_Update(&myDevice);
        usleep(40000);

        pat = OLED_GetStdPattern(1);
        OLED_SetFillPattern(&myDevice, pat);
        OLED_ClearBuffer(&myDevice);
        OLED_Update(&myDevice);
        usleep(40000);
    }
}

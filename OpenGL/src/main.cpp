/*
 * PAC-MAN — OpenGL / GLUT (C++)
 * FIXED VERSION:
 *   1. Pac-Man is yellow
 *   2. Power pellet no longer makes Pac-Man disappear
 *   3. Game gradually speeds up as pellets are eaten
 *   4. Ghost spawns inside ghost house, bobs, then exits and roams
 *
 * Compile:
 *   Linux:   g++ pacman_fixed.cpp -o pacman -lGL -lGLU -lglut -lm
 *   macOS:   g++ pacman_fixed.cpp -o pacman -framework OpenGL -framework GLUT -Wno-deprecated
 *   Windows: g++ pacman_fixed.cpp -o pacman -lfreeglut -lopengl32 -lglu32
 *
 * Controls: Arrow Keys OR WASD
 *           SPACE to start/restart
 *           ESC to exit
 */


#include <windows.h>
#include <GL/glut.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <string>

// ─── Constants ───────────────────────────────────────────────────────────────
static const int COLS       = 28;
static const int ROWS       = 31;
static const int CELL       = 20;
static const int WIN_W      = COLS * CELL;
static const int WIN_H      = ROWS * CELL;
static const float PI       = 3.14159265f;

// ─── Maze definition ─────────────────────────────────────────────────────────
static const char* MAP[ROWS] = {
    "############################",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#o####.#####.##.#####.####o#",
    "#.####.#####.##.#####.####.#",
    "#..........................#",
    "#.####.##.########.##.####.#",
    "#.####.##.########.##.####.#",
    "#......##....##....##......#",
    "######.#####.##.#####.######",
    "######.#####.##.#####.######",
    "######.##..........##.######",
    "######.##.###--###.##.######",
    "######.##.#      #.##.######",
    "      .  .#      #.  .      ",
    "######.##.#      #.##.######",
    "######.##.########.##.######",
    "######.##..........##.######",
    "######.##.########.##.######",
    "######.##.########.##.######",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#.####.#####.##.#####.####.#",
    "#o..##................##..o#",
    "###.##.##.########.##.##.###",
    "###.##.##.########.##.##.###",
    "#......##....##....##......#",
    "#.##########.##.##########.#",
    "#.##########.##.##########.#",
    "#..........................#",
    "############################",
};

// ─── Types ───────────────────────────────────────────────────────────────────
struct Color { float r, g, b; };

enum GameState { STATE_IDLE, STATE_PLAYING, STATE_DEAD, STATE_WIN };

// Ghost lifecycle: bob inside house → move up through door → roam freely
enum GhostMode { GHOST_IN_HOUSE, GHOST_EXITING, GHOST_ROAMING };

struct Pellet {
    int c, r;
    bool eaten;
    bool power;
};

struct Ghost {
    float x, y;
    float dx, dy;
    Color color;
    GhostMode mode;       // current lifecycle stage
    int exitTimer;        // countdown before ghost starts trying to exit
    bool isFrightened;
    int frightTimer;
    bool isDead;
    int deadTimer;
};

// ─── Global state ────────────────────────────────────────────────────────────
static GameState gState   = STATE_IDLE;
static int       gScore   = 0;
static int       gHiScore = 0;
static int       gLives   = 3;
static int       gFrame   = 0;

static float     pacX, pacY;
static float     pacDX, pacDY;
static float     pacNextDX, pacNextDY;
static float     pacLastDX, pacLastDY;

// --- FIX 3: Speed variables (start slow, ramp up) ---
static float     PAC_SPEED        = 2.0f;
static float     GHOST_SPEED_CUR  = 1.4f;
static const float PAC_SPEED_MAX  = 3.6f;
static const float GHOST_SPEED_MAX= 2.8f;
static int       gPelletsEaten    = 0;     // track how many pellets eaten for speed scaling

static const float FRIGHT_SPEED   = 1.0f;

// --- FIX 2: Mouth angle clamped so it never exceeds PI ---
static float     mouthAngle = 0.25f;
static float     mouthDir   = 1.0f;
static const float MOUTH_MAX = 0.38f;   // well below PI — keeps the arc sane
static const float MOUTH_MIN = 0.02f;

static bool      globalFrightened = false;
static int       globalFrightTimer = 0;

static std::vector<Pellet> gPellets;
static Ghost gGhost;

// ─── Helper Functions ────────────────────────────────────────────────────────
static bool isWall(int c, int r) {
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return true;
    char ch = MAP[r][c];
    return ch == '#';
}

// Ghost can pass through the '-' door tiles only while exiting
static bool isWallForGhost(int c, int r, bool allowDoor) {
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return true;
    char ch = MAP[r][c];
    if (ch == '#') return true;
    if (ch == '-') return !allowDoor;
    return false;
}

static bool isWallAt(float x, float y) {
    int c = (int)floor(x / CELL);
    int r = (int)floor(y / CELL);
    return isWall(c, r);
}

static void setColor(float r, float g, float b) { glColor3f(r, g, b); }

// ─── Drawing primitives ──────────────────────────────────────────────────────
static void drawRect(float x, float y, float w, float h) {
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static void drawCircle(float cx, float cy, float radius, int segs = 24) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= segs; i++) {
        float a = i * 2.0f * PI / segs;
        glVertex2f(cx + cosf(a) * radius, cy + sinf(a) * radius);
    }
    glEnd();
}

// FIX 2: drawPacman now clamps mouth so the arc never wraps around.
// The original code used mouthAngle as a fraction of PI (e.g. 0.25 * PI).
// When mouthAngle itself grew past 1.0 the start/end angles crossed and the
// fan flipped, making Pac-Man invisible.  We now multiply by PI *inside*
// the function so the passed value is always a small radian offset.
static void drawPacman(float cx, float cy, float r, float mouth, float facing) {
    // mouth is in [MOUTH_MIN, MOUTH_MAX] — scale to a real angle
    float mRad = mouth * PI;           // e.g. 0.25 * PI ≈ 45°
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    int segs = 32;
    float start = mRad + facing;
    float end   = 2.0f * PI - mRad + facing;
    for (int i = 0; i <= segs; i++) {
        float a = start + i * (end - start) / segs;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();
}

static void drawGhost(float cx, float cy, float r, Color col, bool frightened, bool dead) {
    if (dead) {
        setColor(0.5f, 0.5f, 0.5f);
        drawCircle(cx, cy, r * 0.8f, 16);
        setColor(1, 1, 1);
        drawCircle(cx - r * 0.25f, cy - r * 0.1f, r * 0.2f);
        drawCircle(cx + r * 0.25f, cy - r * 0.1f, r * 0.2f);
        return;
    }

    if (frightened) {
        bool flash = (gFrame / 10) % 2 == 0;
        if (flash) setColor(1, 1, 1);
        else setColor(0.3f, 0.3f, 1.0f);
    } else {
        setColor(col.r, col.g, col.b);
    }

    // Body (semi-circle)
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= 20; i++) {
        float a = PI + i * PI / 20.0f;
        glVertex2f(cx + cosf(a) * r, cy + sinf(a) * r);
    }
    glEnd();

    // Bottom rectangle
    drawRect(cx - r, cy, r * 2, r);

    // Wavy bottom
    for (int i = -2; i <= 2; i++) {
        float wx = cx + i * r * 0.4f;
        glBegin(GL_TRIANGLES);
        glVertex2f(wx - r * 0.2f, cy + r);
        glVertex2f(wx, cy + r + r * 0.3f);
        glVertex2f(wx + r * 0.2f, cy + r);
        glEnd();
    }

    // Eyes
    setColor(1, 1, 1);
    drawCircle(cx - r * 0.35f, cy - r * 0.15f, r * 0.25f);
    drawCircle(cx + r * 0.35f, cy - r * 0.15f, r * 0.25f);

    if (!frightened) {
        setColor(0, 0, 0.8f);
        drawCircle(cx - r * 0.25f, cy - r * 0.1f, r * 0.12f);
        drawCircle(cx + r * 0.25f, cy - r * 0.1f, r * 0.12f);
    } else {
        setColor(0, 0, 0);
        drawCircle(cx - r * 0.35f, cy - r * 0.05f, r * 0.08f);
        drawCircle(cx + r * 0.35f, cy - r * 0.05f, r * 0.08f);
    }
}

static void drawText(float x, float y, const char* str, void* font = GLUT_BITMAP_8_BY_13) {
    glRasterPos2f(x, y);
    for (const char* c = str; *c; c++)
        glutBitmapCharacter(font, *c);
}

// ─── Maze rendering ──────────────────────────────────────────────────────────
static void renderMaze() {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            char ch = MAP[r][c];
            float x = c * CELL, y = r * CELL;
            if (ch == '#') {
                setColor(0.05f, 0.1f, 0.6f);
                drawRect(x, y, CELL, CELL);
                setColor(0.0f, 0.05f, 0.4f);
                drawRect(x + 1, y + 1, CELL - 2, CELL - 2);
            } else if (ch == '-') {
                setColor(0.8f, 0.4f, 0.8f);
                drawRect(x, y + CELL/2 - 2, CELL, 4);
            }
        }
    }
}

static void renderPellets() {
    for (const Pellet& p : gPellets) {
        if (p.eaten) continue;
        float x = p.c * CELL + CELL/2;
        float y = p.r * CELL + CELL/2;
        if (p.power) {
            float pulse = 0.6f + 0.4f * sinf(gFrame * 0.05f);
            setColor(1, 1, 0.8f);
            drawCircle(x, y, 6.0f * pulse, 12);
        } else {
            setColor(1, 0.8f, 0.6f);
            drawCircle(x, y, 2.5f, 8);
        }
    }
}

static void renderHUD() {
    char buf[64];
    // FIX 3: show current speed level in HUD
    setColor(1, 1, 0);
    snprintf(buf, sizeof(buf), "SCORE: %d", gScore);
    drawText(5, WIN_H - 15, buf);

    snprintf(buf, sizeof(buf), "HI: %d", gHiScore);
    drawText(WIN_W - 70, WIN_H - 15, buf);

    // Speed indicator
    int speedLevel = (int)((PAC_SPEED - 2.0f) / (PAC_SPEED_MAX - 2.0f) * 5.0f) + 1;
    speedLevel = (speedLevel < 1) ? 1 : (speedLevel > 5 ? 5 : speedLevel);
    snprintf(buf, sizeof(buf), "SPD:%d", speedLevel);
    setColor(0.6f, 1.0f, 0.6f);
    drawText(WIN_W/2 + 40, WIN_H - 15, buf);

    // Lives display (FIX 1: yellow Pac-Man icons in HUD too)
    setColor(1, 1, 0);
    for (int i = 0; i < gLives && i < 5; i++) {
        drawCircle(WIN_W/2 - 30 + i * 18, WIN_H - 8, 5);
    }
}

static void renderOverlay(const char* line1, const char* line2) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0, 0, 0, 0.7f);
    drawRect(WIN_W/2 - 120, WIN_H/2 - 35, 240, 70);
    glDisable(GL_BLEND);

    setColor(1, 1, 0);
    drawText(WIN_W/2 - strlen(line1)*4, WIN_H/2 + 10, line1, GLUT_BITMAP_9_BY_15);
    setColor(0.8f, 0.8f, 0.8f);
    drawText(WIN_W/2 - strlen(line2)*3.5, WIN_H/2 - 15, line2, GLUT_BITMAP_8_BY_13);
}

// ─── Speed scaling (FIX 3) ───────────────────────────────────────────────────
static void updateSpeed() {
    // Count total pellets for reference
    int totalPellets = (int)gPellets.size();
    if (totalPellets == 0) return;
    float progress = (float)gPelletsEaten / (float)totalPellets;  // 0..1
    PAC_SPEED       = 2.0f + progress * (PAC_SPEED_MAX  - 2.0f);
    GHOST_SPEED_CUR = 1.4f + progress * (GHOST_SPEED_MAX - 1.4f);
}

// ─── Game Initialization ─────────────────────────────────────────────────────
static void initGame() {
    // Pac-Man start position
    pacX = 14 * CELL + CELL/2;
    pacY = 23 * CELL + CELL/2;
    pacDX = 1.0f;
    pacDY = 0.0f;
    pacNextDX = 0;
    pacNextDY = 0;
    pacLastDX = 1.0f;
    pacLastDY = 0.0f;
    mouthAngle = 0.25f;
    mouthDir = 1.0f;

    // Reset speeds
    PAC_SPEED        = 2.0f;
    GHOST_SPEED_CUR  = 1.4f;
    gPelletsEaten    = 0;

    // Global fright
    globalFrightened = false;
    globalFrightTimer = 0;

    // Ghost starts inside the house, bobs for ~2 seconds, then exits
    gGhost.x = 14 * CELL + CELL/2;   // centre of ghost house
    gGhost.y = 14 * CELL + CELL/2;
    gGhost.dx = 0.0f;
    gGhost.dy = 1.0f;                 // bob downward first
    gGhost.color = {1.0f, 0.2f, 0.2f};
    gGhost.mode = GHOST_IN_HOUSE;
    gGhost.exitTimer = 120;           // ~2 seconds at 60 fps before exiting
    gGhost.isFrightened = false;
    gGhost.frightTimer = 0;
    gGhost.isDead = false;
    gGhost.deadTimer = 0;

    // Reset pellets
    for (Pellet& p : gPellets) p.eaten = false;

    gScore = 0;
    gLives = 3;
    gState = STATE_PLAYING;
}

// ─── Movement ────────────────────────────────────────────────────────────────
static bool moveEntity(float& x, float& y, float& dx, float& dy, float speed) {
    float nx = x + dx * speed;
    float ny = y + dy * speed;

    bool blocked = false;
    float r = CELL * 0.4f;

    if (dx != 0) {
        float newX = nx + (dx > 0 ? r : -r);
        if (isWallAt(newX, y - r) || isWallAt(newX, y + r)) {
            blocked = true;
        }
    }
    if (dy != 0) {
        float newY = ny + (dy > 0 ? r : -r);
        if (isWallAt(x - r, newY) || isWallAt(x + r, newY)) {
            blocked = true;
        }
    }

    if (!blocked) {
        x = nx;
        y = ny;
    }

    // Tunnel wrapping
    if (x < -CELL/2) x = WIN_W - CELL/2;
    if (x > WIN_W + CELL/2) x = -CELL/2;

    return !blocked;
}

// ─── Ghost AI ────────────────────────────────────────────────────────────────
// Ghost house centre and door positions (in pixel coords)
static const float HOUSE_CX  = 14 * CELL + CELL/2;   // x to align with door
static const float HOUSE_TOP = 13 * CELL + CELL/2;   // topmost row inside house
static const float HOUSE_BOT = 15 * CELL + CELL/2;   // bottommost row inside house
static const float DOOR_Y    = 12 * CELL + CELL/2;   // row of the '-' door tiles
static const float EXIT_Y    = 11 * CELL + CELL/2;   // row above door (open corridor)

static void updateGhost() {
    // ── Dead: wait then re-enter house ──────────────────────────────────────
    if (gGhost.isDead) {
        gGhost.deadTimer--;
        if (gGhost.deadTimer <= 0) {
            gGhost.isDead = false;
            gGhost.x = HOUSE_CX;
            gGhost.y = HOUSE_TOP;
            gGhost.dx = 0.0f;
            gGhost.dy = 1.0f;
            gGhost.mode = GHOST_IN_HOUSE;
            gGhost.exitTimer = 90;   // shorter wait on respawn
        }
        return;
    }

    // Update frightened timer
    if (gGhost.isFrightened) {
        gGhost.frightTimer--;
        if (gGhost.frightTimer <= 0) gGhost.isFrightened = false;
    }

    float speed = gGhost.isFrightened ? FRIGHT_SPEED : GHOST_SPEED_CUR;

    // ── GHOST_IN_HOUSE: bob up and down, count down exit timer ──────────────
    if (gGhost.mode == GHOST_IN_HOUSE) {
        // Simple vertical bob within the house bounds
        gGhost.y += gGhost.dy * speed;
        if (gGhost.y >= HOUSE_BOT) { gGhost.y = HOUSE_BOT; gGhost.dy = -1.0f; }
        if (gGhost.y <= HOUSE_TOP) { gGhost.y = HOUSE_TOP; gGhost.dy =  1.0f; }

        gGhost.exitTimer--;
        if (gGhost.exitTimer <= 0) {
            // Align to door column and start moving up
            gGhost.x = HOUSE_CX;
            gGhost.dx = 0.0f;
            gGhost.dy = -1.0f;
            gGhost.mode = GHOST_EXITING;
        }
        return;
    }

    // ── GHOST_EXITING: move straight up through the door ────────────────────
    if (gGhost.mode == GHOST_EXITING) {
        gGhost.y -= speed;
        if (gGhost.y <= EXIT_Y) {
            // Fully out of house — snap to corridor centre and start roaming
            gGhost.y = EXIT_Y;
            // Pick a random horizontal direction to start with
            gGhost.dx = (rand() % 2 == 0) ? 1.0f : -1.0f;
            gGhost.dy = 0.0f;
            gGhost.mode = GHOST_ROAMING;
        }
        return;
    }

    // ── GHOST_ROAMING: normal grid-based AI ─────────────────────────────────
    int gridX = (int)(gGhost.x / CELL);
    int gridY = (int)(gGhost.y / CELL);
    float centerX = gridX * CELL + CELL/2;
    float centerY = gridY * CELL + CELL/2;
    float distToCenter = sqrtf((gGhost.x - centerX)*(gGhost.x - centerX) +
                               (gGhost.y - centerY)*(gGhost.y - centerY));

    if (distToCenter < speed + 1.0f) {
        gGhost.x = centerX;
        gGhost.y = centerY;

        int dirs[4][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
        int bestDX = (int)gGhost.dx, bestDY = (int)gGhost.dy;
        float bestDist = 1e9f;

        for (auto& d : dirs) {
            if (d[0] == -(int)gGhost.dx && d[1] == -(int)gGhost.dy) continue;
            int nc = gridX + d[0];
            int nr = gridY + d[1];
            // Roaming ghosts cannot re-enter the house through the door
            if (isWallForGhost(nc, nr, false)) continue;

            float targetX = pacX, targetY = pacY;
            if (gGhost.isFrightened) {
                targetX = (float)(rand() % WIN_W);
                targetY = (float)(rand() % WIN_H);
            }

            float ddx = nc * CELL - targetX;
            float ddy = nr * CELL - targetY;
            float dist = ddx*ddx + ddy*ddy;
            if (dist < bestDist) {
                bestDist = dist;
                bestDX = d[0];
                bestDY = d[1];
            }
        }

        if (bestDX != 0 || bestDY != 0) {
            gGhost.dx = (float)bestDX;
            gGhost.dy = (float)bestDY;
        }
    }

    moveEntity(gGhost.x, gGhost.y, gGhost.dx, gGhost.dy, speed);
}

// ─── Game Update ─────────────────────────────────────────────────────────────
static void update() {
    gFrame++;

    // FIX 2: Clamp mouth angle so it never exceeds MOUTH_MAX (well below 1.0)
    mouthAngle += 0.08f * mouthDir;
    if (mouthAngle > MOUTH_MAX) { mouthAngle = MOUTH_MAX; mouthDir = -1.0f; }
    if (mouthAngle < MOUTH_MIN) { mouthAngle = MOUTH_MIN; mouthDir =  1.0f; }

    // Global fright timer
    if (globalFrightened) {
        globalFrightTimer--;
        if (globalFrightTimer <= 0) {
            globalFrightened = false;
            gGhost.isFrightened = false;
        }
    }

    // Try to change Pac-Man direction
    if (pacNextDX != 0 || pacNextDY != 0) {
        int gridX = (int)(pacX / CELL);
        int gridY = (int)(pacY / CELL);
        int nc = gridX + (int)pacNextDX;
        int nr = gridY + (int)pacNextDY;
        if (!isWall(nc, nr)) {
            float centerX = gridX * CELL + CELL/2;
            float centerY = gridY * CELL + CELL/2;
            if (fabs(pacX - centerX) < PAC_SPEED + 0.5f &&
                fabs(pacY - centerY) < PAC_SPEED + 0.5f) {
                pacX = centerX;
                pacY = centerY;
                pacDX = pacNextDX;
                pacDY = pacNextDY;
                pacLastDX = pacDX;
                pacLastDY = pacDY;
                pacNextDX = pacNextDY = 0;
            }
        }
    }

    // Move Pac-Man
    moveEntity(pacX, pacY, pacDX, pacDY, PAC_SPEED);

    // Eat pellets
    int pc = (int)(pacX / CELL), pr = (int)(pacY / CELL);
    for (Pellet& p : gPellets) {
        if (!p.eaten && p.c == pc && p.r == pr) {
            p.eaten = true;
            gPelletsEaten++;
            if (p.power) {
                gScore += 50;
                globalFrightened = true;
                globalFrightTimer = 360;
                gGhost.isFrightened = true;
                gGhost.frightTimer = 360;
            } else {
                gScore += 10;
            }
            if (gScore > gHiScore) gHiScore = gScore;
            // FIX 3: recalculate speed on every pellet eaten
            updateSpeed();
        }
    }

    // Check win condition
    int remaining = 0;
    for (auto& p : gPellets) if (!p.eaten) remaining++;
    if (remaining == 0) {
        gState = STATE_WIN;
        return;
    }

    // Update ghost
    updateGhost();

    // Check collision — only when ghost is roaming (not inside house or exiting)
    if (gGhost.mode == GHOST_ROAMING && !gGhost.isDead) {
        float dx = pacX - gGhost.x, dy = pacY - gGhost.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < CELL * 0.6f) {
            if (gGhost.isFrightened || globalFrightened) {
                gGhost.isDead = true;
                gGhost.deadTimer = 180;
                gScore += 200;
                if (gScore > gHiScore) gHiScore = gScore;
            } else {
                gLives--;
                if (gLives <= 0) {
                    gState = STATE_DEAD;
                    return;
                }
                // Reset Pac-Man; ghost goes back into house
                pacX = 14 * CELL + CELL/2;
                pacY = 23 * CELL + CELL/2;
                pacDX = 1.0f; pacDY = 0.0f;
                pacNextDX = 0; pacNextDY = 0;
                gGhost.x = HOUSE_CX;
                gGhost.y = HOUSE_TOP;
                gGhost.dx = 0.0f; gGhost.dy = 1.0f;
                gGhost.isDead = false;
                gGhost.isFrightened = false;
                gGhost.mode = GHOST_IN_HOUSE;
                gGhost.exitTimer = 90;
            }
        }
    }
}

// ─── GLUT Callbacks ──────────────────────────────────────────────────────────
static void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();

    renderMaze();
    renderPellets();

    // Draw ghost (always visible — inside house, exiting, or roaming)
    if (!gGhost.isDead) {
        drawGhost(gGhost.x, gGhost.y, CELL * 0.45f, gGhost.color,
                 gGhost.isFrightened || globalFrightened, false);
    } else {
        drawGhost(gGhost.x, gGhost.y, CELL * 0.45f, gGhost.color, false, true);
    }

    // FIX 1: Draw Pac-Man in YELLOW (1, 1, 0)
    setColor(1.0f, 1.0f, 0.0f);
    float facing = atan2f(pacLastDY, pacLastDX);
    drawPacman(pacX, pacY, CELL * 0.45f, mouthAngle, facing);

    renderHUD();

    if (gState == STATE_IDLE)
        renderOverlay("PAC-MAN", "PRESS SPACE TO START");
    else if (gState == STATE_WIN)
        renderOverlay("YOU WIN!", "SPACE TO PLAY AGAIN");
    else if (gState == STATE_DEAD)
        renderOverlay("GAME OVER", "SPACE TO TRY AGAIN");

    glutSwapBuffers();
}

static void reshape(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, WIN_W, WIN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
}

static void timer(int) {
    if (gState == STATE_PLAYING) update();
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

static void keyboard(unsigned char key, int, int) {
    if (key == ' ') {
        if (gState != STATE_PLAYING) initGame();
        return;
    }
    if (key == 27) exit(0);
    if (gState != STATE_PLAYING) return;

    switch (key) {
        case 'w': case 'W': pacNextDX = 0; pacNextDY = -1; break;
        case 's': case 'S': pacNextDX = 0; pacNextDY = 1; break;
        case 'a': case 'A': pacNextDX = -1; pacNextDY = 0; break;
        case 'd': case 'D': pacNextDX = 1; pacNextDY = 0; break;
    }
}

static void specialKey(int key, int, int) {
    if (gState != STATE_PLAYING) return;
    switch (key) {
        case GLUT_KEY_UP:    pacNextDX = 0; pacNextDY = -1; break;
        case GLUT_KEY_DOWN:  pacNextDX = 0; pacNextDY = 1; break;
        case GLUT_KEY_LEFT:  pacNextDX = -1; pacNextDY = 0; break;
        case GLUT_KEY_RIGHT: pacNextDX = 1; pacNextDY = 0; break;
    }
}

// ─── Main ────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    srand((unsigned)time(nullptr));

    // Build pellet list from map
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            char ch = MAP[r][c];
            if (ch == '.' || ch == 'o') {
                Pellet p;
                p.c = c; p.r = r;
                p.eaten = false;
                p.power = (ch == 'o');
                gPellets.push_back(p);
            }
        }
    }

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(WIN_W, WIN_H);
    glutCreateWindow("PAC-MAN | SPACE to Start | ESC to Exit");

    glClearColor(0, 0, 0, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(specialKey);
    glutTimerFunc(16, timer, 0);

    printf("\n========================================\n");
    printf("  PAC-MAN (FIXED)\n");
    printf("  Fixes applied:\n");
    printf("  1. Pac-Man is now YELLOW\n");
    printf("  2. Power pellet no longer hides Pac-Man\n");
    printf("  3. Game speeds up as you eat pellets\n");
    printf("  4. Ghost exits house and roams randomly\n");
    printf("\n  Arrow Keys or WASD to move\n");
    printf("  SPACE to start  |  ESC to quit\n");
    printf("========================================\n\n");

    glutMainLoop();
    return 0;
}
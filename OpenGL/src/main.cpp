
#include <windows.h>
#include <GL/glut.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <string>

static const int COLS       = 28;
static const int ROWS       = 31;
static const int CELL       = 20;
static const int WIN_W      = COLS * CELL;
static const int WIN_H      = ROWS * CELL;
static const float PI       = 3.14159265f;

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
    int id;              // unique ghost id for behavior variation
    float x, y;
    float dx, dy;
    float lastX, lastY;  // for stuck detection
    int stuckFrames;
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
static std::vector<Ghost> gGhosts;

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

static void drawPacman(float cx, float cy, float r, float mouth, float facing) {
    float mRad = mouth * PI;           
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

    // Two ghosts start inside the house, with staggered exits.
    gGhosts.clear();
    Ghost g1;
    g1.id = 0;
    g1.x = 14 * CELL + CELL/2 - CELL * 0.5f;
    g1.y = 14 * CELL + CELL/2;
    g1.dx = 0.0f;
    g1.dy = 1.0f;
    g1.lastX = g1.x;
    g1.lastY = g1.y;
    g1.stuckFrames = 0;
    g1.color = {1.0f, 0.2f, 0.2f};
    g1.mode = GHOST_IN_HOUSE;
    g1.exitTimer = 90;
    g1.isFrightened = false;
    g1.frightTimer = 0;
    g1.isDead = false;
    g1.deadTimer = 0;
    gGhosts.push_back(g1);

    Ghost g2;
    g2.id = 1;
    g2.x = 14 * CELL + CELL/2 + CELL * 0.5f;
    g2.y = 14 * CELL + CELL/2;
    g2.dx = 0.0f;
    g2.dy = -1.0f;
    g2.lastX = g2.x;
    g2.lastY = g2.y;
    g2.stuckFrames = 0;
    g2.color = {1.0f, 0.5f, 0.0f};
    g2.mode = GHOST_IN_HOUSE;
    g2.exitTimer = 150;
    g2.isFrightened = false;
    g2.frightTimer = 0;
    g2.isDead = false;
    g2.deadTimer = 0;
    gGhosts.push_back(g2);

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

static void updateGhost(Ghost& ghost) {
    // ── Dead: wait then re-enter house ──────────────────────────────────────
    if (ghost.isDead) {
        ghost.deadTimer--;
        if (ghost.deadTimer <= 0) {
            ghost.isDead = false;
            ghost.x = HOUSE_CX;
            ghost.y = HOUSE_TOP;
            ghost.dx = 0.0f;
            ghost.dy = (ghost.id == 0) ? 1.0f : -1.0f;
            ghost.lastX = ghost.x;
            ghost.lastY = ghost.y;
            ghost.stuckFrames = 0;
            ghost.mode = GHOST_IN_HOUSE;
            ghost.exitTimer = (ghost.id == 0) ? 90 : 150;   // staggered respawn exits
        }
        return;
    }

    // Update frightened timer
    if (ghost.isFrightened) {
        ghost.frightTimer--;
        if (ghost.frightTimer <= 0) ghost.isFrightened = false;
    }

    float speed = ghost.isFrightened ? FRIGHT_SPEED : GHOST_SPEED_CUR;

    // ── GHOST_IN_HOUSE: bob up and down, count down exit timer ──────────────
    if (ghost.mode == GHOST_IN_HOUSE) {
        // Simple vertical bob within the house bounds
        ghost.y += ghost.dy * speed;
        if (ghost.y >= HOUSE_BOT) { ghost.y = HOUSE_BOT; ghost.dy = -1.0f; }
        if (ghost.y <= HOUSE_TOP) { ghost.y = HOUSE_TOP; ghost.dy =  1.0f; }

        ghost.exitTimer--;
        if (ghost.exitTimer <= 0) {
            // Align to door column and start moving up
            ghost.x = HOUSE_CX;
            ghost.dx = 0.0f;
            ghost.dy = -1.0f;
            ghost.mode = GHOST_EXITING;
        }
        return;
    }

    // ── GHOST_EXITING: move straight up through the door ────────────────────
    if (ghost.mode == GHOST_EXITING) {
        ghost.y -= speed;
        if (ghost.y <= EXIT_Y) {
            // Fully out of house — snap to corridor centre and start roaming
            ghost.y = EXIT_Y;
            // Force opposite initial directions so they split immediately.
            ghost.dx = (ghost.id == 0) ? -1.0f : 1.0f;
            ghost.dy = 0.0f;
            ghost.lastX = ghost.x;
            ghost.lastY = ghost.y;
            ghost.stuckFrames = 0;
            ghost.mode = GHOST_ROAMING;
        }
        return;
    }

    // ── GHOST_ROAMING: normal grid-based AI ─────────────────────────────────
    int gridX = (int)(ghost.x / CELL);
    int gridY = (int)(ghost.y / CELL);
    float centerX = gridX * CELL + CELL/2;
    float centerY = gridY * CELL + CELL/2;
    float distToCenter = sqrtf((ghost.x - centerX)*(ghost.x - centerX) +
                               (ghost.y - centerY)*(ghost.y - centerY));

    if (distToCenter < speed + 1.0f) {
        ghost.x = centerX;
        ghost.y = centerY;

        int dirs[4][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
        int bestDX = (int)ghost.dx, bestDY = (int)ghost.dy;
        float bestDist = 1e9f;
        bool foundOption = false;

        for (auto& d : dirs) {
            if (d[0] == -(int)ghost.dx && d[1] == -(int)ghost.dy) continue;
            int nc = gridX + d[0];
            int nr = gridY + d[1];
            // Roaming ghosts cannot re-enter the house through the door
            if (isWallForGhost(nc, nr, false)) continue;
            foundOption = true;

            float targetX = pacX, targetY = pacY;
            if (ghost.id == 1) {
                // Ghost 2 aims a few tiles ahead of Pac-Man to avoid mirroring ghost 1.
                targetX += pacLastDX * CELL * 4.0f;
                targetY += pacLastDY * CELL * 4.0f;
                if (targetX < 0) targetX += WIN_W;
                if (targetX >= WIN_W) targetX -= WIN_W;
                if (targetY < 0) targetY = 0;
                if (targetY >= WIN_H) targetY = WIN_H - 1;
            }
            if (ghost.isFrightened) {
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

        // If no non-reverse turn is available (dead-end), allow reverse.
        if (!foundOption) {
            for (auto& d : dirs) {
                int nc = gridX + d[0];
                int nr = gridY + d[1];
                if (isWallForGhost(nc, nr, false)) continue;
                bestDX = d[0];
                bestDY = d[1];
                break;
            }
        }

        if (bestDX != 0 || bestDY != 0) {
            ghost.dx = (float)bestDX;
            ghost.dy = (float)bestDY;
        }
    }

    if (!moveEntity(ghost.x, ghost.y, ghost.dx, ghost.dy, speed)) {
        // Safety: if a chosen direction gets blocked unexpectedly, reverse once.
        ghost.dx = -ghost.dx;
        ghost.dy = -ghost.dy;
        moveEntity(ghost.x, ghost.y, ghost.dx, ghost.dy, speed);
    }

    // Robust anti-stuck: if the ghost isn't changing position, force a new turn.
    float moved = fabsf(ghost.x - ghost.lastX) + fabsf(ghost.y - ghost.lastY);
    if (moved < 0.05f) ghost.stuckFrames++;
    else ghost.stuckFrames = 0;
    ghost.lastX = ghost.x;
    ghost.lastY = ghost.y;

    if (ghost.mode == GHOST_ROAMING && ghost.stuckFrames > 12) {
        int gc = (int)(ghost.x / CELL);
        int gr = (int)(ghost.y / CELL);
        int dirs[4][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
        int start = rand() % 4;
        for (int k = 0; k < 4; k++) {
            int* d = dirs[(start + k) % 4];
            int nc = gc + d[0];
            int nr = gr + d[1];
            if (isWallForGhost(nc, nr, false)) continue;
            ghost.dx = (float)d[0];
            ghost.dy = (float)d[1];
            break;
        }
        ghost.stuckFrames = 0;
        moveEntity(ghost.x, ghost.y, ghost.dx, ghost.dy, speed);
    }
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
            for (Ghost& ghost : gGhosts) ghost.isFrightened = false;
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
                for (Ghost& ghost : gGhosts) {
                    if (!ghost.isDead) {
                        ghost.isFrightened = true;
                        ghost.frightTimer = 360;
                    }
                }
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

    // Update ghosts
    for (Ghost& ghost : gGhosts) updateGhost(ghost);

    // Keep ghosts from sitting exactly on top of each other.
    if (gGhosts.size() >= 2) {
        for (size_t i = 0; i < gGhosts.size(); ++i) {
            for (size_t j = i + 1; j < gGhosts.size(); ++j) {
                Ghost& a = gGhosts[i];
                Ghost& b = gGhosts[j];
                if (a.isDead || b.isDead) continue;
                float dx = b.x - a.x;
                float dy = b.y - a.y;
                float d2 = dx*dx + dy*dy;
                float minD = CELL * 0.6f;
                if (d2 > 0.0001f && d2 < minD * minD) {
                    float d = sqrtf(d2);
                    float push = (minD - d) * 0.5f;
                    float ux = dx / d, uy = dy / d;
                    a.x -= ux * push; a.y -= uy * push;
                    b.x += ux * push; b.y += uy * push;
                }
            }
        }
    }

    // Check collisions against all roaming ghosts.
    for (Ghost& ghost : gGhosts) {
        if (ghost.mode != GHOST_ROAMING || ghost.isDead) continue;
        float dx = pacX - ghost.x, dy = pacY - ghost.y;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < CELL * 0.6f) {
            if (ghost.isFrightened || globalFrightened) {
                ghost.isDead = true;
                ghost.deadTimer = 180;
                gScore += 200;
                if (gScore > gHiScore) gHiScore = gScore;
            } else {
                gLives--;
                if (gLives <= 0) {
                    gState = STATE_DEAD;
                    return;
                }
                // Reset Pac-Man and send all ghosts back into house.
                pacX = 14 * CELL + CELL/2;
                pacY = 23 * CELL + CELL/2;
                pacDX = 1.0f; pacDY = 0.0f;
                pacNextDX = 0; pacNextDY = 0;
                for (size_t i = 0; i < gGhosts.size(); ++i) {
                    Ghost& g = gGhosts[i];
                    g.x = HOUSE_CX + ((i == 0) ? -CELL * 0.5f : CELL * 0.5f);
                    g.y = 14 * CELL + CELL/2;
                    g.dx = 0.0f;
                    g.dy = (i == 0) ? 1.0f : -1.0f;
                    g.lastX = g.x;
                    g.lastY = g.y;
                    g.stuckFrames = 0;
                    g.isDead = false;
                    g.isFrightened = false;
                    g.mode = GHOST_IN_HOUSE;
                    g.exitTimer = (i == 0) ? 90 : 150;
                }
                break;
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

    // Draw ghosts (always visible — inside house, exiting, or roaming)
    for (const Ghost& ghost : gGhosts) {
        if (!ghost.isDead) {
            drawGhost(ghost.x, ghost.y, CELL * 0.45f, ghost.color,
                     ghost.isFrightened || globalFrightened, false);
        } else {
            drawGhost(ghost.x, ghost.y, CELL * 0.45f, ghost.color, false, true);
        }
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
    printf("\n  Arrow Keys or WASD to move\n");
    printf("  SPACE to start  |  ESC to quit\n");
    printf("========================================\n\n");

    glutMainLoop();
    return 0;
}
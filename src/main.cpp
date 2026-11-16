#include <Arduino.h>
#include <math.h>
#include "display.h"

namespace {
constexpr int Width = 170, Height = 320;
constexpr int Rows = 13, Cols = 8;
constexpr int Radius = 9, Pitch = 19, Rise = 17, Top = 42;
constexpr int LeftWall = 18, RightWall = 151, LauncherY = 281;
constexpr uint8_t LeftButton = 0, RightButton = 14;
constexpr uint32_t AimTime = 4000;
constexpr uint16_t Background = 0x0863, Panel = 0x1107, Ink = 0xEFDF;
constexpr uint16_t Colors[] = {0, 0xFB0F, 0x3DFF, 0x47D0, 0xFE87, 0xB39F};
constexpr uint16_t Shades[] = {0, 0xA126, 0x1257, 0x1BE8, 0xAC42, 0x610F};

struct Cell { int row, col; };
struct Point { float x, y; };
enum class Phase { Ready, Aim, Flight, Won, Lost };

LGFX display;
LGFX_Sprite canvas(&display);
uint8_t board[Rows][Cols]{};
Phase phase = Phase::Ready;
float angle = 0;
Point shot{}, velocity{};
uint8_t activeColor = 1, nextColor = 1;
uint32_t deadline = 0;
unsigned score = 0, level = 1, misses = 0;
bool shifted = false, buttonsReleased = false;

int offset(int row) { return (row + int(shifted)) % 2; }
int columns(int row) { return Cols - offset(row); }

Point center(Cell cell) {
    return {float(LeftWall + Pitch * cell.col + offset(cell.row) * 10),
            float(Top + Rise * cell.row)};
}

float distanceSquared(Point a, Point b) {
    const float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

bool valid(Cell cell) {
    return cell.row >= 0 && cell.row < Rows && cell.col >= 0 && cell.col < columns(cell.row);
}

template <typename Visit>
void neighbors(Cell cell, Visit visit) {
    const int diagonal = cell.col + offset(cell.row) - 1;
    const Cell adjacent[] = {{cell.row, cell.col - 1}, {cell.row, cell.col + 1},
                             {cell.row - 1, diagonal}, {cell.row - 1, diagonal + 1},
                             {cell.row + 1, diagonal}, {cell.row + 1, diagonal + 1}};
    for (Cell candidate : adjacent) {
        if (valid(candidate)) visit(candidate);
    }
}

uint8_t chooseColor() {
    bool present[6]{};
    for (const auto& row : board) {
        for (uint8_t color : row) present[color] = true;
    }
    uint8_t choices[5];
    unsigned count = 0;
    for (uint8_t color = 1; color <= 5; ++color) {
        if (present[color]) choices[count++] = color;
    }
    return count ? choices[esp_random() % count] : 1;
}

bool colorPresent(uint8_t color) {
    for (const auto& row : board) {
        for (uint8_t item : row) {
            if (item == color) return true;
        }
    }
    return false;
}

void beginAim(uint32_t now) {
    deadline = now + AimTime;
    phase = Phase::Aim;
}

void startLevel(uint32_t now) {
    memset(board, 0, sizeof(board));
    shifted = false;
    const int depth = level < 5 ? 4 + int(level) : 9;
    const unsigned palette = level == 1 ? 4 : 5;
    for (int row = 0; row < depth; ++row) {
        for (int col = 0; col < columns(row); ++col) {
            const bool pair = col % 2 && esp_random() % 4 != 0;
            board[row][col] = pair ? board[row][col - 1] : 1 + esp_random() % palette;
        }
    }
    misses = 0;
    angle = 0;
    activeColor = chooseColor();
    nextColor = chooseColor();
    beginAim(now);
}

unsigned flood(Cell* queue, unsigned count, bool (&marked)[Rows][Cols], uint8_t color) {
    for (unsigned head = 0; head < count; ++head) {
        neighbors(queue[head], [&](Cell cell) {
            const uint8_t value = board[cell.row][cell.col];
            if (value && (!color || value == color) && !marked[cell.row][cell.col]) {
                marked[cell.row][cell.col] = true;
                queue[count++] = cell;
            }
        });
    }
    return count;
}

unsigned popCluster(Cell landed) {
    bool marked[Rows][Cols]{};
    Cell queue[Rows * Cols]{landed};
    marked[landed.row][landed.col] = true;
    const unsigned count = flood(queue, 1, marked, activeColor);
    if (count < 3) return 0;

    for (unsigned i = 0; i < count; ++i) board[queue[i].row][queue[i].col] = 0;
    memset(marked, 0, sizeof(marked));
    unsigned roots = 0;
    for (int col = 0; col < columns(0); ++col) {
        if (board[0][col]) {
            marked[0][col] = true;
            queue[roots++] = {0, col};
        }
    }
    flood(queue, roots, marked, 0);
    unsigned dropped = 0;
    for (int row = 0; row < Rows; ++row) {
        for (int col = 0; col < columns(row); ++col) {
            if (board[row][col] && !marked[row][col]) {
                board[row][col] = 0;
                ++dropped;
            }
        }
    }
    score += count * 10 + dropped * 20;
    return count;
}

void finishShot(Cell hit, uint32_t now) {
    Cell landed{-1, -1};
    float best = 1e9f;
    const auto consider = [&](Cell cell) {
        if (board[cell.row][cell.col]) return;
        const float distance = distanceSquared(shot, center(cell));
        if (distance < best) {
            best = distance;
            landed = cell;
        }
    };
    if (valid(hit)) {
        neighbors(hit, consider);
    } else {
        for (int col = 0; col < columns(0); ++col) consider({0, col});
    }
    if (!valid(landed)) {
        phase = Phase::Lost;
        return;
    }

    board[landed.row][landed.col] = activeColor;
    if (popCluster(landed)) {
        misses = 0;
    } else if (++misses == 5) {
        for (int row = Rows - 1; row > 0; --row) {
            memcpy(board[row], board[row - 1], sizeof(board[row]));
        }
        shifted = !shifted;
        memset(board[0], 0, sizeof(board[0]));
        for (int col = 0; col < columns(0); ++col) board[0][col] = chooseColor();
        misses = 0;
    }

    bool empty = true;
    for (const auto& row : board) {
        for (uint8_t color : row) empty &= color == 0;
    }
    if (empty) {
        phase = Phase::Won;
    } else {
        for (uint8_t color : board[Rows - 1]) {
            if (color) {
                phase = Phase::Lost;
                return;
            }
        }
        activeColor = colorPresent(nextColor) ? nextColor : chooseColor();
        nextColor = chooseColor();
        beginAim(now);
    }
}

Point direction() { return {sinf(angle), -cosf(angle)}; }

void advance(Point& point, Point& speed, float seconds) {
    point.x += speed.x * seconds;
    point.y += speed.y * seconds;
    if (point.x < LeftWall || point.x > RightWall) {
        const int wall = point.x < LeftWall ? LeftWall : RightWall;
        point.x = 2 * wall - point.x;
        speed.x = -speed.x;
    }
}

Cell collision(Point point) {
    Cell hit{-1, -1};
    float best = 18.0f * 18.0f;
    for (int row = 0; row < Rows; ++row) {
        if (fabsf(point.y - (Top + Rise * row)) > 18) continue;
        for (int col = 0; col < columns(row); ++col) {
            if (!board[row][col]) continue;
            const float distance = distanceSquared(point, center({row, col}));
            if (distance <= best) {
                best = distance;
                hit = {row, col};
            }
        }
    }
    return hit;
}

void bubble(Point point, uint8_t color, int radius = Radius) {
    const int x = lroundf(point.x), y = lroundf(point.y);
    canvas.fillCircle(x, y + 1, radius, Shades[color]);
    canvas.fillCircle(x, y, radius - 1, Colors[color]);
    canvas.drawCircle(x, y, radius - 1, Shades[color]);
    canvas.fillCircle(x - 2, y - 3, radius > 7 ? 2 : 1, 0xFFFF);
}

void draw(uint32_t now) {
    canvas.fillScreen(Background);
    canvas.fillRect(0, 0, Width, 28, Panel);
    canvas.setTextSize(1);
    canvas.setTextColor(Ink);
    canvas.setCursor(8, 9);
    canvas.printf("LV %02u     SCORE %05u", level, score);
    canvas.drawFastVLine(8, 30, 228, 0x224C);
    canvas.drawFastVLine(161, 30, 228, 0x224C);
    for (int x = 10; x < 160; x += 6) canvas.drawFastHLine(x, 253, 3, 0xB30D);
    for (int row = 0; row < Rows; ++row) {
        for (int col = 0; col < columns(row); ++col) {
            if (board[row][col]) bubble(center({row, col}), board[row][col]);
        }
    }
    if (phase == Phase::Aim) {
        Point dot{85, LauncherY}, step = direction();
        for (int i = 0; i < 170; ++i) {
            advance(dot, step, 4);
            if (dot.y <= Top || valid(collision(dot))) break;
            if (i > 2 && i % 3 == 0) canvas.fillCircle(dot.x, dot.y, 1, Colors[activeColor]);
        }
    }
    if (phase == Phase::Flight) bubble(shot, activeColor);
    canvas.fillCircle(85, LauncherY, 13, Panel);
    canvas.drawCircle(85, LauncherY, 13, 0x32AD);
    if (phase != Phase::Flight) bubble({85, LauncherY}, activeColor);
    bubble({145, LauncherY}, nextColor, 7);
    canvas.setCursor(12, 277);
    canvas.print("DROP");
    for (unsigned i = 0; i < 5; ++i) {
        canvas.fillCircle(13 + i * 6, 291, 2, i < misses ? 0xFB6B : 0x224C);
    }
    canvas.setCursor(134, 293);
    canvas.print("NEXT");

    const uint32_t remaining = phase == Phase::Aim && int32_t(deadline - now) > 0 ? deadline - now : 0;
    canvas.fillRoundRect(8, 306, 154, 8, 4, Panel);
    if (remaining) {
        canvas.fillRect(10, 308, 150 * remaining / AimTime, 4, remaining <= 1000 ? 0xFB6B : 0x3DFF);
        canvas.setTextDatum(lgfx::middle_center);
        canvas.drawNumber((remaining + 999) / 1000, 85, 299);
        canvas.setTextDatum(lgfx::top_left);
    }

    if (phase == Phase::Ready || phase == Phase::Won || phase == Phase::Lost) {
        canvas.fillRoundRect(12, 113, 146, 86, 9, Panel);
        canvas.drawRoundRect(12, 113, 146, 86, 9, 0x3DFF);
        canvas.setTextDatum(lgfx::middle_center);
        canvas.setTextSize(2);
        canvas.drawString(phase == Phase::Ready ? "BUBBLE POP" :
                          phase == Phase::Won ? "LEVEL CLEAR" : "GAME OVER", 85, 134);
        canvas.setTextSize(1);
        canvas.drawString(phase == Phase::Ready ? "AIM LEFT / RIGHT" :
                          phase == Phase::Won ? "NEXT LEVEL" : "TRY AGAIN", 85, 158);
        canvas.drawString("PRESS EITHER BUTTON", 85, 180);
        canvas.setTextDatum(lgfx::top_left);
    }
    canvas.pushSprite(0, 0);
}
} // namespace

void setup() {
    Serial.begin(115200);
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);
    pinMode(LeftButton, INPUT_PULLUP);
    pinMode(RightButton, INPUT_PULLUP);
    display.init();
    display.setRotation(0);
    display.setBrightness(165);
    canvas.setColorDepth(16);
    if (!canvas.createSprite(Width, Height)) abort();
    startLevel(millis());
    phase = Phase::Ready;
    draw(millis());
    Serial.println("Bubble Pop ready");
}

void loop() {
    static uint32_t lastFrame = millis();
    const uint32_t now = millis();
    const uint32_t elapsed = now - lastFrame;
    if (elapsed < 20) {
        delay(1);
        return;
    }
    lastFrame = now;
    const float seconds = min(elapsed, uint32_t(60)) * 0.001f;
    const bool left = digitalRead(LeftButton) == LOW;
    const bool right = digitalRead(RightButton) == LOW;
    if (!left && !right) buttonsReleased = true;

    switch (phase) {
        case Phase::Ready:
        case Phase::Won:
        case Phase::Lost:
            if (buttonsReleased && (left || right)) {
                if (phase == Phase::Won) ++level;
                if (phase == Phase::Lost) {
                    score = 0;
                    level = 1;
                }
                buttonsReleased = false;
                startLevel(now);
            }
            break;
        case Phase::Aim:
            if (left != right) {
                angle = constrain(angle + (left ? -1.65f : 1.65f) * seconds, -1.18f, 1.18f);
            }
            if (int32_t(now - deadline) >= 0) {
                shot = {85, LauncherY};
                velocity = direction();
                velocity.x *= 365;
                velocity.y *= 365;
                phase = Phase::Flight;
            }
            break;
        case Phase::Flight:
            for (int step = 0; step < 8; ++step) {
                advance(shot, velocity, seconds / 8);
                const Cell hit = collision(shot);
                if (valid(hit) || shot.y <= Top) {
                    finishShot(hit, now);
                    buttonsReleased = !left && !right;
                    break;
                }
            }
            break;
    }
    draw(now);
    if (Serial.available() && Serial.read() == '?') {
        Serial.printf("Bubble Pop: phase=%u angle=%.2f score=%u level=%u\n",
                      unsigned(phase), angle, score, level);
    }
}

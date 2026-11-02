/*
 * Cookie Clicker for the Nucleo-F439ZI + Adafruit SHARP Memory Display (144x168)
 *
 *   TAP the user button   -> bake cookies (the cookie squishes, crumbs fly)
 *   HOLD the user button  -> shop mode: the highlight cycles through the
 *                            buildings, release to buy the highlighted one
 *   Golden cookie         -> appears now and then; tap while it shines to
 *                            start a x7 FRENZY
 *
 * LED0 flashes on every tap, blinks three times on a purchase, strobes during
 * a frenzy and gives a heartbeat while something in the shop is affordable.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/display.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LED0_NODE DT_ALIAS(led0)
#define SW0_NODE  DT_ALIAS(sw0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

/* ------------------------------------------------------------------------ */
/* Game state                                                                */
/* ------------------------------------------------------------------------ */

#define HOLD_MS        450   /* press longer than this to open the shop */
#define STEP_MS        900   /* shop highlight moves on every STEP_MS */
#define FRENZY_MS      15000
#define FRENZY_MULT    7
#define GOLDEN_SHOW_MS 6000
#define TOAST_MS       1400

#define N_ITEMS  4
#define N_POPS   6
#define N_CRUMBS 20

struct item {
	const char *name;
	double base_cost;
	double cps;
};

static const struct item shop[N_ITEMS] = {
	{"CURSOR", 15, 0.1},
	{"GRANDMA", 100, 1},
	{"FARM", 1100, 8},
	{"FACTORY", 12000, 47},
};

struct pop {
	int16_t x, y;
	uint32_t t;
	char txt[10];
};

struct crumb {
	float x, y, vx, vy;
	uint32_t t;
};

struct game {
	double cookies;
	uint16_t owned[N_ITEMS];

	bool held;
	bool shop_mode;
	uint8_t sel;
	uint32_t press_t;
	uint32_t release_t;
	uint32_t buy_t;
	uint32_t deny_t;

	uint32_t frenzy_until;
	uint32_t golden_until;
	uint8_t golden_pos;

	struct pop pops[N_POPS];
	uint8_t pop_next;
	struct crumb crumbs[N_CRUMBS];
	uint8_t crumb_next;

	char toast[24];
	uint32_t toast_until;
};

static struct game game;
K_MUTEX_DEFINE(game_lock);

static uint32_t rng_state = 0x1234567;

static uint32_t rnd(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static double item_cost(const struct game *g, int i)
{
	return ceil(shop[i].base_cost * pow(1.15, g->owned[i]));
}

static double base_cps(const struct game *g)
{
	double cps = 0;

	for (int i = 0; i < N_ITEMS; i++) {
		cps += g->owned[i] * shop[i].cps;
	}
	return cps;
}

static bool frenzy(const struct game *g, uint32_t now)
{
	return (int32_t)(g->frenzy_until - now) > 0;
}

static double total_cps(const struct game *g, uint32_t now)
{
	return base_cps(g) * (frenzy(g, now) ? FRENZY_MULT : 1);
}

static double tap_value(const struct game *g, uint32_t now)
{
	return (1 + floor(base_cps(g) * 0.05)) * (frenzy(g, now) ? FRENZY_MULT : 1);
}

static bool anything_affordable(const struct game *g)
{
	for (int i = 0; i < N_ITEMS; i++) {
		if (g->cookies >= item_cost(g, i)) {
			return true;
		}
	}
	return false;
}

static void toast(struct game *g, uint32_t now, const char *msg)
{
	strncpy(g->toast, msg, sizeof(g->toast) - 1);
	g->toast_until = now + TOAST_MS;
}

/* ------------------------------------------------------------------------ */
/* Number formatting (no float printf needed)                                */
/* ------------------------------------------------------------------------ */

/* 1234567 -> "1,234,567" */
static void fmt_commas(uint32_t v, char *out)
{
	char tmp[16];
	int n = snprintf(tmp, sizeof(tmp), "%u", (unsigned int)v);
	int o = 0;

	for (int i = 0; i < n; i++) {
		out[o++] = tmp[i];
		if ((n - i - 1) % 3 == 0 && i != n - 1) {
			out[o++] = ',';
		}
	}
	out[o] = '\0';
}

/* Three significant digits with a suffix: 1.23M, 45.6B, 789T */
static void fmt_suffix(double v, char *out)
{
	static const char suffix[] = "KMBTQ";
	int s = -1;

	while (v >= 1000 && s < (int)sizeof(suffix) - 2) {
		v /= 1000;
		s++;
	}
	uint32_t c = (uint32_t)(v * 100);

	if (s < 0) {
		sprintf(out, "%u", (unsigned int)v);
	} else if (v < 10) {
		sprintf(out, "%u.%02u%c", c / 100, c % 100, suffix[s]);
	} else if (v < 100) {
		sprintf(out, "%u.%u%c", c / 100, (c / 10) % 10, suffix[s]);
	} else {
		sprintf(out, "%u%c", c / 100, suffix[s]);
	}
}

static void fmt_big(double v, char *out)
{
	if (v < 1e9) {
		fmt_commas((uint32_t)v, out);
	} else {
		fmt_suffix(v, out);
	}
}

static void fmt_cost(double v, char *out)
{
	if (v < 10000) {
		sprintf(out, "%u", (unsigned int)v);
	} else {
		fmt_suffix(v, out);
	}
}

static void fmt_rate(double v, char *out)
{
	if (v < 1000) {
		uint32_t t = (uint32_t)(v * 10 + 0.5);

		sprintf(out, "%u.%u", t / 10, t % 10);
	} else {
		fmt_suffix(v, out);
	}
}

/* ------------------------------------------------------------------------ */
/* Framebuffer + drawing primitives                                          */
/* ------------------------------------------------------------------------ */

#define W 144
#define H 168

/* MONO01, LSB = leftmost pixel, bit set = white */
static uint8_t fb[H][W / 8];

#define INK   1 /* black */
#define PAPER 0 /* white */

static const uint8_t bayer[4][4] = {
	{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5},
};

/* 0..16: how much of the next drawing is actually painted (16 = solid) */
static int density = 16;

static void px(int x, int y, int c)
{
	if (x < 0 || x >= W || y < 0 || y >= H) {
		return;
	}
	if (density < 16 && bayer[y & 3][x & 3] >= density) {
		return;
	}
	if (c == INK) {
		fb[y][x >> 3] &= ~BIT(x & 7);
	} else {
		fb[y][x >> 3] |= BIT(x & 7);
	}
}

/* Ordered-dither grey: level 0 (white) .. 16 (black) */
static void grey(int x, int y, int level)
{
	px(x, y, bayer[y & 3][x & 3] < level ? INK : PAPER);
}

static void fill_rect(int x0, int y0, int x1, int y1, int c)
{
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			px(x, y, c);
		}
	}
}

static void rect(int x0, int y0, int x1, int y1, int c)
{
	for (int x = x0; x <= x1; x++) {
		px(x, y0, c);
		px(x, y1, c);
	}
	for (int y = y0; y <= y1; y++) {
		px(x0, y, c);
		px(x1, y, c);
	}
}

static void line(int x0, int y0, int x1, int y1, int c)
{
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;

	while (1) {
		px(x0, y0, c);
		if (x0 == x1 && y0 == y1) {
			break;
		}
		int e2 = 2 * err;

		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

static void thick_line(int x0, int y0, int x1, int y1, int c)
{
	line(x0, y0, x1, y1, c);
	line(x0 + 1, y0, x1 + 1, y1, c);
	line(x0, y0 + 1, x1, y1 + 1, c);
}

/* Filled disc, or ring of the given thickness when thick > 0 */
static void circle(int cx, int cy, int r, int thick, int c)
{
	int outer = r * r + r;
	int inner = (thick > 0) ? (r - thick) * (r - thick) + (r - thick) : -1;

	for (int y = -r; y <= r; y++) {
		for (int x = -r; x <= r; x++) {
			int d = x * x + y * y;

			if (d <= outer && d > inner) {
				px(cx + x, cy + y, c);
			}
		}
	}
}

/* ------------------------------------------------------------------------ */
/* 5x7 font, ASCII 0x20..0x7E, column-major, bit 0 = top row                 */
/* ------------------------------------------------------------------------ */

static const uint8_t font5x7[][5] = {
	{0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00},
	{0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7F, 0x14, 0x7F, 0x14},
	{0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
	{0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00},
	{0x00, 0x1C, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1C, 0x00},
	{0x08, 0x2A, 0x1C, 0x2A, 0x08}, {0x08, 0x08, 0x3E, 0x08, 0x08},
	{0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08},
	{0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
	{0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
	{0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
	{0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
	{0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
	{0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E},
	{0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00},
	{0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14},
	{0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06},
	{0x32, 0x49, 0x79, 0x41, 0x3E}, {0x7E, 0x11, 0x11, 0x11, 0x7E},
	{0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
	{0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41},
	{0x7F, 0x09, 0x09, 0x01, 0x01}, {0x3E, 0x41, 0x41, 0x51, 0x32},
	{0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
	{0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
	{0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x04, 0x02, 0x7F},
	{0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
	{0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E},
	{0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
	{0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
	{0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x7F, 0x20, 0x18, 0x20, 0x7F},
	{0x63, 0x14, 0x08, 0x14, 0x63}, {0x03, 0x04, 0x78, 0x04, 0x03},
	{0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x7F, 0x41, 0x41, 0x00},
	{0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x7F, 0x00},
	{0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40},
	{0x00, 0x01, 0x02, 0x04, 0x00}, {0x20, 0x54, 0x54, 0x54, 0x78},
	{0x7F, 0x48, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x20},
	{0x38, 0x44, 0x44, 0x48, 0x7F}, {0x38, 0x54, 0x54, 0x54, 0x18},
	{0x08, 0x7E, 0x09, 0x01, 0x02}, {0x08, 0x14, 0x54, 0x54, 0x3C},
	{0x7F, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7D, 0x40, 0x00},
	{0x20, 0x40, 0x44, 0x3D, 0x00}, {0x00, 0x7F, 0x10, 0x28, 0x44},
	{0x00, 0x41, 0x7F, 0x40, 0x00}, {0x7C, 0x04, 0x18, 0x04, 0x78},
	{0x7C, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38},
	{0x7C, 0x14, 0x14, 0x14, 0x08}, {0x08, 0x14, 0x14, 0x18, 0x7C},
	{0x7C, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x20},
	{0x04, 0x3F, 0x44, 0x40, 0x20}, {0x3C, 0x40, 0x40, 0x20, 0x7C},
	{0x1C, 0x20, 0x40, 0x20, 0x1C}, {0x3C, 0x40, 0x30, 0x40, 0x3C},
	{0x44, 0x28, 0x10, 0x28, 0x44}, {0x0C, 0x50, 0x50, 0x50, 0x3C},
	{0x44, 0x64, 0x54, 0x4C, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00},
	{0x00, 0x00, 0x7F, 0x00, 0x00}, {0x00, 0x41, 0x36, 0x08, 0x00},
	{0x10, 0x08, 0x08, 0x10, 0x08},
};

static int text_width(const char *s, int scale)
{
	int n = strlen(s);

	return n ? n * 6 * scale - scale : 0;
}

static void text(int x, int y, const char *s, int scale, int c)
{
	for (; *s; s++, x += 6 * scale) {
		if (*s < 0x20 || *s > 0x7E) {
			continue;
		}
		const uint8_t *glyph = font5x7[*s - 0x20];

		for (int col = 0; col < 5; col++) {
			for (int row = 0; row < 7; row++) {
				if (glyph[col] & BIT(row)) {
					fill_rect(x + col * scale, y + row * scale,
						  x + col * scale + scale - 1,
						  y + row * scale + scale - 1, c);
				}
			}
		}
	}
}

static void text_centered(int cx, int y, const char *s, int scale, int c)
{
	text(cx - text_width(s, scale) / 2, y, s, scale, c);
}

/* Text with a 1 px halo so it stays readable on top of anything */
static void text_halo(int x, int y, const char *s, int scale, int c)
{
	int halo = (c == INK) ? PAPER : INK;

	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			if (dx || dy) {
				text(x + dx, y + dy, s, scale, halo);
			}
		}
	}
	text(x, y, s, scale, c);
}

/* ------------------------------------------------------------------------ */
/* Artwork                                                                   */
/* ------------------------------------------------------------------------ */

#define PI 3.14159265f

#define COOKIE_X 72
#define COOKIE_Y 64
#define COOKIE_R 26

/* Chocolate chips in polar coordinates: radius fraction, angle, size */
static const float chips[][3] = {
	{0.55f, 0.3f, 3}, {0.60f, 1.6f, 4}, {0.50f, 2.9f, 3}, {0.62f, 4.2f, 4},
	{0.55f, 5.4f, 3}, {0.12f, 0.9f, 4}, {0.30f, 3.8f, 3}, {0.32f, 2.2f, 2},
	{0.80f, 0.95f, 2}, {0.80f, 3.5f, 2},
};

/* A slowly spinning, shaded chocolate-chip cookie with a bumpy rim */
static void draw_cookie(int cx, int cy, int r, float spin)
{
	for (int dy = -r - 2; dy <= r + 2; dy++) {
		for (int dx = -r - 2; dx <= r + 2; dx++) {
			float d = sqrtf((float)(dx * dx + dy * dy));

			if (d > r + 2) {
				continue;
			}
			float a = atan2f((float)dy, (float)dx);
			float edge = r + 1.2f * sinf(9 * (a - spin)) + 0.6f * sinf(4 * (a - spin));

			if (d > edge) {
				continue;
			}
			int x = cx + dx, y = cy + dy;

			if (d > edge - 2) {
				px(x, y, INK);
				continue;
			}
			/* Light comes from the top-left */
			float s = (dx + dy) / (1.414f * r);

			if (s < -0.5f && d < r * 0.85f) {
				px(x, y, PAPER);
			} else {
				grey(x, y, 3 + (int)((s + 1) * 3.5f));
			}
		}
	}

	for (int i = 0; i < ARRAY_SIZE(chips); i++) {
		float a = chips[i][1] + spin;
		int x = cx + (int)lroundf(chips[i][0] * r * cosf(a));
		int y = cy + (int)lroundf(chips[i][0] * r * sinf(a));
		int sz = (int)chips[i][2];

		circle(x, y, sz, 0, INK);
		if (sz > 2) {
			px(x - 1, y - 1, PAPER);
		}
	}
}

/* Sunburst behind the cookie during a frenzy */
static void draw_rays(int cx, int cy, int r, float rot)
{
	for (int i = 0; i < 16; i++) {
		float a = rot + i * (2 * PI / 16);
		int r0 = r + 5, r1 = r + ((i & 1) ? 12 : 22);

		thick_line(cx + (int)(r0 * cosf(a)), cy + (int)(r0 * sinf(a)),
			   cx + (int)(r1 * cosf(a)), cy + (int)(r1 * sinf(a)), INK);
	}
}

/* Twinkling golden cookie: a star with sparkles */
static void draw_golden(int cx, int cy, uint32_t now)
{
	float pulse = 1.0f + 0.25f * sinf(now * 0.012f);
	float R = 9 * pulse, rr = R * 0.45f;
	float rot = now * 0.002f;
	int vx[10], vy[10];

	for (int i = 0; i < 10; i++) {
		float a = rot - PI / 2 + i * PI / 5;
		float rad = (i & 1) ? rr : R;

		vx[i] = cx + (int)lroundf(rad * cosf(a));
		vy[i] = cy + (int)lroundf(rad * sinf(a));
	}
	/* Fill by fanning lines from the centre */
	for (int i = 0; i < 10; i++) {
		int j = (i + 1) % 10;

		for (int k = 0; k <= 8; k++) {
			line(cx, cy, vx[i] + (vx[j] - vx[i]) * k / 8,
			     vy[i] + (vy[j] - vy[i]) * k / 8, INK);
		}
	}
	circle(cx, cy, 2, 0, PAPER);

	/* Sparkles orbiting the star */
	for (int i = 0; i < 3; i++) {
		float a = -rot * 1.7f + i * 2 * PI / 3;
		int sx = cx + (int)(15 * cosf(a)), sy = cy + (int)(15 * sinf(a));

		line(sx - 2, sy, sx + 2, sy, INK);
		line(sx, sy - 2, sx, sy + 2, INK);
	}
}

/* 10x10 shop icons */
static void icon_cursor(int x, int y, int c)
{
	for (int r = 0; r < 7; r++) {
		line(x + 1, y + r, x + 1 + r * 5 / 6, y + r, c);
	}
	thick_line(x + 3, y + 6, x + 5, y + 9, c);
}

static void icon_grandma(int x, int y, int c)
{
	circle(x + 5, y + 6, 4, 1, c);
	circle(x + 5, y + 1, 1, 0, c);
	px(x + 3, y + 5, c);
	px(x + 7, y + 5, c);
	line(x + 4, y + 8, x + 6, y + 8, c);
}

static void icon_farm(int x, int y, int c)
{
	line(x, y + 9, x + 10, y + 9, c);
	line(x + 5, y + 9, x + 5, y + 2, c);
	thick_line(x + 5, y + 6, x + 1, y + 3, c);
	thick_line(x + 5, y + 4, x + 8, y + 1, c);
}

static void icon_factory(int x, int y, int c)
{
	fill_rect(x, y + 6, x + 10, y + 9, c);
	line(x, y + 6, x + 3, y + 3, c);
	line(x + 3, y + 3, x + 3, y + 6, c);
	line(x + 3, y + 6, x + 6, y + 3, c);
	line(x + 6, y + 3, x + 6, y + 6, c);
	fill_rect(x + 8, y + 1, x + 9, y + 6, c);
	px(x + 9, y - 1, c);
	px(x + 7, y - 2, c);
}

static void (*const icons[N_ITEMS])(int, int, int) = {
	icon_cursor, icon_grandma, icon_farm, icon_factory,
};

/* Golden cookie spots around the big cookie */
static const int8_t golden_spots[][2] = {{18, 44}, {126, 44}, {18, 84}, {126, 84}};

/* ------------------------------------------------------------------------ */
/* Screen                                                                    */
/* ------------------------------------------------------------------------ */

#define SHOP_Y   100
#define ROW_H    14
#define FOOTER_Y 157

static void render(const struct game *g, uint32_t now)
{
	char buf[32], buf2[16];
	bool fz = frenzy(g, now);

	memset(fb, 0xFF, sizeof(fb));

	/* --- Cookie stage --- */
	float spin = now * 0.0004f;
	int r = COOKIE_R;

	if (g->held && !g->shop_mode) {
		r -= 3; /* squish while pressed */
	} else if (now - g->release_t < 90) {
		r += 2; /* bounce back */
	}

	if (fz) {
		draw_rays(COOKIE_X, COOKIE_Y, COOKIE_R, now * 0.0015f);
	}
	/* Soft drop shadow */
	density = 6;
	circle(COOKIE_X + 3, COOKIE_Y + 4, r, 0, INK);
	density = 16;
	draw_cookie(COOKIE_X, COOKIE_Y, r, spin);

	if ((int32_t)(g->golden_until - now) > 0) {
		draw_golden(golden_spots[g->golden_pos][0], golden_spots[g->golden_pos][1], now);
	}

	/* Crumbs flying off the cookie */
	for (int i = 0; i < N_CRUMBS; i++) {
		const struct crumb *c = &g->crumbs[i];
		float t = (now - c->t) / 1000.0f;

		if (c->t == 0 || t > 0.7f) {
			continue;
		}
		int x = (int)(c->x + c->vx * t), y = (int)(c->y + c->vy * t + 150 * t * t);

		fill_rect(x, y, x + 1, y + 1, INK);
	}

	/* "+N" pop-ups rising and dissolving */
	for (int i = 0; i < N_POPS; i++) {
		const struct pop *p = &g->pops[i];
		uint32_t age = now - p->t;

		if (p->t == 0 || age > 900) {
			continue;
		}
		density = age < 500 ? 16 : 16 - (age - 500) * 16 / 400;
		text_halo(p->x - text_width(p->txt, 1) / 2, p->y - age / 28, p->txt, 1, INK);
		density = 16;
	}

	/* --- Header: cookie count --- */
	fill_rect(0, 0, W - 1, 28, INK);
	fmt_big(g->cookies, buf);
	text_centered(W / 2, 3, buf, text_width(buf, 2) <= W - 8 ? 2 : 1, PAPER);

	if (fz) {
		uint32_t left = (g->frenzy_until - now + 999) / 1000;

		snprintf(buf, sizeof(buf), "FRENZY x%d  %us", FRENZY_MULT, (unsigned int)left);
		if ((now / 250) & 1) {
			fill_rect(0, 18, W - 1, 28, PAPER);
			text_centered(W / 2, 20, buf, 1, INK);
		} else {
			text_centered(W / 2, 20, buf, 1, PAPER);
		}
	} else {
		fmt_rate(total_cps(g, now), buf2);
		snprintf(buf, sizeof(buf), "%s per second", buf2);
		text_centered(W / 2, 20, buf, 1, PAPER);
	}
	/* Scalloped edge under the header, like icing */
	for (int x = 0; x < W; x += 8) {
		circle(x + 4, 28, 3, 0, INK);
	}

	/* --- Shop --- */
	fill_rect(0, SHOP_Y - 2, W - 1, FOOTER_Y - 2, PAPER);
	line(0, SHOP_Y - 2, W - 1, SHOP_Y - 2, INK);

	for (int i = 0; i < N_ITEMS; i++) {
		int y = SHOP_Y + i * ROW_H;
		double cost = item_cost(g, i);
		bool can = g->cookies >= cost;
		bool selected = g->shop_mode && g->sel == i;
		bool just_bought = selected == false && now - g->buy_t < 300 && g->sel == i;
		int shake = (g->sel == i && now - g->deny_t < 300) ? ((now / 40) & 1 ? 2 : -2) : 0;
		int c = INK;

		if (selected || just_bought) {
			fill_rect(1, y - 1, W - 2, y + ROW_H - 3, INK);
			c = PAPER;
		}

		icons[i](4 + shake, y + 1, c);
		text(18 + shake, y + 2, shop[i].name, 1, c);

		if (g->owned[i] > 0) {
			snprintf(buf, sizeof(buf), "%u", g->owned[i]);
			/* Owned count in a little pill */
			int pw = text_width(buf, 1) + 4;

			fill_rect(66, y, 66 + pw, y + 10, c);
			text(68, y + 2, buf, 1, c == INK ? PAPER : INK);
		}

		fmt_cost(cost, buf);
		int cx = W - 4 - text_width(buf, 1);

		text(cx + shake, y + 2, buf, 1, c);
		/* Cookie coin next to the price: solid = affordable, hollow = not yet */
		circle(cx - 6 + shake, y + 5, 3, can ? 0 : 1, c);

		if (i < N_ITEMS - 1) {
			density = 8;
			line(4, y + ROW_H - 2, W - 5, y + ROW_H - 2, INK);
			density = 16;
		}
	}

	/* --- Footer --- */
	fill_rect(0, FOOTER_Y, W - 1, H - 1, INK);

	if ((int32_t)(g->toast_until - now) > 0) {
		fill_rect(2, FOOTER_Y + 1, W - 3, H - 2, PAPER);
		text_centered(W / 2, FOOTER_Y + 2, g->toast, 1, INK);
	} else if (g->shop_mode) {
		uint32_t in_step = (now - g->press_t - HOLD_MS) % STEP_MS;
		int bar = 4 + (int)(62 * in_step / STEP_MS);

		rect(4, FOOTER_Y + 2, 66, H - 3, PAPER);
		fill_rect(4, FOOTER_Y + 2, bar, H - 3, PAPER);
		text(72, FOOTER_Y + 2, "RELEASE=BUY", 1, PAPER);
	} else if (g->held && now - g->press_t > 150) {
		int bar = 4 + (int)(W - 9) * (int)(now - g->press_t) / HOLD_MS;

		rect(4, FOOTER_Y + 2, W - 5, H - 3, PAPER);
		fill_rect(4, FOOTER_Y + 2, MIN(bar, W - 5), H - 3, PAPER);
	} else {
		text_centered(W / 2, FOOTER_Y + 2, "TAP=BAKE  HOLD=SHOP", 1, PAPER);
	}
}

/* ------------------------------------------------------------------------ */
/* Game logic                                                                */
/* ------------------------------------------------------------------------ */

static void spawn_pop(struct game *g, uint32_t now, const char *txt)
{
	struct pop *p = &g->pops[g->pop_next++ % N_POPS];

	p->x = COOKIE_X + (int)(rnd() % 41) - 20;
	p->y = COOKIE_Y - 8 + (int)(rnd() % 11) - 5;
	p->t = now;
	strncpy(p->txt, txt, sizeof(p->txt) - 1);
	p->txt[sizeof(p->txt) - 1] = '\0';
}

static void spawn_crumbs(struct game *g, uint32_t now, int n)
{
	for (int i = 0; i < n; i++) {
		struct crumb *c = &g->crumbs[g->crumb_next++ % N_CRUMBS];
		float a = (rnd() % 628) / 100.0f;

		c->x = COOKIE_X + COOKIE_R * cosf(a);
		c->y = COOKIE_Y + COOKIE_R * sinf(a);
		c->vx = 70 * cosf(a);
		c->vy = 60 * sinf(a) - 40;
		c->t = now;
	}
}

static void tap(struct game *g, uint32_t now)
{
	char buf[12];
	double v = tap_value(g, now);

	if ((int32_t)(g->golden_until - now) > 0) {
		g->golden_until = now;
		g->frenzy_until = now + FRENZY_MS;
		toast(g, now, "GOLDEN! FRENZY x7");
		spawn_crumbs(g, now, 10);
	}

	g->cookies += v;
	buf[0] = '+';
	fmt_cost(v, buf + 1);
	spawn_pop(g, now, buf);
	spawn_crumbs(g, now, 4);
}

static void buy(struct game *g, uint32_t now, int i)
{
	char buf[24];
	double cost = item_cost(g, i);

	if (g->cookies < cost) {
		g->deny_t = now;
		toast(g, now, "NOT ENOUGH COOKIES");
		return;
	}
	g->cookies -= cost;
	g->owned[i]++;
	g->buy_t = now;
	snprintf(buf, sizeof(buf), "+1 %s!", shop[i].name);
	toast(g, now, buf);
}

static bool led_state(const struct game *g, uint32_t now)
{
	if (now - g->release_t < 50 && !g->shop_mode) {
		return true; /* tap flash */
	}
	if (now - g->buy_t < 600) {
		return ((now - g->buy_t) / 100) % 2 == 0; /* 3 quick blinks */
	}
	if (frenzy(g, now)) {
		return (now / 60) % 2; /* strobe */
	}
	if (anything_affordable(g)) {
		uint32_t ph = now % 1200;

		return ph < 70 || (ph >= 180 && ph < 250); /* heartbeat */
	}
	return false;
}

static void logic_thread(void)
{
	uint32_t last = k_uptime_get_32();
	int stable = 0, candidate = 0, count = 0;
	bool led_now = false;

	rng_state ^= k_cycle_get_32();
	game.golden_until = 0;
	uint32_t next_golden = last + 20000;

	while (1) {
		uint32_t now = k_uptime_get_32();
		int raw = gpio_pin_get_dt(&button) > 0;

		k_mutex_lock(&game_lock, K_FOREVER);
		struct game *g = &game;

		g->cookies += total_cps(g, now) * (now - last) / 1000.0;
		last = now;

		/* Debounce: accept a new level after it is steady for 3 polls */
		if (raw != candidate) {
			candidate = raw;
			count = 0;
		} else if (raw != stable && ++count >= 3) {
			stable = raw;
			if (stable) {
				g->held = true;
				g->press_t = now;
			} else {
				g->held = false;
				if (g->shop_mode) {
					buy(g, now, g->sel);
				} else {
					g->release_t = now;
					tap(g, now);
				}
				g->shop_mode = false;
			}
		}

		if (g->held && now - g->press_t >= HOLD_MS) {
			g->shop_mode = true;
			g->sel = ((now - g->press_t - HOLD_MS) / STEP_MS) % N_ITEMS;
		}

		/* Golden cookie scheduling */
		if ((int32_t)(now - next_golden) >= 0) {
			g->golden_pos = rnd() % ARRAY_SIZE(golden_spots);
			g->golden_until = now + GOLDEN_SHOW_MS;
			next_golden = now + 30000 + rnd() % 30000;
		}

		bool want = led_state(g, now);

		k_mutex_unlock(&game_lock);

		if (want != led_now) {
			gpio_pin_set_dt(&led, want);
			led_now = want;
		}

		k_msleep(10);
	}
}

K_THREAD_DEFINE(logic_tid, 2048, logic_thread, NULL, NULL, NULL, 5, 0, 100);

/* ------------------------------------------------------------------------ */

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(fb),
		.width = W,
		.height = H,
		.pitch = W,
	};
	static struct game snap;

	if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&button) ||
	    !device_is_ready(display)) {
		printk("LED, button or display not ready\n");
		return 0;
	}

	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&button, GPIO_INPUT);

	while (1) {
		k_mutex_lock(&game_lock, K_FOREVER);
		snap = game;
		k_mutex_unlock(&game_lock);

		render(&snap, k_uptime_get_32());
		display_write(display, 0, 0, &desc, fb);
		k_msleep(10);
	}

	return 0;
}

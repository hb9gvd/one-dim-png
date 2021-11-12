/*
 * Arduino 1D Pong Game with (60) WS2812B LEDs
 *
 * Copyright (C) 2015  B.Stultiens
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "Adafruit_NeoPixel.h"
#include "notes.h"

#define NELEM(x)		(sizeof(x) / sizeof((x)[0]))

int sensorPin    =    A0;   // select the input pin for ldr
int sensorValue  =     0;   // variable to store the value coming from the sensor
uint8_t bright;
#define minValue      930   // min. sensor value for lowest brightness
#define maxValue      1020  // max. sensor value for highest brightness
#define minBrightAnim 255   // lowest brightness
#define maxBrightAnim 255   // highest brightness

#define PIN_WSDATA    13  // LED data
#define PIN_BUT_LS    8   // Right start/hit button
#define PIN_BUT_LP    3   // Right power-up button
#define PIN_BUT_RS    4   // Left start/hit button
#define PIN_BUT_RP    5   // Left power-up button
#define PIN_BUT_COIN  2   // Coin pulse
#define PIN_SOUND     9   // Buzzer output (PB1/OC1A)
#define PIN_LED_RP    6   // Right start/hit button light
#define PIN_LED_RS    11  // Right power-up button light
#define PIN_LED_LP    10  // Left power-up button light
#define PIN_LED_LS    7   // Left start/hit button light
#define PIN_COINS_AVAILABLE 12

#define BL_NONE		0xff		// State changes of the button LEDs
#define BL_LS			0x01
#define BL_LP			0x02
#define BL_RS			0x04
#define BL_RP			0x08
#define BL_ALL		0x0f

#define NPIXELS			90		// Number of pixels to handle

#define H_STEPS			1542		// HSV maximum hue +1
#define H_YELLOW		257		// Hue for (hard) yellow color

#define ZONE_SIZE		7		// Bounce-back zone size
#define SHOW_LO			12		// Score dots intensity background
#define SHOW_HI			48		// Score dots intensity foreground
#define WIN_POINTS		5		// Points needed to win
#define TONE_INTERVAL		5		// Not every ball move should give a sound
int CREDIT  =       0;
#define COIN_PULSE  150

Adafruit_NeoPixel one_d = Adafruit_NeoPixel(NPIXELS, PIN_WSDATA, NEO_GRB | NEO_KHZ800);

// Events from buttons and timers
#define EV_BUT_LS_PRESS		0x01
#define EV_BUT_RS_PRESS		0x02
#define EV_BUT_LP_PRESS		0x04
#define EV_BUT_RP_PRESS		0x08
#define EV_COIN_INSERTED           0x10

#define EV_TIMER		0x20
#define EV_TIMEOUT		0x40
#define EV_TONETIMER		0x80

#define TIME_DEBOUNCE		8
#define TIME_IDLE		40
#define TIME_START_TIMEOUT	20000		// Go idle if nothing happens
#define TIME_RESUME_TIMEOUT	7500		// Auto-fire after timeout
#define TIME_BALL_BLINK		150
#define TIME_SPEED_MIN		10
#define TIME_SPEED_INTERVAL	3
#define TIME_POINT_BLINK	233
#define TIME_WIN_BLINK		85
#define TIME_LOCKOUT		250		// Prevent fast button-press to max. 4 times/s

#define TIME_TONE_SERVE		50		// Sound durations
#define TIME_TONE_BOUNCE	50
#define TIME_TONE_MOVE		25
#define TIME_TONE_SCORE		50

enum {
	ST_IDLE = 0,
	ST_START_L,
	ST_START_R,
	ST_MOVE_LR,
	ST_MOVE_RL,
	ST_ZONE_L,
	ST_ZONE_R,
	ST_POINT_L,
	ST_POINT_R,
	ST_RESUME_L,
	ST_RESUME_R,
	ST_WIN_L,
	ST_WIN_R,
};

static uint32_t oldtime;	// Previous' loop millis() value
static uint8_t thestate;	// Game state

static uint8_t bstate_ls;	// Button states
static uint8_t bstate_rs;
static uint8_t bstate_lp;
static uint8_t bstate_rp;
static uint8_t bstate_cn;
static uint8_t debtmr_ls;	// Button debounce timers
static uint8_t debtmr_rs;
static uint8_t debtmr_lp;
static uint8_t debtmr_rp;
static uint8_t debtmr_cn;
static uint16_t timer;		// General timer
static uint16_t timeout;	// Timeout timer (auto-start and goto idle)
static uint16_t tonetimer;	// Tone duration timer
static uint16_t lockout_l;	// Lockout timer to prevent pushing too often
static uint16_t lockout_r;
static uint16_t lock_coin;  // Timer to asure one CREDIT per COIN_PULSE only
static uint8_t ballblinkstate;	// Blinking ball at edge on/off
static uint8_t pointblinkcount;	// Blinking point when a side scores
static uint8_t ballpos;		// Current position of the ball
static uint16_t speed;		// Time between ball moves
static uint8_t speedup;		// Faster and faster replies counter
static uint8_t points_l;	// Score
static uint8_t points_r;
static uint8_t zone_l;		// Hit back zone
static uint8_t zone_r;
static uint8_t boost_l;		// Set if user boosted speed last round
static uint8_t boost_r;
static uint8_t boosted;		// Set if any user boosted until the ball reaches opposite side
static uint8_t tonecount;	// Interval counter for sound during move
static uint8_t tuneidx;		// Index to the running tune

static uint16_t ballh;		// Ball hue when moving boosted

// Twinkling lights when boosted (comet trail parts breaking off)
#define NTWINKLES	(NPIXELS / 8)
static struct {
	uint8_t		v;
	uint8_t 	pos;
	uint16_t	h;
} twinkles[NTWINKLES];

static uint32_t prng_val;	// Local random number generator state

typedef struct __note_t {
	uint8_t		note;
	uint16_t	duration;
} note_t;

/* Tone pitch table for 16MHz crystal */
static const uint16_t tone_pitch[NTONE_PITCH] PROGMEM = {
	61155, 57722, 54482, 51424, 48538, 45814, 43242, 40815, 38524, 36362 /* == 220Hz */, 34321, 32395,
	30577, 28860, 27240, 25711, 24268, 22906, 21620, 20407, 19261, 18180 /* == 440Hz */, 17160, 16197,
	15288, 14429, 13619, 12855, 12133, 11452, 10809, 10203,  9630,  9089 /* == 880Hz */,  8579,  8098,
	 7643,  7214,  6809,  6427,  6066,  5725,  5404,  5101,  4814,  4544,  4289,  4048,
	 3821,  3606,  3404,  3213,  3032,  2862,  2701,  2550,  2406,  2271,  2144,  2023,
	 1910,
};
static const note_t tune_win[] PROGMEM = {
	{ NOTE_Gs6, DUR_1_16 },
	{ NOTE_A6, DUR_1_16 },
	{ NOTE_Gs6, DUR_1_16 },
	{ NOTE_E6, DUR_1_16 },
	{ NOTE_Gs6, DUR_1_16 },
	{ NOTE_A6, DUR_1_16 },
	{ NOTE_Gs6, DUR_1_16 },
	{ NOTE_E6, DUR_1_16 },
	{ 0, DUR_1_8 },
	{ NOTE_D4, DUR_1_8 },
	{ NOTE_D4, DUR_1_8 },
	{ NOTE_B3, DUR_1_8 },
	{ NOTE_E4, DUR_1_8 },
	{ NOTE_D4, DUR_1_4 },
	{ NOTE_B3, DUR_1_4 },
	{ NOTE_D4, DUR_1_8 },
	{ NOTE_D4, DUR_1_8 },
	{ NOTE_B3, DUR_1_8 },
	{ NOTE_E4, DUR_1_8 },
	{ NOTE_D4, DUR_1_4 },
	{ NOTE_B3, DUR_1_4 },
};

#define sound_off()	do { TCCR1A = _BV(COM1A1); /* Set clear output */ } while(0)

/*
 * Local very fast Pseudo Random Number Generator (PRNG)
 * based on a simple LFSR design.
 */
static inline uint8_t lfsr_prng(void)
{
	uint8_t c = prng_val & 1;
	prng_val >>= 1;
	if(c)
		prng_val ^= 0xa6a6a6a6;
	return prng_val;
}

/*
 * Return the current state of a button.
 * Returns non-zero on button pressed.
 */
static inline uint8_t button_is_down(uint8_t pin)
{
	switch(pin) {
	case PIN_BUT_LS:	    return !debtmr_ls && !bstate_ls;
	case PIN_BUT_RS:	    return !debtmr_rs && !bstate_rs;
	case PIN_BUT_LP:	    return !debtmr_lp && !bstate_lp;
	case PIN_BUT_RP:	    return !debtmr_rp && !bstate_rp;
  case PIN_BUT_COIN:    return !debtmr_cn && !bstate_cn;
	}
	return 0;
}

/*
 * Debounce a button and return an event at the rising edge of the detection.
 * The rising edge ensures that there is no delay from pressing the button and
 * the event propagating. It is a prerequisite that the input line is not
 * glitchy.
 * A release event may be generated if the routine is slightly modified.
 */
static inline uint8_t do_debounce(uint8_t tdiff, uint8_t *bstate, uint8_t *debtmr, uint8_t pin, uint8_t ev)
{
	if(0 == *debtmr) {
		uint8_t state = digitalRead(pin);
		if(state != *bstate) {
			*debtmr = TIME_DEBOUNCE;
			if(!(*bstate = state))
				return ev;	// Event on High-to-Low transition of input
			// else
			//  return release_event_value
		}
	} else {
		if(*debtmr >= tdiff)
			*debtmr -= tdiff;
		else
			*debtmr = 0;
	}
	return 0;
}

/*
 * Timer countdown and return an event on timer reaching zero.
 */
static inline uint8_t do_timer(uint8_t tdiff, uint16_t *tmr, uint8_t ev)
{
	if(0 != *tmr) {
		if(*tmr >= tdiff)
			*tmr -= tdiff;	// Timer countdown
		else
			*tmr = 0;
		// Set event when done counting
		if(0 == *tmr)
			return ev;
	}
	return 0;
}

/*
 * Set the speaker to generate a tone
 */
static inline void set_tone(uint16_t note, uint16_t duration)
{
	tonetimer = duration;
	if(note && note <= NTONE_PITCH) {
		OCR1A = pgm_read_word(&tone_pitch[note-1]);
		TCCR1A = _BV(COM1A0);	/* Set toggle output */
		TCNT1 = 0;
	} else
		sound_off();
}

/*
 * Play the next note of the tune
 */
static inline void tune_next()
{
	if(tuneidx < NELEM(tune_win)) {
		uint16_t n = pgm_read_byte(&tune_win[tuneidx].note);
		uint16_t d = pgm_read_word(&tune_win[tuneidx].duration);
		set_tone(n, d);
		tuneidx++;
	} else
		set_tone(0, 0);
}

/*
 * Draw the left and right zones where the ball may be hit back.
 */
static void draw_sides()
{
	for(uint8_t i = 0; i < zone_l-1; i++) {
		one_d.setPixelColor(i, 0, 64, 64);
	}
	one_d.setPixelColor(0, 0, 64, 64);
	for(uint8_t i = 0; i < zone_r-1; i++) {
		one_d.setPixelColor(NPIXELS-1-i, 0, 64, 64);
	}
	one_d.setPixelColor(NPIXELS-1, 0, 64, 64);
}

/*
 * Draw the ball with a tail of five pixels in diminishing intensity.
 * The ball changes color when boosted and there is a chance of parts "falling
 * off" which will twinkle for a while.
 */
static void draw_ball(int8_t dir, uint8_t pos)
{
	uint8_t c = 255;
	uint8_t tail;

	/* Adjust the tail-length to the speed of the ball.
	 * We use 'timer' here because a boost changes the speed and the timer
	 * is set before we draw the ball.
	 */
	if(timer <= 3)
		tail = 7;	// High speed, long tail
	else if(timer <= 12)
		tail = 6;
	else
		tail = 5;	// Default to 5 pixels

	if(boosted) {
		ballh += 75;	// About three times round the circle on the course
		if(ballh >= H_STEPS)
			ballh -= H_STEPS;
		/* 1 in 4 chance of adding a twinkle */
		if(!(lfsr_prng() & 0x03)) {
			for(uint8_t i = 0; i < NELEM(twinkles); i++) {
				if(!twinkles[i].v) {
					/* Add to first free slot */
					twinkles[i].v = 255;
					twinkles[i].pos = pos;
					twinkles[i].h = ballh;
					break;
				}
			}
		}
		/* Draw color ball */
		for(uint8_t i = 0; i < tail && pos >= 0 && pos < NPIXELS; i++) {
			one_d.setPixelColorHsv(pos, ballh, 255, c);
			c >>= 1;
			pos -= dir;
		}
		/* Add twinkling lights */
		for(uint8_t i = 0; i < NELEM(twinkles); i++) {
			if(twinkles[i].v) {
				one_d.setPixelColorHsv(twinkles[i].pos, twinkles[i].h, 128, twinkles[i].v);
				/* 1 in 8 chance to increase the intensity */
				if(!(lfsr_prng() & 0x07)) {
					twinkles[i].v <<= 4;
					twinkles[i].v |= 0x0f;
				} else {
					twinkles[i].v >>= 1;
				}
			}
		}
	} else {
		for(uint8_t i = 0; i < tail && pos >= 0 && pos < NPIXELS; i++) {
			one_d.setPixelColor(pos, c, c, 0);
			c >>= 1;
			pos -= dir;
		}
	}

}

/*
 * Draw the playing field consisting of the zones and the points scored so far.
 */
static void draw_course(uint8_t v)
{
	one_d.clear();
	draw_sides();
	if(v) {
		for(uint8_t i = 0; i < points_l; i++) {
			one_d.setPixelColor(NPIXELS/2-1-(2*i+0), v, 0, 0);
			one_d.setPixelColor(NPIXELS/2-1-(2*i+1), v, 0, 0);
		}
		for(uint8_t i = 0; i < points_r; i++) {
			one_d.setPixelColor(NPIXELS/2+(2*i+0), 0, v, 0);
			one_d.setPixelColor(NPIXELS/2+(2*i+1), 0, v, 0);
		}
	}
}

/*
 * Animate the game idle situation with following content:
 * - A rainbow pattern
 * - Ball bouncing left-right-left-right
 * - Score animation
 */
static uint16_t ai_h;
static uint8_t ai_state;
static uint8_t ai_pos;

static void animate_idle_init(void)
{
	ai_h = 0;
	ai_state = 0;
}

static void animate_idle(void)
{
	switch(ai_state) {
	case 0:
	case 1:
	case 2:
	case 3:
		/* Rainbow pattern */
		for(uint8_t i = 0; i < NPIXELS; i++) {
			uint16_t h = ai_h + (i << 4);
			if(h >= H_STEPS)
				h -= H_STEPS;
			one_d.setPixelColorHsv(i, h, 255, 128);
		}
		ai_h += H_STEPS/60;
		if(ai_h >= H_STEPS) {
			ai_h -= H_STEPS;
			ai_pos = 0;
			ai_state++;
		}
		break;
	case 4:
	case 6:
		/* Ball left-to-right */
		draw_course(0);
		draw_ball(1, ai_pos++);
		if(ai_pos >= NPIXELS) {
			ai_state++;
		}
		break;
	case 5:
	case 7:
		/* Ball right-to-left */
		draw_course(0);
		draw_ball(-1, --ai_pos);
		if(!ai_pos) {
			ai_state++;
		}
		break;
	case 8:
	case 10:
		/* Score blinkenlights */
		sensorValue = analogRead(sensorPin);
    bright=map(constrain(sensorValue,minValue,maxValue),minValue,maxValue,minBrightAnim,maxBrightAnim);
    draw_course(0);
    		for(uint8_t i = 0; i < ai_pos; i++) {
			one_d.setPixelColor(NPIXELS/2-1-i, bright, 0, 0);
			one_d.setPixelColor(NPIXELS/2+i, 0, bright, 0);
		}
		if(++ai_pos >= NPIXELS/2) {
			ai_state++;
			ai_pos = 0;
		}
		break;

	case 9:
	case 11:
    sensorValue = analogRead(sensorPin);
    bright=map(constrain(sensorValue,minValue,maxValue),minValue,maxValue,minBrightAnim,maxBrightAnim);
		draw_course(0);
		for(uint8_t i = 0; i < NPIXELS/2-ai_pos; i++) {
			one_d.setPixelColor(NPIXELS/2-1-i, bright, 0, 0);
			one_d.setPixelColor(NPIXELS/2+i, 0, bright, 0);
		}
		if(++ai_pos >= NPIXELS/2) {
			ai_state++;
			ai_pos = 0;
		}
		break;

	default:
		ai_state = 0;
		break;
	}
	one_d.show();
}

/*
 * Animate a winner. Flash the winning side's points.
 */
static uint8_t aw_state;
static void animate_win_init()
{
	aw_state = 0;
}

static uint8_t animate_win(uint8_t side)
{
	uint32_t clr;
	uint8_t pos;
  sensorValue = analogRead(sensorPin);
  bright=map(constrain(sensorValue,minValue,maxValue),minValue,maxValue,minBrightAnim,maxBrightAnim);
  if(side) {
		clr = Adafruit_NeoPixel::Color(0, bright, 0);
		pos = NPIXELS/2;
	} else {
		clr = Adafruit_NeoPixel::Color(bright, 0, 0);
		pos = 0;
	}

	one_d.clear();
	if(aw_state < 20) {
		if(aw_state & 0x01) {
			for(uint8_t i = 0; i < NPIXELS/2; i++) {
				one_d.setPixelColor(pos+i, clr);
			}
		}
	} else if(aw_state < 50) {
		for(uint8_t i = 0; i < aw_state - 20; i++) {
			one_d.setPixelColor(pos+i, clr);
		}
	} else if(aw_state < 80) {
		for(uint8_t i = aw_state - 50; i < NPIXELS/2; i++) {
			one_d.setPixelColor(pos+i, clr);
		}
	} else if(aw_state < 110) {
		for(uint8_t i = 0; i < aw_state - 80; i++) {
			one_d.setPixelColor(NPIXELS/2-1-i+pos, clr);
		}
	} else if(aw_state < 140) {
		for(uint8_t i = aw_state - 110; i < NPIXELS/2; i++) {
			one_d.setPixelColor(NPIXELS/2-1-i+pos, clr);
		}
	}
	one_d.show();
	return ++aw_state < 140;
}

/*
 * Active game states suppress fast button pushes
 */
static uint8_t is_game_state(uint8_t s)
{
	switch(s) {
	case ST_MOVE_LR:	// If you press too soon
	case ST_MOVE_RL:
	case ST_ZONE_R:		// In the zone
	case ST_ZONE_L:
	case ST_POINT_L:	// Just got a point, delay resume
	case ST_POINT_R:
	case ST_WIN_R:		// Delay to activate the win sequence
	case ST_WIN_L:
		return 1;
	default:
		return 0;
	}
}

/*
 * Set the timer to the speed of the ball and the current boost-state
 */
static inline void speed_to_timer()
{
	if(boosted)
		timer = speed * 3 / 4;
	else
		timer = speed;
	if(timer < 2)
		timer = 2;
}

/*
 * Show a given set of leds on the buttons
 * Handle special cases when buttons are inactive.
 * Note: outputs are active low.
 */
static void button_leds_show(uint8_t l)
{
	digitalWrite(PIN_LED_RS, !(l & BL_LS) || lockout_l);
	digitalWrite(PIN_LED_LP, !(l & BL_LP) || zone_l <= 1);
	digitalWrite(PIN_LED_LS, !(l & BL_RS) || lockout_r);
	digitalWrite(PIN_LED_RP, !(l & BL_RP) || zone_r <= 1);
  if((CREDIT))
  {
    digitalWrite(PIN_COINS_AVAILABLE,HIGH);
  }
  else
  {
    digitalWrite(PIN_COINS_AVAILABLE,LOW);
  }
}

/*
 * Set the LEDs on the buttons given a game state
 */
static void button_leds_set(void)
{
	switch(thestate) {
  case ST_IDLE:
    if(CREDIT){
      button_leds_show(BL_LP|BL_RP);
    }
    else {
      button_leds_show(BL_NONE);
    }
    break;

    
	case ST_WIN_L:
	case ST_WIN_R:
    if(CREDIT){
      button_leds_show(BL_LP|BL_RP);
    }
    else {
      button_leds_show(BL_NONE);
    }
		break;

	case ST_MOVE_LR:
		button_leds_show(BL_RP);
		break;

	case ST_MOVE_RL:
		button_leds_show(BL_LP);
		break;

	case ST_ZONE_L:
		button_leds_show(BL_LS|BL_LP);
		break;

	case ST_RESUME_L:
	case ST_POINT_L:
	case ST_START_L:
		button_leds_show(BL_LP);
		break;

	case ST_ZONE_R:
 //thomas changed this button_leds_show(BL_LS|BL_RS);
		button_leds_show(BL_RS|BL_RP);
		break;

	case ST_RESUME_R:
	case ST_POINT_R:
	case ST_START_R:
		button_leds_show(BL_RP);
		break;
	}
}

/*
 * State transition routine. Setup prerequisites for the new state to function
 * properly.
 * - Handle a state's exit actions
 * - Handle a state's entry actions
 */
static void set_state(uint8_t newstate)
{
	/* State exit actions */
	switch(thestate) {
	case ST_IDLE:
	case ST_WIN_L:
	case ST_WIN_R:
		points_l = points_r = 0;
		boost_l = boost_r = 0;
		zone_l = zone_r = ZONE_SIZE;
		speedup = 0;
		boosted = 0;
		break;

	case ST_START_L:
	case ST_POINT_L:
	case ST_RESUME_L:
		ballpos = 0;
		/* Serve speed not too fast */
		speed = TIME_SPEED_MIN + 5*TIME_SPEED_INTERVAL;
		speedup = 0;
		break;

	case ST_START_R:
	case ST_POINT_R:
	case ST_RESUME_R:
		ballpos = NPIXELS-1;
		/* Serve speed not too fast */
		speed = TIME_SPEED_MIN + 5*TIME_SPEED_INTERVAL;
		speedup = 0;
		break;

	case ST_ZONE_L:
		/* Calculate the speed for the return */
		speed = TIME_SPEED_MIN + TIME_SPEED_INTERVAL * ballpos;
		if(++speedup / 2 >= speed)
			speed = 2;
		else
			speed -= speedup / 2;
		boosted = 0;
		break;

	case ST_ZONE_R:
		/* Calculate the speed for the return */
		speed = TIME_SPEED_MIN + TIME_SPEED_INTERVAL * (NPIXELS-1 - ballpos);
		if(++speedup / 2 >= speed)
			speed = 2;
		else
			speed -= speedup / 2;
		boosted = 0;
		break;
	}

	thestate = newstate;
	/* State entry actions */
	switch(thestate) {
	case ST_IDLE:
		memset(twinkles, 0, sizeof(twinkles));
		boost_l = boost_r = 0;
		zone_l = zone_r = ZONE_SIZE;
		animate_idle_init();
		timer = TIME_IDLE;
		set_tone(0, 0);		// In case we enter idle in strange ways, kill any sound
		break;

	case ST_START_L:
	case ST_START_R:
		memset(twinkles, 0, sizeof(twinkles));
		draw_course(SHOW_HI);
		one_d.show();
		timer = TIME_BALL_BLINK;
		timeout = TIME_START_TIMEOUT;
		ballblinkstate = 0;
		ballpos = thestate == ST_START_L ? 0 : NPIXELS-1;
		break;

	case ST_MOVE_LR:
	case ST_MOVE_RL:
		speed_to_timer();
		tonecount = TONE_INTERVAL;
		break;

	case ST_POINT_L:
	case ST_POINT_R:
		memset(twinkles, 0, sizeof(twinkles));
		pointblinkcount = 7;
		/* Recover the zone next round */
		if(!boost_l && zone_l < ZONE_SIZE)
			zone_l++;
		if(!boost_r && zone_r < ZONE_SIZE)
			zone_r++;
		timer = TIME_POINT_BLINK;
		if(boost_l)
			boost_l--;
		if(boost_r)
			boost_r--;
		// Ensure we get to the score display before continuing
		lockout_l  = lockout_r = TIME_LOCKOUT;
		break;

	case ST_RESUME_L:
	case ST_RESUME_R:
		draw_course(SHOW_HI);
		one_d.show();
		timer = TIME_BALL_BLINK;
		timeout = TIME_RESUME_TIMEOUT;
		ballblinkstate = 0;
		break;

	case ST_WIN_L:
	case ST_WIN_R:
		// Ensure we get to the winner display before continuing
		lockout_l  = lockout_r = 2 * TIME_LOCKOUT;
		animate_win_init();
		timer = TIME_WIN_BLINK;
		tuneidx = 0;
		tune_next();
		break;
	}
}

/*
 * Arduino setup
 */
void setup()
{
	PORTB = PORTC = PORTD = 0xff;	// Enable all pull-ups so we don't have undef inputs hanging

	pinMode(PIN_BUT_LS, INPUT_PULLUP);	// Buttons input
	pinMode(PIN_BUT_RS, INPUT_PULLUP);
	pinMode(PIN_BUT_LP, INPUT_PULLUP);
	pinMode(PIN_BUT_RP, INPUT_PULLUP);
	digitalWrite(PIN_SOUND, 0);
	pinMode(PIN_SOUND, OUTPUT);
	pinMode(PIN_LED_LS, OUTPUT);		// Button LEDs output
	pinMode(PIN_LED_LP, OUTPUT);
	pinMode(PIN_LED_RS, OUTPUT);
	pinMode(PIN_LED_RP, OUTPUT);
  pinMode(PIN_COINS_AVAILABLE, OUTPUT);
  digitalWrite(PIN_COINS_AVAILABLE, LOW);
  Serial.begin(9600); //sets serial port for communication
 
	one_d.begin();		// Setup IO
	one_d.show();		// All leds off

  /*
   * Setup sound hardware with Timer1 manually. The disabled interrupts
   * in the pixel-update causes interference in the timing resulting in
   * clicks in the sound output.
   */
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS10);
  OCR1A = NOTE_C4;  // Just a value
  TCNT1 = 0;

  prng_val = 0x31415926;  // PRNG init with Pi

  
 thestate = ST_IDLE;
 set_state(thestate);	// To run both exit and entry actions

}

/*
 * Main program, called constantly and forever.
 *
 * - Handle timing and generate events
 * - Run the game's state machine
 */
#define chk_ev(ev)	(events & (ev))

void loop()
{
	uint32_t now;
	uint8_t tdiff = (now = millis()) - oldtime;
	uint8_t events = 0;
      
	/* Handle buttons and timers on (just about) every millisecond */
	if(tdiff) {
		oldtime = now;
		events |= do_debounce(tdiff, &bstate_ls, &debtmr_ls, PIN_BUT_LS, EV_BUT_LS_PRESS);
		events |= do_debounce(tdiff, &bstate_rs, &debtmr_rs, PIN_BUT_RS, EV_BUT_RS_PRESS);
		events |= do_debounce(tdiff, &bstate_lp, &debtmr_lp, PIN_BUT_LP, EV_BUT_LP_PRESS);
		events |= do_debounce(tdiff, &bstate_rp, &debtmr_rp, PIN_BUT_RP, EV_BUT_RP_PRESS);
    events |= do_debounce(tdiff, &bstate_cn, &debtmr_cn, PIN_BUT_COIN,   EV_COIN_INSERTED);
		events |= do_timer(tdiff, &timer, EV_TIMER);
		events |= do_timer(tdiff, &timeout, EV_TIMEOUT);
		events |= do_timer(tdiff, &tonetimer, EV_TONETIMER);
		do_timer(tdiff, &lockout_l, 0);
		do_timer(tdiff, &lockout_r, 0);
    do_timer(tdiff, &lock_coin, 0);
	}

	if(is_game_state(thestate)) {
		// If the lockout timer is running, squash the button event
		if(lockout_l){
			events &= ~EV_BUT_LS_PRESS;}
		if(lockout_r){
			events &= ~EV_BUT_RS_PRESS;}
	}

	// A button press activates the lockout timer
	if(chk_ev(EV_BUT_LS_PRESS)){
		lockout_l = TIME_LOCKOUT;}
	if(chk_ev(EV_BUT_RS_PRESS)){
		lockout_r = TIME_LOCKOUT;}


  // If the COIN_PULSE timer is running, squash the COINevent
  if(lock_coin){
      events &= ~EV_COIN_INSERTED;
  }

  // A Coinpulse activates the lock_coin timer
  if(chk_ev(EV_COIN_INSERTED)){
    lock_coin = COIN_PULSE;
  }
	
	// Show indicators on buttons which are active
	button_leds_set();
  // In every state check, if coins are inserted so check it before entereing the state machine logic below
  
  if(chk_ev(EV_COIN_INSERTED)) {
      ++CREDIT;
    }  

	switch(thestate) {
	case ST_IDLE:
  // If CREDIT available, decrease CREDIT and start game
		if(chk_ev(EV_BUT_LS_PRESS) && (CREDIT)) {
    	--CREDIT;
			set_state(ST_START_L);
    } else if(chk_ev(EV_BUT_RS_PRESS) && (CREDIT)) {
    	--CREDIT;
			set_state(ST_START_R);
		} else if(chk_ev(EV_TIMER)) {
			timer = TIME_IDLE;
			animate_idle();
		}
		break;

	// Game is started, waiting for left player to serve the ball
	case ST_START_L:
  	if(chk_ev(EV_BUT_LS_PRESS)) {
			set_state(ST_MOVE_LR);
		} else if(chk_ev(EV_TIMEOUT)) {
			set_state(ST_IDLE);
		} else if(chk_ev(EV_TIMER)) {
			timer = TIME_BALL_BLINK;
			if(ballblinkstate)
				one_d.setPixelColor(ballpos, 255, 128, 0);
			else
				one_d.setPixelColor(ballpos, 0, 0, 0);
			one_d.show();
			ballblinkstate = !ballblinkstate;
		}
		break;

	// Game is started, waiting for right player to serve the ball
	case ST_START_R:
		if(chk_ev(EV_BUT_RS_PRESS)) {
			set_state(ST_MOVE_RL);
		} else if(chk_ev(EV_TIMEOUT)) {
			set_state(ST_IDLE);
		} else if(chk_ev(EV_TIMER)) {
			timer = TIME_BALL_BLINK;
			if(ballblinkstate)
				one_d.setPixelColor(ballpos, 255, 128, 0);
			else
				one_d.setPixelColor(ballpos, 0, 0, 0);
			one_d.show();
			ballblinkstate = !ballblinkstate;
		}
		break;

	case ST_MOVE_LR:
  	if(chk_ev(EV_TIMER)) {
			if(!--tonecount) {
				set_tone(NOTE_G4, TIME_TONE_MOVE);
				tonecount = TONE_INTERVAL;
			}
			speed_to_timer();
			draw_course(SHOW_LO);
			draw_ball(1, ballpos);
			one_d.show();
			ballpos++;
			if(NPIXELS-1 - ballpos <= zone_r)
				set_state(ST_ZONE_R);
		}
		break;

	// Ball is moving right-to-left outside the playback zone
	case ST_MOVE_RL:
		if(chk_ev(EV_TIMER)) {
			if(!--tonecount) {
				set_tone(NOTE_G4, TIME_TONE_MOVE);
				tonecount = TONE_INTERVAL;
			}
			speed_to_timer();
			draw_course(SHOW_LO);
			draw_ball(-1, ballpos);
			one_d.show();
			ballpos--;
			if(ballpos <= zone_l)
				set_state(ST_ZONE_L);
		}
		break;
	case ST_ZONE_L:
		if(chk_ev(EV_BUT_LS_PRESS)) {
			set_tone(NOTE_G3, TIME_TONE_BOUNCE);
			set_state(ST_MOVE_LR);
			// Changing speed is done after the state-change's exit/entry action
			if(zone_l > 1 && button_is_down(PIN_BUT_LP)) {
				zone_l--;
				boosted = 1;
				ballh = H_YELLOW;
				speed_to_timer();
				boost_l++;
			}
		} else if(chk_ev(EV_TIMER)) {
			if(!ballpos) {
				set_tone(NOTE_C5, TIME_TONE_SCORE);
				if(++points_r >= WIN_POINTS)
					set_state(ST_WIN_R);
				else
					set_state(ST_POINT_R);
			} else {
				speed_to_timer();
				ballpos--;
			}
			draw_course(SHOW_LO);
			draw_ball(-1, ballpos);
			one_d.show();
		}
		break;

	// Ball is in the right playback zone, waiting for hit/score
	case ST_ZONE_R:
		if(chk_ev(EV_BUT_RS_PRESS)) {
			set_tone(NOTE_G3, TIME_TONE_BOUNCE);
			set_state(ST_MOVE_RL);
			// Changing speed is done after the state-change's exit/entry action
			if(zone_r > 1 && button_is_down(PIN_BUT_RP)) {
				zone_r--;
				speed_to_timer();
				boosted = 1;
				ballh = H_YELLOW;
				boost_r++;
			}
		} else if(chk_ev(EV_TIMER)) {
			if(ballpos == NPIXELS-1) {
				set_tone(NOTE_C5, TIME_TONE_SCORE);
				if(++points_l >= WIN_POINTS)
					set_state(ST_WIN_L);
				else
					set_state(ST_POINT_L);
			} else {
				speed_to_timer();
				ballpos++;
			}
			draw_course(SHOW_LO);
			draw_ball(1, ballpos);
			one_d.show();
		}
		break;

	// Left player scored, animate point
	case ST_POINT_L:
		if(chk_ev(EV_BUT_LS_PRESS)) {
			set_state(ST_RESUME_L);
		} else if(chk_ev(EV_TIMER)) {
			timer = TIME_POINT_BLINK;
			draw_course(SHOW_HI);
			if(!(pointblinkcount & 0x01)) {
				one_d.setPixelColor(NPIXELS/2-1-(2*(points_l-1)+0), 0, 0, 0);
				one_d.setPixelColor(NPIXELS/2-1-(2*(points_l-1)+1), 0, 0, 0);
			} else {
				one_d.setPixelColor(NPIXELS/2-1-(2*(points_l-1)+0), 255, 0, 0);
				one_d.setPixelColor(NPIXELS/2-1-(2*(points_l-1)+1), 255, 0, 0);
			}
			one_d.show();
			if(!--pointblinkcount)
				set_state(ST_RESUME_L);
		}
		break;

	// Right player scored, animate point
	case ST_POINT_R:
		if(chk_ev(EV_BUT_RS_PRESS)) {
			set_state(ST_RESUME_R);
		} else if(chk_ev(EV_TIMER)) {
			timer = TIME_POINT_BLINK;
			draw_course(SHOW_HI);
			if(!(pointblinkcount & 0x01)) {
				one_d.setPixelColor(NPIXELS/2+(2*(points_r-1)+0), 0, 0, 0);
				one_d.setPixelColor(NPIXELS/2+(2*(points_r-1)+1), 0, 0, 0);
			} else {
				one_d.setPixelColor(NPIXELS/2+(2*(points_r-1)+0), 0, 255, 0);
				one_d.setPixelColor(NPIXELS/2+(2*(points_r-1)+1), 0, 255, 0);
			}
			one_d.show();
			if(!--pointblinkcount)
				set_state(ST_RESUME_R);
		}
		break;

	// Left player previously scored and must serve again (or timeout to auto-serve)
	case ST_RESUME_L:
		if(chk_ev(EV_BUT_LS_PRESS | EV_TIMEOUT)) {
			set_state(ST_MOVE_LR);
			set_tone(NOTE_F3, TIME_TONE_SERVE);
		} else if(chk_ev(EV_TIMER)) {
			timer = TIME_BALL_BLINK;
			if(ballblinkstate)
				one_d.setPixelColor(ballpos, 255, 128, 0);
			else
				one_d.setPixelColor(ballpos, 0, 0, 0);
			one_d.show();
			ballblinkstate = !ballblinkstate;
		}
		break;

	// Right player previously scored and must serve again (or timeout to auto-serve)
	case ST_RESUME_R:
		if(chk_ev(EV_BUT_RS_PRESS | EV_TIMEOUT)) {
			set_state(ST_MOVE_RL);
			set_tone(NOTE_F3, TIME_TONE_SERVE);
		} else if(chk_ev(EV_TIMER)) {
			timer = TIME_BALL_BLINK;
			if(ballblinkstate)
				one_d.setPixelColor(ballpos, 255, 128, 0);
			else
				one_d.setPixelColor(ballpos, 0, 0, 0);
			one_d.show();
			ballblinkstate = !ballblinkstate;
		}
		break;

	// A player won the game, animate the winning side
	case ST_WIN_L:
	case ST_WIN_R:
		if(chk_ev(EV_TONETIMER)) {
			events &= ~EV_TONETIMER;	// Remove the event so we don't get messed up with a set_tone(0, 0) below call
			tune_next();
		}
		if(chk_ev(EV_BUT_LS_PRESS) && (CREDIT)) {
      --CREDIT;
			set_state(ST_START_L);
		} else if(chk_ev(EV_BUT_RS_PRESS) && (CREDIT)) {
      --CREDIT;
			set_state(ST_START_R);
		} else if(chk_ev(EV_TIMER)) {
			timer = TIME_WIN_BLINK;
			if(!animate_win(thestate == ST_WIN_R)){
        set_state(ST_IDLE);
      }
		}
		break;

	// If we get confused, start at idle...
	default:
		set_state(ST_IDLE);
		break;
	}

	/* The sound timer is async to the rest */
	/* Alternative is to handle it in each and every state */
	if(chk_ev(EV_TONETIMER))
		set_tone(0, 0);

}

// vim: syn=cpp

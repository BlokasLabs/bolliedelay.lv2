/**
    Bollie Delay - (c) 2016 Thomas Ebeling https://ca9.eu

    This file is part of bolliedelay.lv2

    bolliedelay.lv2 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    bolliedelay.lv2 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
* \file bollie-delay.c
* \author Bollie
* \date 05 Nov 2016
* \brief An LV2 tempo delay plugin with filters and tapping.
*/

#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include "bolliefilter.h"

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define URI "https://ca9.eu/lv2/bolliedelay"

#define MAX_TAPE_LEN 1920001


/**
* Make a bool type available. ;)
*/
typedef enum { false, true } bool;


/**
* Enumeration of LV2 ports
*/
typedef enum {
    BDL_TEMPO_HOST  = 0,
    BDL_TEMPO_USER  = 1,
    BDL_TEMPO_MODE  = 2,
    BDL_TAP         = 3,
    BDL_MIX         = 4,
    BDL_FEEDBACK    = 5,
    BDL_CROSSF      = 6,
    BDL_LOW_ON      = 7,
    BDL_LOW_F       = 8,
    BDL_LOW_Q       = 9,
    BDL_HIGH_ON     = 10,
    BDL_HIGH_F      = 11,
    BDL_HIGH_Q      = 12,
    BDL_DIV_L       = 13,
    BDL_DIV_R       = 14,
    BDL_INPUT_L     = 15,
    BDL_INPUT_R     = 16,
    BDL_OUTPUT_L    = 17,
    BDL_OUTPUT_R    = 18,
    BDL_TEMPO_OUT   = 19,
} PortIdx;

/**
* Struct for THE BollieDelay instance, the host is going to use.
*/
typedef struct {
    const float* tempo_host;    ///< Tempo in BPM from host
    const float* tempo_user;    ///< Tempo in BPM set by user
    const float* tempo_mode;    ///< Tempo mode 0=host, 1=user
    float* tempo_out;           ///< For displaying the current tempo on the UI
    const float* tap;           ///< Control port for tapping
    const float* mix;           ///< mix/blend in percentage
    const float* feedback;      ///< feedback in percentage
    const float* crossf;        ///< crossfeed in percentage between inputs
    const float* low_on;        ///< LCF: 0=off, 1=on
    const float* low_f;         ///< LCF cut off frequency
    const float* low_q;         ///< LCF quality
    const float* high_on;       ///< HCF: 0=off, 1=on
    const float* high_f;        ///< HCF cuf off frequency
    const float* high_q;        ///< HCF quality
    const float* div_l;         ///< Divider enum "left"(0) input
    const float* div_r;         ///< Divider enum "right"(1) input
    const float* input_l;       ///< input0, left side
    const float* input_r;       ///< input1, right side
    float* output_l;            ///< output1, left side
    float* output_r;            ///< output2, right side

    double rate;                ///< Current sample rate

    float buffer_l[MAX_TAPE_LEN];   ///< delay buffer left
    float buffer_r[MAX_TAPE_LEN];   ///< delay buffer right

    BollieFilter filter_low_l;      ///< LCF left
    BollieFilter filter_low_r;      ///< LCF right
    BollieFilter filter_high_l;     ///< HCF left
    BollieFilter filter_high_r;     ///< HCF right

    float tempo_tap;    ///< storing tapped tempo
    float cur_tempo;    ///< state variable for current tempo set by tempo (above)
    float cur_div_l;    ///< state var for current division, left side
    float cur_div_r;    ///< state var for current division, right side
    double cur_d_t_ch1; ///< current delay time
    double cur_d_t_ch2; ///< current delay time
    int pos_w;          ///< current write position, left side
    int start_tap;      ///< when did the last tap happen (ms since epoch)
    float dry_gain;     ///< current state leading towards target dry gain
    float wet_gain;     ///< current state leading towards target wet gain
    float cur_feedback; ///< current state leading towards target feedback gain
    float cur_crossf;   ///< current state leading towards target crossfeed gain
    double tgt_d_t_ch1; ///< target delay time
    double tgt_d_t_ch2; ///< target delay time
} BollieDelay;


/**
* Instantiates the plugin
* Allocates memory for the BollieDelay object and returns a pointer as
* LV2Handle.
*/
static LV2_Handle instantiate(const LV2_Descriptor * descriptor, double rate,
    const char* bundle_path, const LV2_Feature* const* features) {
    
    BollieDelay *self = (BollieDelay*)calloc(1, sizeof(BollieDelay));

    // Memorize sample rate for calculation
    self->rate = rate;

    return (LV2_Handle)self;
}


/**
* Used by the host to connect the ports of this plugin.
* \param instance current LV2_Handle (will be cast to BollieDelay*)
* \param port LV2 port index, maches the enum above.
* \param data Pointer to the actual port data.
*/
static void connect_port(LV2_Handle instance, uint32_t port, void *data) {
    BollieDelay *self = (BollieDelay*)instance;

    switch ((PortIdx)port) {
        case BDL_TEMPO_HOST:
            self->tempo_host = data;
            break;
        case BDL_TEMPO_USER:
            self->tempo_user = data;
            break;
        case BDL_TEMPO_MODE:
            self->tempo_mode = data;
            break;
        case BDL_TAP:
            self->tap = data;
            break;
        case BDL_MIX:
            self->mix = data;
            break;
        case BDL_FEEDBACK:
            self->feedback = data;
            break;
        case BDL_CROSSF:
            self->crossf = data;
            break;
        case BDL_LOW_ON:
            self->low_on = data;
            break;
        case BDL_LOW_F:
            self->low_f = data;
            break;
        case BDL_LOW_Q:
            self->low_q = data;
            break;
        case BDL_HIGH_ON:
            self->high_on = data;
            break;
        case BDL_HIGH_F:
            self->high_f = data;
            break;
        case BDL_HIGH_Q:
            self->high_q = data;
            break;
        case BDL_DIV_L:
            self->div_l = data;
            break;
        case BDL_DIV_R:
            self->div_r = data;
            break;
        case BDL_INPUT_L:
            self->input_l = data;
            break;
        case BDL_INPUT_R:
            self->input_r = data;
            break;
        case BDL_OUTPUT_L:
            self->output_l = data;
            break;
        case BDL_OUTPUT_R:
            self->output_r = data;
            break;
        case BDL_TEMPO_OUT:
            self->tempo_out = data;
            break;
    }
}
    

/**
* This has to reset all the internal states of the plugin
* \param instance pointer to current plugin instance
*/
static void activate(LV2_Handle instance) {
    BollieDelay* self = (BollieDelay*)instance;
    // Let's remove all that noise
    for (int i = 0 ; i < MAX_TAPE_LEN ; ++i) {
        self->buffer_l[i] = 0;
        self->buffer_r[i] = 0;
    }

    // Initialize number of samples needed
    self->cur_d_t_ch1 = 0;
    self->cur_d_t_ch2 = 0;
    self->tgt_d_t_ch1 = 0;
    self->tgt_d_t_ch2 = 0;

    // Clear the filters
    bf_reset(&self->filter_low_l);
    bf_reset(&self->filter_low_r);
    bf_reset(&self->filter_high_l);
    bf_reset(&self->filter_high_r);

    // Reset the positions & state variables
    self->pos_w = 0;
    self->cur_tempo = 0;
    self->cur_div_l = 0;
    self->cur_div_r = 0;
    self->dry_gain = 0;
    self->wet_gain = 0;

    // Reset tapping
    self->start_tap = 0;
    self->tempo_tap = 120;
}


/**
* Handles a tap on the tap button and calculates time differences
* \param self pointer to current plugin instance
* \return Beats per minute or zero if it didn't work
*/
static float handle_tap(BollieDelay* self) {

    float d = 0;

    struct timeval t_cur;
    gettimeofday(&t_cur, 0);

    // convert it to milliseconds
    long int t_cur_ms = floor(t_cur.tv_sec * 1000 + t_cur.tv_usec / 1000);

    // If start tap is memorized, do some calculations
    if (self->start_tap) {
        d = t_cur_ms - self->start_tap;

        // Reset if we exceed the maximum delay time
        if (d <= 50 || d > 10000 ) {
            d = 0;
        }
    }
    self->start_tap = t_cur_ms;
    return (d > 0 ? 60000 / d : 0);   // convert to bpm
}


/**
* Calculates number of samples used for divided delay times.
* \param self pointer to current plugin instance.
* \param tempo Tempo in BPM
* \param div   Divider
* \return number of samples needed for the delay buffer
* \todo divider enum
*/
static double calc_delay_samples(BollieDelay* self, float tempo, int div) {
    // Calculate the samples needed 
    double d = 60 / tempo * self->rate;
    switch(div) {
        case 1:
        d = d * 2/3;
            break;
        case 2:
        d = d / 2;
            break;
        case 3:
        d = d / 4 * 3;
            break;
        case 4:
        d = d / 3;
            break;
        case 5:
            d = d / 4;
            break;
    }
    return d;
}

/**
* linear sample interpolation from buffer
* \param buf pointer to the buffer
* \param x sample coordinate. Can be also negative.
* \return interpolated sample
*/
static float interpolate(float *buf, double x) {
    if (x < 0) x += MAX_TAPE_LEN;
    if (x >= MAX_TAPE_LEN) x -= MAX_TAPE_LEN;
    int32_t x0 = (int32_t)x;
    float frac = x - (double)x0;
    int32_t x1 = x0+1;
    return buf[x0]  + frac * (buf[x1 >= MAX_TAPE_LEN ? 0 : x1] - buf[x0]);
}

/**
* Main process function of the plugin.
* \param instance  handle of the current plugin
* \param n_samples number of samples in this current input block.
*/
static void run(LV2_Handle instance, uint32_t n_samples) {
    BollieDelay* self = (BollieDelay*)instance;

    // First some TAP handling
    if (*(self->tap) > 0) {
        float d = handle_tap(self);
        if (d > 0) 
            self->tempo_tap = (float)d;
    }

    // Handle tempo mode
    float tempo = *self->tempo_host; 
    switch ((int)(*self->tempo_mode)) {
        case 1:
            tempo = *self->tempo_user;
            break;
        case 2:
            tempo = self->tempo_tap;
            break;
    }

    // pull delay times from heap
    double cur_d_t_ch1 = self->cur_d_t_ch1;
    double cur_d_t_ch2 = self->cur_d_t_ch2;
    double tgt_d_t_ch1 = self->tgt_d_t_ch1;
    double tgt_d_t_ch2 = self->tgt_d_t_ch2;

    // Tempo changes always initiate a fade out.
    if ((tempo != self->cur_tempo ||
        *self->div_l != self->cur_div_l ||
        *self->div_r != self->cur_div_r)
    ) {
    	// Calculate the samples needed for the currently set delay time
        self->tgt_d_t_ch1 = calc_delay_samples(self, tempo, *self->div_l);
        self->tgt_d_t_ch2 = calc_delay_samples(self, tempo, *self->div_r);

        // Memorize the user's current settings.
        self->cur_tempo = tempo;
        self->cur_div_l = *self->div_l;
        self->cur_div_r = *self->div_r;

        /* The buffer always needs to be one sample bigger than the delay 
        time.  In order to not exceed MAX_TAPE_LEN, cut the number of 
        samples, if needed */
        if (self->tgt_d_t_ch1+1 > MAX_TAPE_LEN)
            self->tgt_d_t_ch1 = MAX_TAPE_LEN-1;

        if (self->tgt_d_t_ch2-1 > MAX_TAPE_LEN)
            self->tgt_d_t_ch2 = MAX_TAPE_LEN-1;
    }

    // Let's do the vfade gain calculation
    const float cp_blend = *self->mix;
    float target_dry_gain = 1;
    float target_wet_gain = 0;
    if (cp_blend > 0 && cp_blend < 50) {
        target_wet_gain = powf(10.0f, (cp_blend-50) * 0.04f);
    }
    else if (cp_blend < 100 && cp_blend > 50) {
        target_wet_gain = 1;
        target_dry_gain = powf(10.0f, (cp_blend-50) * -0.04f);
    }
    else if (cp_blend == 50) {
        target_wet_gain = 1;
    }
    else if (cp_blend == 100) {
        target_wet_gain = 1;
        target_dry_gain = 0;
    }

    // Lets do the feedback/crossfeed calculation        
    const float cp_feedback = *self->feedback;
    float target_feedback = 0;
    if (cp_feedback > 0 && cp_feedback < 100) {
        target_feedback = powf(10.0f, (cp_feedback-100) * 0.02f);
    }
    else if (cp_feedback == 100) {
        target_feedback = 1;
    }

    const float cp_crossf = *self->crossf;
    float target_crossf = 0;
    if (cp_crossf > 0 && cp_crossf < 100) {
        target_crossf = powf(10.0f, (cp_crossf-100) * 0.02f);
    }
    else if (cp_feedback == 100) {
        target_crossf = 1;
    }

    // State stuff to get more from heap to stack
    float dry_gain = self->dry_gain;
    float wet_gain = self->wet_gain;
    float cur_feedback = self->cur_feedback;
    float cur_crossf = self->cur_crossf;
    int pos_w = self->pos_w;

    // Loop over the block of audio we got
    for (unsigned int i = 0 ; i < n_samples ; ++i) {

        // Current samples
        float cur_fs_l = self->input_l[i];
        float cur_fs_r = self->input_r[i];

        // Previous samples
        float old_s_l = 0;
        float old_s_r = 0;

	// calculate fractional sample position. function interpolate will
	// handle wrap-arounds on its own
	double x = (double)pos_w - cur_d_t_ch1;
        old_s_l = interpolate(self->buffer_l, x);
	x = (double)pos_w - cur_d_t_ch2;
        old_s_r = interpolate(self->buffer_r, x);
    
        // Apply the low cut filter if enabled
        if (*self->low_on) {
            cur_fs_l = bf_lcf(
                cur_fs_l, 
                *self->low_f, 
                *self->low_q, 
                self->rate, 
                &self->filter_low_l
            );
            cur_fs_r = bf_lcf(
                cur_fs_r, 
                *self->low_f, 
                *self->low_q, 
                self->rate, 
                &self->filter_low_r
            );
        }
 
        // Apply the high cut filter if enabled
        if (*self->high_on) {
            cur_fs_l = bf_hcf(
                cur_fs_l, 
                *self->high_f, 
                *self->high_q, 
                self->rate, 
                &self->filter_high_l
            );
            cur_fs_r = bf_hcf(
                cur_fs_r,
                *self->high_f,
                *self->high_q,
                self->rate, 
                &self->filter_high_r
            );
        }

	// delay time smoothing
	cur_d_t_ch1 = tgt_d_t_ch1 * 0.001f + cur_d_t_ch1 * 0.999f;
	cur_d_t_ch2 = tgt_d_t_ch2 * 0.001f + cur_d_t_ch2 * 0.999f;

        /* Feedback and Crossfeed filling the buffer */

        // parameter smoothing for feedback/crossfeed
        cur_feedback = target_feedback * 0.01f + cur_feedback * 0.99f;
        cur_crossf = target_crossf * 0.01f + cur_crossf * 0.99f;

        // Left Channel
        self->buffer_l[pos_w] = cur_fs_l       // current filtered sample
            + old_s_r * cur_crossf              // crossfeed sample
            + old_s_l * cur_feedback            // feedback sample
        ;

        // Right channel (s. above)
        self->buffer_r[pos_w] = cur_fs_r
            + old_s_l * cur_crossf
            + old_s_r * cur_feedback
        ;

        /* end of buffer handling */

        // Paraemter smoothing for wet and dry gain
        wet_gain = target_wet_gain * 0.01f + wet_gain * 0.99f;
        dry_gain = target_dry_gain * 0.01f + dry_gain * 0.99f;

        // Will it blend? ;)
        self->output_l[i] = 
            dry_gain * self->input_l[i] + wet_gain * old_s_l;
        self->output_r[i] = 
            dry_gain * self->input_r[i] + wet_gain * old_s_r;

        // Iterate write position, reset to 0 if required
        pos_w = pos_w + 1 >= MAX_TAPE_LEN ? 0 : pos_w + 1;
    }
    // Memorize state for next run
    self->cur_d_t_ch1 = cur_d_t_ch1;
    self->cur_d_t_ch2 = cur_d_t_ch2;
    self->pos_w = pos_w;
    self->wet_gain = wet_gain;
    self->dry_gain = dry_gain;
    self->cur_crossf = cur_crossf;
    self->cur_feedback = cur_feedback;
}


/**
* Called, when the host deactivates the plugin.
*/
static void deactivate(LV2_Handle instance) {
}


/**
* Cleanup, freeing memory and stuff
*/
static void cleanup(LV2_Handle instance) {
    free(instance);
}


/**
* extension stuff for additional interfaces
*/
static const void* extension_data(const char* uri) {
    return NULL;
}


/**
* Descriptor linking our methods.
*/
static const LV2_Descriptor descriptor = {
    URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};


/**
* Symbol export using the descriptor above
*/
LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    switch (index) {
        case 0:  return &descriptor;
        default: return NULL;
    }
}

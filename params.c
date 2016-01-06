/*
 * params.c
 *
 *  Created on: Mar 27, 2015
 *      Author: design
 */

#define QUANTIZE_TIMECV_CH1 1
#define QUANTIZE_TIMECV_CH2 0

#include "adc.h"
#include "dig_inouts.h"
#include "params.h"
#include "globals.h"
#include "equalpowpan_lut.h"
#include "exp_1voct.h"
#include "si5153a.h"
#include "looping_delay.h"

extern __IO uint16_t potadc_buffer[NUM_POT_ADCS];
extern __IO uint16_t cvadc_buffer[NUM_CV_ADCS];

uint8_t flag_time_param_changed[2]={0,0};

extern volatile uint32_t write_addr[NUM_CHAN];
extern volatile uint32_t read_addr[NUM_CHAN];

extern uint32_t loop_start[NUM_CHAN];
extern uint32_t loop_end[NUM_CHAN];
extern const uint32_t LOOP_RAM_BASE[NUM_CHAN];

extern uint8_t flag_ping_was_changed;
extern uint8_t flag_inf_change[2];
extern uint8_t flag_rev_change[2];


float param[NUM_CHAN][NUM_PARAMS];
uint8_t mode[NUM_CHAN][NUM_MODES];

const int32_t MIN_POT_ADC_CHANGE[NUM_POT_ADCS] = {60, 60, 20, 20, 20, 20, 20, 20};
const int32_t MIN_CV_ADC_CHANGE[NUM_CV_ADCS] = {60, 60, 20, 20, 20, 20};


float POT_LPF_COEF[NUM_POT_ADCS];
float CV_LPF_COEF[NUM_CV_ADCS];


void init_params(void)
{
	uint8_t chan=0;

	for (chan=0;chan<NUM_CHAN;chan++){
		param[chan][TIME] = 1.0;
		param[chan][LEVEL] = 0.0;
		param[chan][REGEN] = 0.0;
		param[chan][MIX_DRY] = 1.0;
		param[chan][MIX_WET] = 0.0;
	}
}

void init_modes(void)
{
	uint8_t chan=0;

	for (chan=0;chan<NUM_CHAN;chan++){
		mode[chan][INF] = 0;
		mode[chan][REV] = 0;
		mode[chan][TIMEMODE_POT] = MOD_READWRITE_TIME;
		mode[chan][TIMEMODE_JACK] = MOD_READWRITE_TIME;
	}
	//debug:
	//mode[0][TIMEMODE_POT] = MOD_READWRITE_TIME;
	//mode[0][TIMEMODE_CV] = MOD_READWRITE_TIME;
	//mode[1][TIMEMODE_POT] = MOD_READWRITE_TIME;
	//mode[1][TIMEMODE_CV] = MOD_READWRITE_TIME;

}


inline float LowPassSmoothingFilter(float current_value, float new_value, float coef)
{
	return (current_value * coef) + (new_value * (1.0f-coef));
}

void init_LowPassCoefs(void)
{
	CV_LPF_COEF[TIME*2] = 0.99;
	CV_LPF_COEF[TIME*2+1] = 0.99;

	CV_LPF_COEF[LEVEL*2] = 0.99;
	CV_LPF_COEF[LEVEL*2+1] = 0.99;

	CV_LPF_COEF[REGEN*2] = 0.99;
	CV_LPF_COEF[REGEN*2+1] = 0.99;


	POT_LPF_COEF[TIME*2] = 0.99;
	POT_LPF_COEF[TIME*2+1] = 0.99;

	POT_LPF_COEF[LEVEL*2] = 0.9;
	POT_LPF_COEF[LEVEL*2+1] = 0.9;

	POT_LPF_COEF[REGEN*2] = 0.9;
	POT_LPF_COEF[REGEN*2+1] = 0.9;

	POT_LPF_COEF[MIXPOT*2] = 0.9;
	POT_LPF_COEF[MIXPOT*2+1] = 0.9;

}

float time_mult[2]={1.0,1.0};
uint32_t sample_rate_div[2]={30,30};
uint32_t sample_rate_num[2]={265,265};
uint32_t sample_rate_denom[2]={512,512};
uint32_t old_sample_rate_div[2]={0,0};
uint32_t old_sample_rate_num[2]={0,0};
uint32_t old_sample_rate_denom[2]={0,0};

void update_adc_params(void)
{
	int16_t i_smoothed_potadc[NUM_POT_ADCS]={0x7FFF,0x7FFF,0x7FFF,0x7FFF,0x7FFF,0x7FFF,0x7FFF,0x7FFF};
	int16_t i_smoothed_cvadc[NUM_CV_ADCS]={0x7FFF,0x7FFF,0x7FFF,0x7FFF,0x7FFF,0x7FFF};

	static float smoothed_potadc[NUM_POT_ADCS]={0,0,0,0,0,0,0,0};
	static float smoothed_cvadc[NUM_CV_ADCS]={0,0,0,0,0,0};


	static int16_t old_smoothed_cvadc[2]={0,0};
	static int16_t old_smoothed_potadc[2]={0,0};

	uint8_t i, channel;
	int32_t t,t2;
	//float t_f;
	int32_t t_combined;

	uint8_t switch1_val;

	for (i=0;i<NUM_POT_ADCS;i++)
	{
		smoothed_potadc[i] = LowPassSmoothingFilter(smoothed_potadc[i], (float)potadc_buffer[i], POT_LPF_COEF[i]);
		i_smoothed_potadc[i] = (int16_t)smoothed_potadc[i];
	}

	for (i=0;i<NUM_CV_ADCS;i++)
	{
		smoothed_cvadc[i] = LowPassSmoothingFilter(smoothed_cvadc[i], (float)cvadc_buffer[i], CV_LPF_COEF[i]);
		i_smoothed_cvadc[i] = (int16_t)smoothed_cvadc[i];
	}


	for (channel=0;channel<2;channel++)
	{

		if (mode[channel][TIMEMODE_POT]==MOD_SAMPLE_RATE_Q)
		{
			sample_rate_div[channel] = 11 * (get_clk_div_nominal(4095-i_smoothed_potadc[TIME*2+channel]) + get_clk_div_nominal(4095-i_smoothed_cvadc[TIME*2+channel]));

			if (sample_rate_div[channel]>182) sample_rate_div[channel]=182; //limit at 8kHz SR = 2.048MHz MCLK

			sample_rate_num[channel] = 265;
			sample_rate_denom[channel] = 512;
		}

		else if (mode[channel][TIMEMODE_POT]==MOD_SAMPLE_RATE_NOQ)
		{
			t=i_smoothed_cvadc[TIME*2+channel] - old_smoothed_cvadc[channel];
			t2=i_smoothed_potadc[TIME*2+channel] - old_smoothed_potadc[channel];

			if ( t>50 || t<-50 || t2<-50 || t2>50 )
			{
				old_smoothed_cvadc[channel] = i_smoothed_cvadc[TIME*2+channel];
				old_smoothed_potadc[channel] = i_smoothed_potadc[TIME*2+channel];


				sample_rate_div[channel] = ((4095-i_smoothed_potadc[TIME*2+channel])/27)+30; //8 to 520 to 1032
				sample_rate_div[channel] += ((4095-i_smoothed_cvadc[TIME*2+channel])/27)+30; //8 to 520 to 1032

				if (sample_rate_div[channel]>182) sample_rate_div[channel]=182; //limit at 8kHz SR = 2.048MHz MCLK
			}

			sample_rate_num[channel] = 265;
			sample_rate_denom[channel] = 512;
		}

		else if (mode[channel][TIMEMODE_POT]==MOD_READWRITE_TIME)
		{
			//Quantize sum of pot and jack (hard clipped at 4095)
			t_combined = i_smoothed_potadc[TIME*2+channel] + (i_smoothed_cvadc[TIME*2+channel]-2048);
			if (t_combined>4095) t_combined = 4095;
			else if (t_combined<0) t_combined = 0;

			time_mult[channel] = get_clk_div_nominal(t_combined);
		}

#ifdef USE_VCXO
		if ( ((old_sample_rate_div[channel]>sample_rate_div[channel]) && ((old_sample_rate_div[channel]-sample_rate_div[channel])>4))
				|| ((old_sample_rate_div[channel]<sample_rate_div[channel]) && ((sample_rate_div[channel]-old_sample_rate_div[channel])>4)))
		{
			old_sample_rate_div[channel] = sample_rate_div[channel];
			old_sample_rate_num[channel] = sample_rate_num[channel];
			old_sample_rate_denom[channel] = sample_rate_denom[channel];
			setupMultisynth(channel, SI5351_PLL_A, sample_rate_div[channel], sample_rate_num[channel], sample_rate_denom[channel]);
		}
#endif

		/*else {

			t=i_smoothed_cvadc[TIME*2+channel] - old_smoothed_cvadc[channel];
			t2=i_smoothed_potadc[TIME*2+channel] - old_smoothed_potadc[channel];

			if ( t>50 || t<-50 || t2<-50 || t2>50 )
			{
				old_smoothed_cvadc[channel] = i_smoothed_cvadc[TIME*2+channel];
				old_smoothed_potadc[channel] = i_smoothed_potadc[TIME*2+channel];
			}

			time_mult[channel] = get_clk_div_nominal(old_smoothed_potadc[channel]);

			if (old_smoothed_cvadc[channel]>2100) {
				t_f = exp_1voct[(old_smoothed_cvadc[channel]-2048)>>1];
			} else
			if (old_smoothed_cvadc[channel]<2000) {
				t_f = 1.0 / exp_1voct[old_smoothed_cvadc[channel]>>1];
			} else
				t_f = 1.0;

			if (t_f < 1.1) t_f=1.0;
			time_mult[channel] = time_mult[channel] * t_f;

		}*/

		// Adjust TIME by the time switch position

		if (channel==0){
			switch1_val = TIMESW_CH1;
		}else{
			switch1_val = TIMESW_CH2;
		}

		if (switch1_val==0b10) time_mult[channel] = time_mult[channel] + 16.0; //switch up: 17-32
		//else if (switch1_val==0b11) time_mult[channel] = time_mult[channel]; //switch in middle: 1-16
		else if (switch1_val==0b01) time_mult[channel] = time_mult[channel] / 8.0; //switch down: eighth notes


		if (time_mult[channel]!=param[channel][TIME]){
			flag_time_param_changed[channel] = 1;

			param[channel][TIME] = time_mult[channel];
		}


		// Set LEVEL and REGEN to 0 and 1 if we're in infinite repeat mode
		// Otherwise combine Pot and CV, and hard-clip at 4096

		if (mode[channel][INF] == 0){

			t_combined = i_smoothed_potadc[LEVEL*2+channel] + i_smoothed_cvadc[LEVEL*2+channel];
			if (t_combined>4095) t_combined = 4095;

			param[channel][LEVEL]=t_combined/4095.0;

			t_combined = i_smoothed_potadc[REGEN*2+channel] + i_smoothed_cvadc[REGEN*2+channel];
			if (t_combined>4095) t_combined = 4095;

			// From 0 to 80% of rotation, Regen goes from 0% to 100%
			// From 80% to 90% of rotation, Regen is set at 100%
			// From 90% to 100% of rotation, Regen goes from 100% to 110%
			if (t_combined<3300.0)
				param[channel][REGEN]=t_combined/3300.0;

			else if (t_combined<=3723.0)
				param[channel][REGEN]=1.0;

			else
				param[channel][REGEN]=t_combined/3723.0; // 4096/3723 = 110% regeneration

		} else {
			param[channel][LEVEL]=0.0;
			param[channel][REGEN]=1.0;
		}

		// MIX uses an equal power panning lookup table
		// Each MIX pot sets two parameters: wet and dry

		param[channel][MIX_DRY]=epp_lut[i_smoothed_potadc[MIXPOT*2+channel]];

		param[channel][MIX_WET]=epp_lut[4095 - i_smoothed_potadc[MIXPOT*2+channel]];

	}
}

inline void update_instant_params(uint8_t channel){

	if (flag_inf_change[channel])
	{
		flag_inf_change[channel]=0;
		//mode[channel][INF] = 1 - mode[channel][INF];

		if (mode[channel][INF])
		{
			mode[channel][INF] = 0;

			//read_addr[channel] = calculate_read_addr(channel, divmult_time[channel]);
			set_divmult_time(channel);

			loop_start[channel] = LOOP_RAM_BASE[channel];
			loop_end[channel] = LOOP_RAM_BASE[channel] + LOOP_SIZE;

		} else {
			mode[channel][INF] = 1;

			loop_start[channel]=read_addr[channel];
			loop_end[channel]=write_addr[channel];
		}

	}


	if (flag_rev_change[channel])
	{
		flag_rev_change[channel]=0;

		mode[channel][REV] = 1- mode[channel][REV];

		if (channel==0)
		{
			if (mode[channel][REV]) LED_REV1_ON;
			else LED_REV1_OFF;
		} else {
			if (mode[channel][REV]) LED_REV2_ON;
			else LED_REV2_OFF;
		}

		swap_read_write(channel);
	}


	if (flag_time_param_changed[channel] || flag_ping_was_changed)
	{
		flag_time_param_changed[channel]=0;

		set_divmult_time(channel);
	}
}


float get_clk_div_nominal(uint16_t adc_val)
{
	if (adc_val<=100) //was 150
		return(P_1);
	else if (adc_val<=310) //was 310
		return(P_2);
	else if (adc_val<=565)
		return(P_3);
	else if (adc_val<=816)
		return(P_4);
	else if (adc_val<=1062)
		return(P_5);
	else if (adc_val<=1304)
		return(P_6);
	else if (adc_val<=1529)
		return(P_7);
	else if (adc_val<=1742)
		return(P_8);
	else if (adc_val<=1950)
		return(P_9);
	else if (adc_val<=2157) // Center
		return(P_10);
	else if (adc_val<=2365)
		return(P_11);
	else if (adc_val<=2580)
		return(P_12);
	else if (adc_val<=2806)
		return(P_13);
	else if (adc_val<=3044)
		return(P_14);
	else if (adc_val<=3289)
		return(P_15);
	else if (adc_val<=3537)
		return(P_16);
	else if (adc_val<=3790)
		return(P_17);
	else if (adc_val<=4003)
		return(P_18);
	else
		return(P_19);
}



/*
 * audio_memory.c
 *
 *  Created on: Apr 6, 2015
 *      Author: design
 */

#include "audio_memory.h"
#include "looping_delay.h"
#include "globals.h"
#include "dig_inouts.h"
#include "params.h"

extern float param[NUM_CHAN][NUM_PARAMS];
extern uint8_t mode[NUM_CHAN][NUM_MODES];

extern const uint32_t LOOP_RAM_BASE[NUM_CHAN];


uint32_t sdram_read(uint32_t addr, uint8_t channel, int16_t *rd_buff, uint8_t num_samples){
	uint8_t i;

	//Loop of 8 takes 2.5us
	//read from SDRAM. first one takes 200us, subsequent reads take 50ns
	for (i=0;i<num_samples;i++){
		while(FMC_GetFlagStatus(FMC_Bank2_SDRAM, FMC_FLAG_Busy) != RESET){;}

		//Enforce valid addr range
		if ((addr<SDRAM_BASE) || (addr > (SDRAM_BASE + SDRAM_SIZE)))
		addr=SDRAM_BASE;

		//even addresses only
		addr = (addr & 0xFFFFFFFE);

		rd_buff[i] = *((int16_t *)(addr & 0xFFFFFFFE));

		addr = inc_addr(addr, channel);

	}

	return(addr);
}


uint32_t sdram_write(uint32_t addr, uint8_t channel, int16_t *wr_buff, uint8_t num_samples){
	uint8_t i;

	for (i=0;i<num_samples;i++){

		while(FMC_GetFlagStatus(FMC_Bank2_SDRAM, FMC_FLAG_Busy) != RESET){;}

		//Enforce valid addr range
		if ((addr<SDRAM_BASE) || (addr > (SDRAM_BASE + SDRAM_SIZE)))
		addr=SDRAM_BASE;

		//even addresses only
		addr = (addr & 0xFFFFFFFE);

		*((int16_t *)addr) = wr_buff[i];

		addr = inc_addr(addr, channel);

	}

	return(addr);

}

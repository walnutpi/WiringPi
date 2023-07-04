

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <limits.h>
#include "softPwm.h"
#include "softTone.h"

#include "wiring-allwinner.h"
#include "../board/board.h"
#include "wiringPi.h"

int pwmmode = 1;
uint32_t MAP_MASK = 0Xfff;

volatile uint32_t *mmap_gpio;
volatile uint32_t *mmap_gpioc;



uint32_t GPIOL_BASE;
static void board_init()
{
    AW_select();
    GPIOL_BASE = AW_get_GpioABase();
}

const char * int2bin(uint32_t param) {
    int bits = sizeof(uint32_t)*CHAR_BIT;
    static char buffer[sizeof(uint32_t)*CHAR_BIT + 1];
    char chars[2] = {'0', '1'};
    int i,j,offset;
    for (i = 0; i < bits; i++) {
        j = bits - i - 1;
        offset = (param & (1 << j)) >> j;
        buffer[i] = chars[offset];
    }
    buffer[bits] = '\0';
    return buffer;
}

/*
 * Read register value helper  
 */
unsigned int readR(unsigned int addr)
{
    board_init();
	uint32_t val = 0;
	uint32_t mmap_base = (addr & ~MAP_MASK);
	uint32_t mmap_seek = ((addr - mmap_base) >> 2);

	if (addr >= GPIOL_BASE) {
		val = *(mmap_gpioc + mmap_seek);
	} else		
		val = *(mmap_gpio + mmap_seek);
	return val;
}

/*
 * Wirte value to register helper
 */
void writeR(unsigned int val, unsigned int addr)
{
    board_init();
	unsigned int mmap_base = (addr & ~MAP_MASK);
	unsigned int mmap_seek = ((addr - mmap_base) >> 2);
		        
	if (addr >= GPIOL_BASE) {
		*(mmap_gpioc + mmap_seek) = val;
	} else
		*(mmap_gpio + mmap_seek) = val;
}



void print_pwm_reg() {
    uint32_t val = readR(SUNXI_PWM_CTRL_REG);
    uint32_t val2 = readR(SUNXI_PWM_CH0_PERIOD);
    if (wiringPiDebug) {
        printf("SUNXI_PWM_CTRL_REG: %s\n", int2bin(val));
        printf("SUNXI_PWM_CH0_PERIOD: %s\n", int2bin(val2));
    }
}

void sunxi_pwm_set_enable(int en) {
    int val = 0;
    val = readR(SUNXI_PWM_CTRL_REG);
    if (en) {
        val |= (SUNXI_PWM_CH0_EN | SUNXI_PWM_SCLK_CH0_GATING);
    }
    else {
        val &= ~(SUNXI_PWM_CH0_EN | SUNXI_PWM_SCLK_CH0_GATING);
    }
    if (wiringPiDebug)
        printf(">>function%s,no:%d,enable? :0x%x\n", __func__, __LINE__, val);
    writeR(val, SUNXI_PWM_CTRL_REG);
    delay(1);
    print_pwm_reg();
}

void sunxi_pwm_set_mode(int mode) {
    int val = 0;
    val = readR(SUNXI_PWM_CTRL_REG);
    mode &= 1; //cover the mode to 0 or 1
    if (mode) { //pulse mode
        val |= (SUNXI_PWM_CH0_MS_MODE | SUNXI_PWM_CH0_PUL_START);
        pwmmode = 1;
    } else { //cycle mode
        val &= ~(SUNXI_PWM_CH0_MS_MODE);
        pwmmode = 0;
    }
    val |= (SUNXI_PWM_CH0_ACT_STA);
    if (wiringPiDebug)
        printf(">>function%s,no:%d,mode? :0x%x\n", __func__, __LINE__, val);
    writeR(val, SUNXI_PWM_CTRL_REG);
    delay(1);
    print_pwm_reg();   
}

void sunxi_pwm_set_clk(int clk) {
    int val = 0;
    if (wiringPiDebug)
        printf(">>function%s,no:%d\n", __func__, __LINE__);
    // sunxi_pwm_set_enable(0);
    val = readR(SUNXI_PWM_CTRL_REG);
    if (wiringPiDebug)
        printf("read reg val: 0x%x\n", val);
    //clear clk to 0
    val &= 0xf801f0;
    val |= ((clk & 0xf) << 15); //todo check wether clk is invalid or not
    writeR(val, SUNXI_PWM_CTRL_REG);
    sunxi_pwm_set_enable(1);
    if (wiringPiDebug)
        printf(">>function%s,no:%d,clk? :0x%x\n", __func__, __LINE__, val);
    delay(1);
    print_pwm_reg();
}

/**
 * ch0 and ch1 set the same,16 bit period and 16 bit act
 */
int sunxi_pwm_get_period(void) {
    uint32_t period_cys = 0;
    period_cys = readR(SUNXI_PWM_CH0_PERIOD); //get ch1 period_cys
    if (wiringPiDebug) {
        printf("periodcys: %d\n", period_cys);
    }
    period_cys &= 0xffff0000; //get period_cys
    period_cys = period_cys >> 16;
    if (wiringPiDebug)
        printf(">>func:%s,no:%d,period/range:%d", __func__, __LINE__, period_cys);
    delay(1);
    return period_cys;
}

int sunxi_pwm_get_act(void) {
    uint32_t period_act = 0;
    period_act = readR(SUNXI_PWM_CH0_PERIOD); //get ch1 period_cys
    period_act &= 0xffff; //get period_act
    if (wiringPiDebug)
        printf(">>func:%s,no:%d,period/range:%d", __func__, __LINE__, period_act);
    delay(1);
    return period_act;
}

void sunxi_pwm_set_period(int period_cys) {
    uint32_t val = 0;
    //all clear to 0
    if (wiringPiDebug)
        printf(">>func:%s no:%d\n", __func__, __LINE__);
    period_cys &= 0xffff; //set max period to 2^16
    period_cys = period_cys << 16;
    val = readR(SUNXI_PWM_CH0_PERIOD);
    if (wiringPiDebug)
        printf("read reg val: 0x%x\n", val);
    val &= 0x0000ffff;
    period_cys |= val;
    if (wiringPiDebug)
        printf("write reg val: 0x%x\n", period_cys);
    writeR(period_cys, SUNXI_PWM_CH0_PERIOD);
    delay(1);
    val = readR(SUNXI_PWM_CH0_PERIOD);
    if (wiringPiDebug)
        printf("readback reg val: 0x%x\n", val);
    print_pwm_reg();
}

void sunxi_pwm_set_act(int act_cys) {
    uint32_t per0 = 0;
    //keep period the same, clear act_cys to 0 first
    if (wiringPiDebug)
        printf(">>func:%s no:%d\n", __func__, __LINE__);
    per0 = readR(SUNXI_PWM_CH0_PERIOD);
    if (wiringPiDebug)
        printf("read reg val: 0x%x\n", per0);
    per0 &= 0xffff0000;
    act_cys &= 0xffff;
    act_cys |= per0;
    if (wiringPiDebug)
        printf("write reg val: 0x%x\n", act_cys);
    writeR(act_cys, SUNXI_PWM_CH0_PERIOD);
    delay(1);
    print_pwm_reg();
}


void sunxi_gpio_write(int pin, int value)
{
    unsigned int bank   = pin >> 5;
    unsigned int index  = pin - (bank << 5);
    unsigned int phyaddr = 0;

    unsigned int regval = 0;
	
	if (bank == 11) 
		phyaddr = GPIOL_BASE + 0x10;
	else
		phyaddr = GPIOL_BASE + (bank * 36) + 0x10;



		regval = readR(phyaddr);
		if (wiringPiDebug)
			printf("befor write reg val: 0x%x,index:%d\n", regval, index);
		if(0 == value) {
			regval &= ~(1 << index);
			writeR(regval, phyaddr);
			regval = readR(phyaddr);
			if (wiringPiDebug)
				printf("LOW val set over reg val: 0x%x\n", regval);
		} else {
			regval |= (1 << index);
			writeR(regval, phyaddr);
			regval = readR(phyaddr);
			if (wiringPiDebug)
				printf("HIGH val set over reg val: 0x%x\n", regval);
		}


}
void sunxi_init()
{
    ;
}
int sunxi_gpio_read(int gpio_num)
{
    return 0;
}
int OrangePi_set_gpio_mode(int pin, int mode)
{
    unsigned int regval = 0;
    unsigned int bank   = pin >> 5;
    unsigned int index  = pin - (bank << 5);
    unsigned int phyaddr = 0;

	int offset = ((index - ((index >> 3) << 3)) << 2);

	if (bank == 11) {
		phyaddr = AW_get_GpioLBase() + ((index >> 3) << 2);
	}
	else
		phyaddr = AW_get_GpioABase() + (bank * 36) + ((index >> 3) << 2);

    /* Ignore unused gpio */
		regval = readR(phyaddr);
			if (wiringPiDebug)
				printf("Before read reg val: 0x%x offset:%d\n",regval,offset);
        if (wiringPiDebug)
            printf("Register[%#x]: %#x index:%d\n", phyaddr, regval, index);

        /* Set Input */
        if(INPUT == mode) {

			regval &= ~(7 << offset);
			writeR(regval, phyaddr);
            regval = readR(phyaddr);
            if (wiringPiDebug)
                printf("Input mode set over reg val: %#x\n",regval);
        } else if(OUTPUT == mode) { /* Set Output */

			regval &= ~(7 << offset);
			regval |=  (1 << offset);
			if (wiringPiDebug)
				printf("Out mode ready set val: 0x%x\n",regval);
			writeR(regval, phyaddr);
            regval = readR(phyaddr);
            if (wiringPiDebug)
                printf("Out mode get value: 0x%x\n",regval);
        }else if (PWM_OUTPUT == mode) {
            // set pin PWMx to pwm mode
            regval &= ~(7 << offset);
            regval |= (0x3 << offset);
            if (wiringPiDebug)
                printf(">>>>>line:%d PWM mode ready to set val: 0x%x\n", __LINE__, regval);
            writeR(regval, phyaddr);
            delayMicroseconds(200);
            regval = readR(phyaddr);
            if (wiringPiDebug)
                printf("<<<<<PWM mode set over reg val: 0x%x\n", regval);
            //clear all reg
            writeR(0, SUNXI_PWM_CTRL_REG);
            writeR(0, SUNXI_PWM_CH0_PERIOD);

            //set default M:S to 1/2
            sunxi_pwm_set_period(1024);
            sunxi_pwm_set_act(512);
            pwmSetMode(PWM_MODE_MS);
            sunxi_pwm_set_clk(PWM_CLK_DIV_120); //default clk:24M/120
            delayMicroseconds(200);
        } 
		else {
            printf("Unknow mode\n");
        }


    return 0;
}


void sunxi_pinMode (int pin, int mode)
{
    struct wiringPiNodeStruct *node = wiringPiNodes ;
    if (wiringPiDebug)
        printf("PinMode: pin:%d,mode:%d\n", pin, mode);

    if ((pin & PI_GPIO_MASK) == 0) {
        if (wiringPiMode == WPI_MODE_PINS)
            pin = pinToGpio[pin];
        else if (wiringPiMode == WPI_MODE_PHYS)
            pin = physToGpio[pin];
        else if (wiringPiMode != WPI_MODE_GPIO)
                return;
        
        if (-1 == pin) {
            printf("[%s:L%d] the pin:%d is invaild,please check it over!\n", 
                        __func__,  __LINE__, pin);
            return;
        }
        
        if (mode == INPUT) {
            OrangePi_set_gpio_mode(pin, INPUT);
            return;
        } else if (mode == OUTPUT) {
            OrangePi_set_gpio_mode(pin, OUTPUT);
            return ;
        } else if (mode == PWM_OUTPUT) {
            if(pin != 5) {
                printf("the pin you choose doesn't support hardware PWM\n");
                printf("you can select wiringPi pin %d for PWM pin\n", 42);
                printf("or you can use it in softPwm mode\n");
                return;
            }
            OrangePi_set_gpio_mode(pin, PWM_OUTPUT);
            return;
        } else
            return;
    } else {
        if ((node = wiringPiFindNode (pin)) != NULL)
            node->pinMode(node, pin, mode);
        return ;
    }



}
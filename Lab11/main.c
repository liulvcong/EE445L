/*
 * main.c
 
 * derived from TI's getweather example
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com/
 *
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/debug.h"
#include "driverlib/fpu.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/rom.h"
#include "driverlib/sysctl.h"
#include "driverlib/uart.h"
#include "utils/uartstdio.h"
#include "utils/cmdline.h"
#include "application_commands.h"
#include "LED.h"
#include "ST7735.h"
#include "ADCSWTrigger.h"
#include "../inc/tm4c123gh6pm.h"
#include "Timers.h"
#include "PLL.h"
#include "motorLib/motor.h"
#include "MPU6050.h"
#include "i2c.h"
#include "SysTick.h"
#include "board.h"
#include "switch.h"
#include "balance.h"
#include "sense.h"
#include <math.h>

#define ACCELEROMETER_SENSITIVITY 8192.0
#define GYROSCOPE_SENSITIVITY 65.536
 
#define M_PI 3.14159265359	  
#define NUM_SAMPLES 100
 
#define dt 0.01	

RobotState segway;
float pitch = 0;
float lastRoll = 0;
float targetAngle = 0;
float roll = 0;
float iTerm = 0;
float kD = 0.4;
float kP = 1.5;
float kI = 1;
float output = 0;
int botSpeed = 0;
volatile uint16_t count = 0;
uint32_t currentTime = 0;
uint32_t lastTime = 0;
char buf[500] = {0};
float calibrationSum = 0;
volatile bool calibrationDone = false;
float input = 0;
float lastInput = 0;


// DEBUG
typedef enum Mode {FWD, BWD, LFT, RGT, NO} Mode;
Mode mode;


void UART_Init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    UARTStdioConfig(0, 115200, 50000000);
}

void handleButtons(){
	// very crude debug for demoing
	mode = NO;
	if (sw1) mode = FWD;
	if (sw2) mode = BWD;
	if (sw3) mode = LFT;
	if (sw4) mode = RGT;
}


void ComplementaryFilter(RobotState* r, float *pitch, float *roll)
{
    float pitchAcc, rollAcc;               
 
    // Integrate the gyroscope data -> int(angularSpeed) = angle
    *pitch += ((float)r->gyro_x / GYROSCOPE_SENSITIVITY) * dt; // Angle around the X-axis
    *roll -= ((float)r->gyro_y / GYROSCOPE_SENSITIVITY) * dt;    // Angle around the Y-axis
 
    // Compensate for drift with accelerometer data if !bullshit
    // Sensitivity = -2 to 2 G at 16Bit -> 2G = 32768 && 0.5G = 8192
    int forceMagnitudeApprox = abs(r->accel_x) + abs(r->accel_y) + abs(r->accel_z);
    if (forceMagnitudeApprox > 8192 && forceMagnitudeApprox < 32768)
    {
	// Turning around the X axis results in a vector on the Y-axis
        pitchAcc = atan2f((float)r->accel_y, (float)r->accel_z) * 180 / M_PI;
        *pitch = *pitch * 0.98 + pitchAcc * 0.02;
 
	// Turning around the Y axis results in a vector on ssthe X-axis
        rollAcc = atan2f((float)r->accel_x, (float)r->accel_z) * 180 / M_PI;
        *roll = *roll * 0.98 + rollAcc * 0.02;
    }
} 


long map(long x, long in_min, long in_max, long out_min, long out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void getInitialSetpoint(){
		
	
		while(1){
			if (count % 100 == 0){
			ST7735_SetCursor(0,0);
		sprintf(buf, "roll = %.1f   \npitch = %.1f   \nsum = %.1f    \ntarget = %.1f   ", roll, pitch, calibrationSum, targetAngle);
		ST7735_OutString(buf);
			calibrationSum += roll;
				targetAngle = calibrationSum/count;
			}
		}
		
		targetAngle = calibrationSum/100;
}


int main(void)
{
    initClk();       // set system clock to 50 MHz
    Init_Switches();
    motor_init();
    MPU6050_Init();
    UART_Init();
		ST7735_InitR(INITR_REDTAB);
		Timer1_Init(50000000/100);
		//Timer2_Init(50000000/1000);
    //Init_Timers();
    motor_set(PORT, FORWARD, 1.0);
    motor_set(STARBOARD, FORWARD, 1.0);
		int i = 0;
		/*
		while(1){
			if (count % 100 == 0){
			ST7735_SetCursor(0,0);
		sprintf(buf, "roll = %d  \npitch = %.1f   ", ((int)roll + 360) % 360, pitch);
		ST7735_OutString(buf);
			//calibrationSum += roll;
				//targetAngle = calibrationSum/count;
			}
		}
		*/
		while(!calibrationDone){}
		targetAngle = calibrationSum/NUM_SAMPLES;
		ST7735_SetCursor(0,0);
		sprintf(buf, "Angle = %.1f", targetAngle);
		ST7735_OutString(buf);

    while(1) {
 
        // set the mode based on the buttons (DEBUG)
        handleButtons();
				
				float error = targetAngle - roll;
								
				float pTerm = kP * error;
				iTerm += kI * error;
				float dTerm = kD * (input - lastInput);
  
				output = pTerm - dTerm;
  
				if(abs(iTerm) < 200)
				{
					output += kI * iTerm;
				}
				lastInput = input;
				
				botSpeed = map((int)abs(output), 0, 75, 10000,39990);
				
				if(botSpeed > 39990){
					botSpeed = 39990;
				}
				
				if(abs(error) < 2){
					botSpeed = 0;
				}
				
				if (i%1000 == 0){
					ST7735_SetCursor(0,0);
					sprintf(buf, "speed: %d    ", botSpeed);
					ST7735_OutString(buf);
				}
				
				if (roll < targetAngle){
					motor_set(PORT, BACKWARD, botSpeed);
          motor_set(STARBOARD, BACKWARD, botSpeed);
				} else {
					motor_set(PORT, FORWARD, botSpeed);
          motor_set(STARBOARD, FORWARD, botSpeed);
				}
				motor_run();
        
    }
}

void Timer1A_Handler(void){
	TIMER1_ICR_R = TIMER_ICR_TATOCINT;
	update_state(&segway);
	lastRoll = roll;
	ComplementaryFilter(&segway, &pitch, &roll);
	if (!calibrationDone){
		calibrationSum += roll;
		count++;
		if(count == NUM_SAMPLES) calibrationDone = true;;
	}
}

void Timer2A_Handler(void){
	TIMER2_ICR_R = TIMER_ICR_TATOCINT;
	motor_run();
}

/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lcd.h"
#include "string.h"
#include "pid.h"
#include "moving_average.h"
#include "flash.h"
#include "stusb4500.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "usbd_cdc_if.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#define version "3.0.0"

enum handles {
	T115,
	T210,
	T245
} handle;

/* Timing constants */
uint32_t previous_millis_display = 0;
uint32_t interval_display = 25;

uint32_t previous_millis_debug = 0;
uint32_t interval_debug = 50;

uint32_t previous_PID_update = 0;
uint32_t interval_PID_update = 50;

uint32_t previous_millis_HANDLE_update = 0;
uint32_t interval_HANDLE_update = 200;

uint32_t previous_millis_heating_halted_update = 0;
uint32_t interval_heating_halted_update = 500;

uint32_t previous_millis_left_stand = 0;

uint32_t previous_standby_millis = 0;

uint32_t previous_check_for_valid_heater_update = 0;
uint32_t interval_check_for_valid_heater = 500;

uint32_t previous_sensor_PID_update = 0;
uint32_t interval_sensor_update = 1;

/* states for runtime switch */
typedef enum {
    RUN,
	STANDBY,
	SLEEP,
	EMERGENCY_SLEEP,
	HALTED,
} mainstates;
mainstates active_state = SLEEP;

/* Custom tuning parameters */
double Kp_custom = 0;
double Ki_custom = 0;
double Kd_custom = 0;
double temperature_custom = 100;

/* PID tuning parameters */
double Kp = 0;
double Ki = 0;
double Kd = 0;

/* PID parameters */
#define PID_MAX_OUTPUT 500
#define PID_MIN_LIMIT -300
#define PID_MAX_LIMIT 300

/* Function to detect tip presence by a periodic voltage and measure the current */
#define DETECT_TIP_BY_CURRENT

char buffer[40];								/* Buffer for UART print */
#define CHAR_BUFF_SIZE 40

#define POWER_REDUCTION_FACTOR 0.12 			/* Converts power in W to correct duty cycle */
float max_power_watt = 0.0; 					/* Sets the maximum output power */

float ADC_filter_mean = 0.0; 					/* Filtered ADC reading value */

#define ADC2_BUF_VIN_LEN 10
uint16_t ADC2_BUF_VIN[ADC2_BUF_VIN_LEN];

#define ADC1_BUF_VIN_LEN 2
uint16_t ADC1_BUF_VIN[ADC1_BUF_VIN_LEN];

float thermocouple_temperature_raw = 0.0;
float current_raw = 0.0;

#define EMERGENCY_SHUTDOWN_TEMPERATURE 475		/* Max allowed tip temperature */

#define VOLTAGE_COMPENSATION 0.00741347365 		/* Constant for scaling input voltage ADC value*/
#define CURRENT_COMPENSATION 1.000 				/* Constant for scaling input voltage ADC value*/

double min_selectable_temperature = 20;
double max_selectable_temperature = 450;

/* TC Compensation constants */
#define TC_COMPENSATION_X3_T115 -6.798689261365103e-09
#define TC_COMPENSATION_X2_T115 -6.084684965926526e-06
#define TC_COMPENSATION_X1_T115 0.2710175613404393
#define TC_COMPENSATION_X0_T115 25.398999666765054

#define TC_COMPENSATION_X3_T210 -6.798689261365103e-09
#define TC_COMPENSATION_X2_T210 -6.084684965926526e-06
#define TC_COMPENSATION_X1_T210 0.2710175613404393
#define TC_COMPENSATION_X0_T210 25.398999666765054

#define TC_COMPENSATION_X3_T245 2.0923844111330006e-09
#define TC_COMPENSATION_X2_T245 -1.2139133328964936e-05
#define TC_COMPENSATION_X1_T245 0.11753371673595008
#define TC_COMPENSATION_X0_T245 25.051871505499836

/* Struct to hold sensor values */
struct sensor_values_struct {
	double set_temperature;
	double thermocouple_temperature;
	float bus_voltage;
	float heater_current;
	float pcb_temperature;
	double in_stand;
	double handle_sense;
	mainstates previous_state;
	double enc_button_status;
};

struct sensor_values_struct sensor_values  = {.set_temperature = 0.0,
        									.thermocouple_temperature = 0.0,
											.bus_voltage = 0.0,
											.heater_current = 0.0,
											.pcb_temperature = 0.0,
											.in_stand = 0.0,
											.handle_sense  = 0.0,
											.previous_state = SLEEP,
											.enc_button_status = 0.0};


Flash_values flash_values;
Flash_values default_flash_values = {.startup_temperature = 330,
											.temperature_offset = 0,
											.standby_temp = 150,
											.standby_time = 10,
											.emergency_time = 30,
											.buzzer_enable = 1};

char menu_names[10][20] = {"Startup Temp",
							"Temp Offset",
							"Standby Temp",
							"Standby Time",
							"EM Time",
							"Buzzer Enable",
							"-Load Default-",
							"-Exit and Save-",
							"-Exit no Save-"};

double PID_output = 0.0;
double PID_setpoint = 0.0;
double duty_cycle = 0.0;

uint8_t current_measurement_requested = 0;
uint8_t thermocouple_measurement_requested = 0;

/* Moving average filters for sensor data */
FilterTypeDef thermocouple_temperature_filter_struct;
FilterTypeDef input_voltage_filterStruct;
FilterTypeDef stand_sense_filterStruct;
FilterTypeDef handle_sense_filterStruct;
FilterTypeDef enc_button_sense_filterStruct;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
DMA_HandleTypeDef hdma_adc2;

CRC_HandleTypeDef hcrc;

I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi2;
DMA_HandleTypeDef hdma_spi2_tx;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim4;
TIM_HandleTypeDef htim16;
TIM_HandleTypeDef htim17;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_ADC2_Init(void);
static void MX_CRC_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM16_Init(void);
static void MX_SPI2_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM17_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
PID_TypeDef TPID;

/* Function to clamp d between the limits min and max */
double clamp(double d, double min, double max) {
  const double t = d < min ? min : d;
  return t > max ? max : t;
}

/* Function to take the base b to the power of the exponent e */
double power2(double b, double e) {
	for( uint8_t i = 0; i < e; i++){
		b = b * b;
	}
  return b;
}

uint16_t RGB_to_BRG(uint16_t color){
	/*if(color ==C_BLACK){
		color = 0b0010100100000101;
	}*/

	return ((((color & 0b0000000000011111)  << 11) & 0b1111100000000000) | ((color & 0b1111111111100000) >> 5));
}

/* Returns the average of the ADC_buffer vector */
float get_mean_ADC_reading(uint16_t *adc_buffer , uint8_t adc_buffer_len){
	ADC_filter_mean = 0;
	for(uint8_t n=0; n<adc_buffer_len; n++){
		ADC_filter_mean += adc_buffer[n];
	}
	return ADC_filter_mean/adc_buffer_len;
}

void get_bus_voltage(){
	sensor_values.bus_voltage = Moving_Average_Compute(get_mean_ADC_reading(ADC2_BUF_VIN, ADC2_BUF_VIN_LEN), &input_voltage_filterStruct)*VOLTAGE_COMPENSATION;
}

void get_thermocouple_temperature(){
	if(handle == T210){
		sensor_values.thermocouple_temperature = power2(thermocouple_temperature_raw, 3)*TC_COMPENSATION_X3_T210 + power2(thermocouple_temperature_raw, 2)*TC_COMPENSATION_X2_T210 + thermocouple_temperature_raw*TC_COMPENSATION_X1_T210 + TC_COMPENSATION_X0_T210;
	}
	else if(handle == T245){
		sensor_values.thermocouple_temperature = power2(thermocouple_temperature_raw, 3)*TC_COMPENSATION_X3_T245 + power2(thermocouple_temperature_raw, 2)*TC_COMPENSATION_X2_T245 + thermocouple_temperature_raw*TC_COMPENSATION_X1_T245 + TC_COMPENSATION_X0_T245;
	}
	else if(handle == T115){
		sensor_values.thermocouple_temperature = power2(thermocouple_temperature_raw, 3)*TC_COMPENSATION_X3_T115 + power2(thermocouple_temperature_raw, 2)*TC_COMPENSATION_X2_T115 + thermocouple_temperature_raw*TC_COMPENSATION_X1_T115 + TC_COMPENSATION_X0_T115;
	}

	sensor_values.thermocouple_temperature += flash_values.temperature_offset; // Add temperature offset value
	sensor_values.thermocouple_temperature = clamp(sensor_values.thermocouple_temperature ,0 ,999); // Clamp
}

void get_current(){
	sensor_values.heater_current = current_raw * CURRENT_COMPENSATION;
}

void update_display(){
	memset(&buffer, '\0', sizeof(buffer));
	sprintf(buffer, "%.f", sensor_values.set_temperature);
	if(sensor_values.set_temperature<100){
		buffer[2] = 32;
		buffer[3] = 32;
	}
  	LCD_PutStr(10, 75, buffer, FONT_arial_29X35, RGB_to_BRG(C_GREEN), RGB_to_BRG(C_BLACK));

	memset(&buffer, '\0', sizeof(buffer));
	sprintf(buffer, "%.1f V", sensor_values.bus_voltage);
	LCD_PutStr(100, 260, buffer, FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));


	if(handle == T210){
		LCD_PutStr(100, 220, "T210", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
	}
	else if(handle == T245){
		LCD_PutStr(100, 240, "T245", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
	}


	if(active_state == SLEEP || active_state == EMERGENCY_SLEEP || active_state == HALTED){
		LCD_PutStr(214, 65, "Z", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		LCD_PutStr(214, 121, "z", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		LCD_PutStr(214, 177, "Z", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		LCD_PutStr(214, 233, "z", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
	}
	else if(active_state == STANDBY){
		LCD_PutStr(214, 65, "S", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		LCD_PutStr(214, 121, "T", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		LCD_PutStr(214, 177, "A", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		LCD_PutStr(214, 233, "N", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		LCD_PutStr(214, 289, "D", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		LCD_PutStr(214, 253, "B", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		LCD_PutStr(214, 279, "Y", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
	}
	else{
		UG_FillFrame(210, 287-(PID_output/PID_MAX_OUTPUT)*262, 	230, 	287, 									RGB_to_BRG(C_LIGHT_SKY_BLUE));
		UG_FillFrame(210, 55, 									230, 	287-(PID_output/PID_MAX_OUTPUT)*262-1, RGB_to_BRG(C_BLACK));
	}


	if(sensor_values.heater_current == 0){
	  	LCD_PutStr(10, 185, "---", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
	}

	else{
		memset(&buffer, '\0', sizeof(buffer));
		sprintf(buffer, "%.f", sensor_values.thermocouple_temperature);
		if(sensor_values.thermocouple_temperature < 100){
			buffer[2] = 32;
			buffer[3] = 32;
		}
	  	LCD_PutStr(10, 165, buffer, FONT_arial_29X35, RGB_to_BRG(C_GREEN), RGB_to_BRG(C_BLACK));
	}
}


/* Get encoder value (Set temp.) and limit is NOT heating_halted*/
void get_set_temperature(){
	TIM2->CNT = clamp(TIM2->CNT, min_selectable_temperature, max_selectable_temperature);
	sensor_values.set_temperature = TIM2->CNT;
}

/* Beep the buzzer */
void beep(){
		__HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, 5);
		  HAL_TIM_Base_Start_IT(&htim17);
}

/* Function to set state to EMERGENCY_SLEEP */
void check_emergency_shutdown(){
	/* Function to set state to EMERGENCY_SLEEP if iron is in RUN state for longer than EMERGENCY_shutdown_time */
	if(!sensor_values.previous_state == RUN  && active_state == RUN){
		previous_millis_left_stand = HAL_GetTick();
	}
	if ((sensor_values.in_stand == 0) && (HAL_GetTick() - previous_millis_left_stand >= flash_values.emergency_time*60000) && active_state == RUN){
		active_state = EMERGENCY_SLEEP;
		beep();
	}
	sensor_values.previous_state = active_state;

	/* Function to set state to EMERGENCY_SLEEP if iron is over max allowed temp */
	if((sensor_values.thermocouple_temperature > EMERGENCY_SHUTDOWN_TEMPERATURE) && (active_state == RUN)){
		active_state = EMERGENCY_SLEEP;
		beep();
	}
}

/* Function to toggle between RUN and HALTED at each press of the encoder button */
void get_enc_button_status(){
	uint8_t button_status;
	if(HAL_GPIO_ReadPin (GPIOB, SW_2_Pin) == 1){
		button_status = 1;
	}
	else{
		button_status = 0;
	}
	sensor_values.enc_button_status = Moving_Average_Compute(button_status, &enc_button_sense_filterStruct); /* Moving average filter */

	/* If encoder button is pressed */
	if((sensor_values.enc_button_status > 0.8) && (HAL_GetTick()-previous_millis_heating_halted_update >= interval_heating_halted_update)){
		beep();
		// toggle between RUN and HALTED
		if ((active_state == RUN) || (active_state == STANDBY)){
			active_state = HALTED;
		}
		else if (active_state == HALTED){
			active_state = RUN;
		}
		else if (active_state == EMERGENCY_SLEEP){
			active_state = RUN;
		}
		previous_millis_heating_halted_update = HAL_GetTick();
	}
}

/* Get the status of handle in/on stand to trigger SLEEP */
void get_stand_status(){
	uint8_t stand_status;
	if(HAL_GPIO_ReadPin (GPIOA, STAND_INP_Pin) == 0){
		stand_status = 1;
	}
	else{
		stand_status = 0;
	}
	sensor_values.in_stand = Moving_Average_Compute(stand_status, &stand_sense_filterStruct); /* Moving average filter */

	/* If handle is in stand set state to STANDBY */
	if(sensor_values.in_stand > 0.5){
		if(active_state == RUN){
			active_state = STANDBY;
			previous_standby_millis = HAL_GetTick();
		}
		if((HAL_GetTick()-previous_standby_millis >= flash_values.standby_time*60000.0) && (active_state == STANDBY)){
			active_state = SLEEP;
		}
		if((active_state == EMERGENCY_SLEEP) || (active_state == HALTED)){
			active_state = SLEEP;
		}
	}

	/* If handle is NOT in stand and state is SLEEP, change state to RUN */
	if(sensor_values.in_stand < 0.5){
		if((active_state == SLEEP) || (active_state == STANDBY)){
			active_state = RUN;
		}
	}
}

/* Automatically detect handle type, T210 or T245 based on HANDLE_DETECTION_Pin, which is connected to BLUE for T210.*/
void get_handle_type(){
	uint8_t handle_status;
	if(HAL_GPIO_ReadPin (GPIOB, HANDLE_INP_1_Pin) == 0){
		handle_status = 1;
	}
	else{
		handle_status = 0;
	}
	sensor_values.handle_sense = Moving_Average_Compute(handle_status, &handle_sense_filterStruct); /* Moving average filter */

	/* If the handle_sense is high -> T210 handle is detected */
	if(sensor_values.handle_sense > 0.5){
		handle = T210;
		max_power_watt = 60; //60W
		max_selectable_temperature = 450; //450 deg C
		Kp = 10;
		Ki = 30;
		Kd = 0.25;
	}
	/* If the handle_sense is low -> T245 Handle */
	else{
		handle = T245;
		max_power_watt = 120; //120W
		max_selectable_temperature = 450; //430 deg C
		Kp = 15;
		Ki = 30;
		Kd = 0.5;
	}
	PID_SetTunings(&TPID, Kp, Ki, Kd); // Update PID parameters based on handle type
}

/* Called when buffer is completely filled, used for DEBUG */
//void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
//    //HAL_GPIO_TogglePin(GPIOF, DEBUG_SIGNAL_A_Pin);
//}

/* Interrupts at every encoder increment */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim){
	if ((htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) || (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2)) {
		//beep();
	}
}

/* Sets the duty cycle of timer controlling the heater */
void set_heater_duty(uint16_t dutycycle){
	__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, dutycycle);
}

// Callback:
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  // Check which version of the timer triggered this callback and toggle LED
  if ((htim == &htim1) && (current_measurement_requested == 1) )
  {
	  HAL_TIM_Base_Start_IT(&htim16);
	  set_heater_duty(duty_cycle); //Set duty cycle back to calculated
  }
}

//float thermocouple_temperature_raw = Moving_Average_Compute(100, &thermocouple_temperature_filter_struct); /* Moving average filter */


// Callback:
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  // Check which version of the timer triggered this callback and toggle LED
  if (htim == &htim16 ){
	  HAL_TIM_Base_Stop_IT(&htim16);
	  //HAL_ADC_Start_IT(&hadc2);
	  HAL_ADCEx_InjectedStart_IT(&hadc1);
	  HAL_GPIO_WritePin(USR_1_GPIO_Port, USR_1_Pin, GPIO_PIN_SET);
  }
  if (htim == &htim17){
	  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, 0);
	  HAL_TIM_Base_Stop_IT(&htim17);

  }
}

void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef* hadc)
{
	if(current_measurement_requested == 1){
		HAL_GPIO_WritePin(USR_1_GPIO_Port, USR_1_Pin, GPIO_PIN_RESET);
		thermocouple_temperature_raw = HAL_ADCEx_InjectedGetValue(&hadc1,1);
		current_raw = HAL_ADCEx_InjectedGetValue(&hadc1,2);
		current_measurement_requested = 0;

	}
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_CRC_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM4_Init();
  MX_TIM16_Init();
  MX_SPI2_Init();
  MX_I2C1_Init();
  MX_USB_Device_Init();
  MX_TIM17_Init();
  /* USER CODE BEGIN 2 */
  LCD_init();

	 HAL_TIM_Encoder_Start_IT(&htim2, TIM_CHANNEL_ALL);
	 HAL_TIM_PWM_Start_IT(&htim4, TIM_CHANNEL_2);
	 HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);


		HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);
		HAL_ADC_Start_DMA(&hadc2, (uint32_t*)ADC2_BUF_VIN, (uint32_t)ADC2_BUF_VIN_LEN);	//Start ADC DMA

        HAL_ADC_Start_IT(&hadc1);        //Start ADC DMA

		Moving_Average_Init(&thermocouple_temperature_filter_struct,5);
		Moving_Average_Init(&input_voltage_filterStruct,50);
		Moving_Average_Init(&stand_sense_filterStruct,50);
		Moving_Average_Init(&handle_sense_filterStruct,50);
		Moving_Average_Init(&enc_button_sense_filterStruct,10);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  	/* Init and fill filter structures with initial values */
  		set_heater_duty(0);
  		for (int i = 0; i<200;i++){
  			get_bus_voltage();
  			get_thermocouple_temperature();
  			get_handle_type();
  			get_stand_status();
  			get_enc_button_status();
  		}

  		uint16_t menu_length = 8;

  		if(!FlashCheckCRC()){
  	    	FlashWrite(&default_flash_values);
  		}
  	    FlashRead(&flash_values);

  		/* Set startup state */
  		active_state = SLEEP;

  		/* Initiate OLED display */
  		TIM2->CNT = 1000;
  		uint16_t menu_cursor_position = 0;
  		uint16_t old_menu_cursor_position = 0;
  		uint16_t menue_start = 0;
  		uint16_t menue_level = 0;
  		uint16_t menu_active = 1;
  		float old_value = 0;

  		/* If button is pressed during startup - Show SETTINGS and allow to release button. */
  		if (HAL_GPIO_ReadPin (GPIOB, SW_2_Pin) == 1){
  			LCD_PutStr(50, 5, "SETTINGS", FONT_arial_29X35, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
  			LCD_DrawLine(0,40,240,40,RGB_to_BRG(C_WHITE));
  			LCD_DrawLine(0,41,240,41,RGB_to_BRG(C_WHITE));
  			LCD_DrawLine(0,42,240,42,RGB_to_BRG(C_WHITE));

  			HAL_Delay(1000);
  			while(menu_active == 1){
  				if(menue_level == 0){
  					if(TIM2->CNT < 1000)
  					{
  						TIM2->CNT = 1000;
  					}
  					menu_cursor_position = (TIM2->CNT - 1000) / 2;
  				}
  				if (menue_level == 1){
  					((double*)&flash_values)[menu_cursor_position] = (float)old_value + (float)(TIM2->CNT - 1000.0) / 2.0 - (float)menu_cursor_position;
  					if (menu_cursor_position == 5){
  						((double*)&flash_values)[menu_cursor_position] = round(fmod(abs(((double*)&flash_values)[menu_cursor_position]), 2));
  					}
  					if(menu_cursor_position != 1){
  						((double*)&flash_values)[menu_cursor_position] = abs(((double*)&flash_values)[menu_cursor_position]);
  					}
  				}

  				if(menu_cursor_position > menu_length){
  								menu_cursor_position = menu_length;
  								TIM2->CNT = 1000 + menu_length*2;
  				}

  				if(menu_cursor_position >= menu_length-(menu_length-5)){
  					menue_start = menu_cursor_position-5;
  				}


  				if((HAL_GPIO_ReadPin (GPIOB, SW_1_Pin) == 1) && (menu_cursor_position < menu_length-2)){
  					if(menue_level == 0){
  						old_value = ((double*)&flash_values)[menu_cursor_position];
  						old_menu_cursor_position = menu_cursor_position;
  					}
  					if(menue_level == 1){
  						TIM2->CNT = old_menu_cursor_position*2 + 1000;
  					}

  					menue_level = abs(menue_level-1);
  					HAL_Delay(200);
  				}
  				else if((HAL_GPIO_ReadPin (GPIOB, SW_1_Pin) == 1) && (menu_cursor_position == menu_length)){
  					menu_active = 0;
  				}
  				else if((HAL_GPIO_ReadPin (GPIOB, SW_1_Pin) == 1) && (menu_cursor_position == menu_length-1)){
  					menu_active = 0;
  					FlashWrite(&flash_values);
  				}
  				else if((HAL_GPIO_ReadPin (GPIOB, SW_1_Pin) == 1) && (menu_cursor_position == menu_length-2)){
  					flash_values = default_flash_values;
  				}

  	  			LCD_PutStr(0, 300, "Version:", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
  	  			LCD_PutStr(150, 300, version, FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));

  				for(int i = menue_start;i<menue_start+6;i++){

  					if((i == menu_cursor_position) && (menue_level == 0)){
  		  	  			LCD_PutStr(5, 45+(i-menue_start)*25, menu_names[i], FONT_arial_20X23, RGB_to_BRG(C_BLACK), RGB_to_BRG(C_WHITE));

  					}
  					else{
  		  	  			LCD_PutStr(5, 45+(i-menue_start)*25, menu_names[i], FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
  					}

  					char str[20];
  				  	memset(&str, '\0', sizeof(str));
  					sprintf(str, "%.0f", (((double*)&flash_values)[i]));
  					if(i <= menu_length-3){
  						if((i == menu_cursor_position) && (menue_level == 1)){
  	  		  	  			LCD_PutStr(200, 45+(i-menue_start)*25, str, FONT_arial_20X23, RGB_to_BRG(C_BLACK), RGB_to_BRG(C_WHITE));

  						}
  						else{
  	  		  	  			LCD_PutStr(200, 45+(i-menue_start)*25, str, FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));

  						}
  					}
  				}
  			}
  		}

  		/* Set initial encoder timer value */
  		TIM2->CNT = flash_values.startup_temperature;

  		/* Initiate PID controller */
  		PID(&TPID, &sensor_values.thermocouple_temperature, &PID_output, &PID_setpoint, Kp, Ki, Kd, _PID_P_ON_E, _PID_CD_DIRECT);
  		PID_SetMode(&TPID, _PID_MODE_AUTOMATIC);
  		PID_SetSampleTime(&TPID, interval_PID_update, 0); //Set PID sample time to "interval_PID_update" to make sure PID is calculated every time it is called
  		PID_SetOutputLimits(&TPID, 0, PID_MAX_OUTPUT); 	// Set max and min output limit
  		PID_SetILimits(&TPID, PID_MIN_LIMIT, PID_MAX_LIMIT); 		// Set max and min I limit

		UG_FillScreen(RGB_to_BRG(C_BLACK));

		LCD_PutStr(55, 5, "AxxSolder", FONT_arial_29X35, RGB_to_BRG(C_YELLOW), RGB_to_BRG(C_BLACK));
		LCD_DrawLine(0,40,240,40,RGB_to_BRG(C_YELLOW));
		LCD_DrawLine(0,41,240,41,RGB_to_BRG(C_YELLOW));
		LCD_DrawLine(0,42,240,42,RGB_to_BRG(C_YELLOW));


		LCD_PutStr(10, 50, "Set temp", FONT_arial_29X35, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		UG_DrawCircle(105, 90, 4, RGB_to_BRG(C_GREEN));
		UG_DrawCircle(105, 90, 3, RGB_to_BRG(C_GREEN));
		LCD_PutStr(115, 75, "C", FONT_arial_29X35, RGB_to_BRG(C_GREEN), RGB_to_BRG(C_BLACK)); //FONT_arial_49X57


		LCD_PutStr(10, 140, "Actual temp", FONT_arial_29X35, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		UG_DrawCircle(105, 185, 4, RGB_to_BRG(C_GREEN));
		UG_DrawCircle(105, 185, 3, RGB_to_BRG(C_GREEN));
		LCD_PutStr(115, 170, "C", FONT_arial_29X35, RGB_to_BRG(C_GREEN), RGB_to_BRG(C_BLACK));

		UG_DrawFrame(3, 136, 165, 225, RGB_to_BRG(C_GREEN));
		UG_DrawFrame(2, 135, 166, 226, RGB_to_BRG(C_GREEN));


		LCD_PutStr(2, 235, "Handle type:", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		LCD_PutStr(2, 255, "Input voltage:", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		LCD_PutStr(2, 275, "PCB temp:", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));
		LCD_PutStr(125, 275, "POWER ->", FONT_arial_20X23, RGB_to_BRG(C_WHITE), RGB_to_BRG(C_BLACK));

		UG_DrawLine(2, 298, 240, 298, RGB_to_BRG(C_YELLOW));
		LCD_PutStr(2, 300, "PRESETS    ", FONT_arial_20X23, RGB_to_BRG(C_YELLOW), RGB_to_BRG(C_BLACK));
		LCD_PutStr(165, 300, "280", FONT_arial_20X23, RGB_to_BRG(C_YELLOW), RGB_to_BRG(C_BLACK));
		LCD_PutStr(205, 300, "330", FONT_arial_20X23, RGB_to_BRG(C_YELLOW), RGB_to_BRG(C_BLACK));

		UG_DrawFrame(208, 53, 232, 289, RGB_to_BRG(C_WHITE));
		UG_DrawFrame(209, 54, 231, 288, RGB_to_BRG(C_WHITE));


		UG_FillFrame(180, 60, 200, 80, RGB_to_BRG(C_RED));
		UG_FillFrame(180, 80, 200, 100, RGB_to_BRG(C_GREEN));
		UG_FillFrame(180, 100, 200, 120, RGB_to_BRG(C_BLUE));
		UG_FillFrame(180, 120, 200, 140, RGB_to_BRG(C_ORANGE));
		UG_FillFrame(180, 140, 200, 160, RGB_to_BRG(C_WHITE));
		UG_FillFrame(180, 160, 200, 180, RGB_to_BRG(C_BLACK));
		UG_FillFrame(180, 180, 200, 200, RGB_to_BRG(C_YELLOW));
		UG_FillFrame(180, 200, 200, 220, RGB_to_BRG(C_DARK_GREEN));
		UG_FillFrame(180, 220, 200, 240, RGB_to_BRG(C_LIGHT_SKY_BLUE));

  		/* Start-up beep */
  		beep();
  		HAL_Delay(200);
  		beep();
  		HAL_Delay(200);
  		beep();

  		while (1){

  			check_emergency_shutdown();

  			if(HAL_GetTick() - previous_sensor_PID_update >= interval_sensor_update){
  				get_stand_status();
  				get_bus_voltage();
  				get_handle_type();
  				get_enc_button_status();
  				get_set_temperature();
  				previous_sensor_PID_update = HAL_GetTick();
  			}

  			/* switch */
  			switch (active_state) {
  				case EMERGENCY_SLEEP: {
  					PID_setpoint = 0;
  					break;
  				}
  				case RUN: {
  					PID_setpoint = sensor_values.set_temperature;
  					break;
  				}
  				case STANDBY: {
  					PID_setpoint = flash_values.standby_temp;
  					break;
  				}
  				case SLEEP: {
  					PID_setpoint = 0;
  					break;
  				}
  				case HALTED: {
  					PID_setpoint = 0;
  					break;
  				}
  			}

  			if(HAL_GetTick() - previous_PID_update >= interval_PID_update){
					thermocouple_measurement_requested = 1;

  				//set_heater_duty(0);
  				//HAL_Delay(5); // Wait to let the thermocouple voltage stabilize before taking measurement
  				get_thermocouple_temperature();

  				/* Compute PID and set duty cycle */
  				PID_Compute(&TPID);
  				duty_cycle = PID_output*(max_power_watt*POWER_REDUCTION_FACTOR/sensor_values.bus_voltage);
  				set_heater_duty(clamp(duty_cycle, 0.0, PID_MAX_OUTPUT));
  				previous_PID_update = HAL_GetTick();
  			}

  			// TUNING - ONLY USED DURING MANUAL PID TUNING
  			// ----------------------------------------------
  			//PID_SetTunings(&TPID, Kp_custom, Ki_custom, Kd_custom);
  			//sensor_values.set_temperature = temperature_custom;
  			// ----------------------------------------------

  			/* Send debug information over serial */
  			if(HAL_GetTick() - previous_millis_debug >= interval_debug){
  				memset(&buffer, '\0', sizeof(buffer));
  				sprintf(buffer, "%3.1f\t%3.1f\t%3.1f\t%3.1f\t%3.1f\t%3.1f\t%3.2f\t%3.2f\n",
  						sensor_values.thermocouple_temperature, sensor_values.set_temperature,
  						PID_output/10, PID_GetPpart(&TPID)/10.0, PID_GetIpart(&TPID)/10.0, PID_GetDpart(&TPID)/10.0,
  						sensor_values.in_stand, sensor_values.enc_button_status);
  				CDC_Transmit_FS((uint8_t *) buffer, strlen(buffer)); //Print string over USB virtual COM port
  				previous_millis_debug = HAL_GetTick();
  			}

 			/* Detect if a tip is present by sending a short voltage pulse and sense current */
			#ifdef DETECT_TIP_BY_CURRENT
  				if(HAL_GetTick() - previous_check_for_valid_heater_update >= interval_check_for_valid_heater){
  					set_heater_duty(PID_MAX_OUTPUT*0.8);
  					current_measurement_requested = 1;
  					previous_check_for_valid_heater_update = HAL_GetTick();
  				}
			#endif

  			/* Update display */
  			if(HAL_GetTick() - previous_millis_display >= interval_display){
  				update_display();
  				previous_millis_display = HAL_GetTick();
  			}
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.GainCompensation = 0;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 2;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief ADC2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Common config
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc2.Init.Resolution = ADC_RESOLUTION_12B;
  hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc2.Init.GainCompensation = 0;
  hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc2.Init.LowPowerAutoWait = DISABLE;
  hadc2.Init.ContinuousConvMode = ENABLE;
  hadc2.Init.NbrOfConversion = 1;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc2.Init.DMAContinuousRequests = ENABLE;
  hadc2.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc2.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_10;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

/**
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
  hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
  hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
  hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
  hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x30A0A7FB;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_1LINE;
  hspi2.Init.DataSize = SPI_DATASIZE_4BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7;
  hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 17-6;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 500;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.BreakAFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.Break2AFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4.294967295E9;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 10;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 10;
  if (HAL_TIM_Encoder_Init(&htim2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 8500-1;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 10;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

}

/**
  * @brief TIM16 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM16_Init(void)
{

  /* USER CODE BEGIN TIM16_Init 0 */

  /* USER CODE END TIM16_Init 0 */

  /* USER CODE BEGIN TIM16_Init 1 */

  /* USER CODE END TIM16_Init 1 */
  htim16.Instance = TIM16;
  htim16.Init.Prescaler = 1700;
  htim16.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim16.Init.Period = 65535;
  htim16.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim16.Init.RepetitionCounter = 0;
  htim16.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim16) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM16_Init 2 */

  /* USER CODE END TIM16_Init 2 */

}

/**
  * @brief TIM17 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM17_Init(void)
{

  /* USER CODE BEGIN TIM17_Init 0 */

  /* USER CODE END TIM17_Init 0 */

  /* USER CODE BEGIN TIM17_Init 1 */

  /* USER CODE END TIM17_Init 1 */
  htim17.Instance = TIM17;
  htim17.Init.Prescaler = 17000-1;
  htim17.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim17.Init.Period = 99;
  htim17.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim17.Init.RepetitionCounter = 0;
  htim17.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim17) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OnePulse_Init(&htim17, TIM_OPMODE_SINGLE) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM17_Init 2 */

  /* USER CODE END TIM17_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
  /* DMA1_Channel2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(USR_1_GPIO_Port, USR_1_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, USR_2_Pin|USR_3_Pin|USR_4_Pin|SPI2_SD_CS_Pin
                          |SPI2_DC_Pin|SPI2_RST_Pin|SPI2_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : VERSION_BIT_1_Pin VERSION_BIT_2_Pin VERSION_BIT_3_Pin */
  GPIO_InitStruct.Pin = VERSION_BIT_1_Pin|VERSION_BIT_2_Pin|VERSION_BIT_3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : USR_1_Pin */
  GPIO_InitStruct.Pin = USR_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USR_1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : HANDLE_INP_1_Pin HANDLE_INP_2_Pin STAND_INP_Pin SW_2_Pin */
  GPIO_InitStruct.Pin = HANDLE_INP_1_Pin|HANDLE_INP_2_Pin|STAND_INP_Pin|SW_2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : USR_2_Pin USR_3_Pin USR_4_Pin SPI2_SD_CS_Pin
                           SPI2_DC_Pin SPI2_RST_Pin SPI2_CS_Pin */
  GPIO_InitStruct.Pin = USR_2_Pin|USR_3_Pin|USR_4_Pin|SPI2_SD_CS_Pin
                          |SPI2_DC_Pin|SPI2_RST_Pin|SPI2_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : SW_1_Pin SW_3_Pin */
  GPIO_InitStruct.Pin = SW_1_Pin|SW_3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

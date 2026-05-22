/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "can.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "bme280.h"
#include "state_machine.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static SystemState current_state = STATE_NORMAL;
static SystemState previous_state = STATE_NORMAL;

/* CAN handles */
CAN_TxHeaderTypeDef tx_header;
CAN_RxHeaderTypeDef rx_header;
uint8_t tx_data[8] = {0};
uint8_t rx_data[8] = {0};
uint32_t tx_mailbox;
volatile uint8_t can_rx_flag = 0;  /* set by ISR when frame arrives */
volatile uint8_t can_rx_count = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
int _write(int file, char *ptr, int len);
void CAN_TestInit(void);
HAL_StatusTypeDef CAN_SendTestFrame(uint8_t counter);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_CAN1_Init();
  /* USER CODE BEGIN 2 */
  printf("\r\n=== blink_test booted ===\r\n");
    printf("Build: %s %s\r\n", __DATE__, __TIME__);

    if (BME280_Init() == HAL_OK) {
        printf("BME280 calibrated and configured.\r\n\r\n");
    } else {
        printf("BME280 init FAILED.\r\n\r\n");
        while (1) { /* halt */ }
    }

    /* CAN test initialization */
      CAN_TestInit();
      printf("CAN initialized in loopback mode.\r\n\r\n");
      uint8_t tx_counter = 1;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	      /* --- Sensor read + state machine (unchanged from Day 6) --- */
	      float temp_c, hum_pct, press_hpa;
	      if (BME280_Read(&temp_c, &hum_pct, &press_hpa) == HAL_OK) {
	          current_state = classify_state(temp_c, current_state);
	          if (current_state != previous_state) {
	              printf("[uptime: %lu ms] T = %.2f °C  [%s]  *** STATE CHANGE: %s -> %s ***\r\n",
	                     HAL_GetTick(), temp_c,
	                     state_name(current_state),
	                     state_name(previous_state), state_name(current_state));
	              previous_state = current_state;
	          }
	      }

	      /* --- CAN test: send frame, wait briefly, check if it came back --- */
	      HAL_StatusTypeDef tx_status = CAN_SendTestFrame(tx_counter);

	      if (tx_status == HAL_OK) {
	          printf("TX: ID=0x%03lX  DLC=%lu  data=[%02X %02X %02X %02X]\r\n",
	                 tx_header.StdId, tx_header.DLC,
	                 tx_data[0], tx_data[1], tx_data[2], tx_data[3]);
	      } else {
	          printf("TX failed (HAL status: %d, CAN error: 0x%08lX, state: %d)\r\n",
	                 tx_status,
	                 HAL_CAN_GetError(&hcan1),
	                 HAL_CAN_GetState(&hcan1));
	      }

	      HAL_Delay(50);  /* give the ISR time to fire */

	      if (can_rx_flag) {
	          can_rx_flag = 0;
	          int match = (rx_data[0] == tx_data[0] &&
	                       rx_data[1] == tx_data[1] &&
	                       rx_data[2] == tx_data[2] &&
	                       rx_data[3] == tx_data[3]);
	          printf("RX: ID=0x%03lX  DLC=%lu  data=[%02X %02X %02X %02X]   %s\r\n",
	                 rx_header.StdId, rx_header.DLC,
	                 rx_data[0], rx_data[1], rx_data[2], rx_data[3],
	                 match ? "OK match" : "MISMATCH!");
	      } else {
	          printf("RX: (no frame received)\r\n");
	      }

	      tx_counter++;
	      HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
	      HAL_Delay(blink_interval_ms(current_state));
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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

/* ----------------------------------------------------------
 * CAN initialization + diagnostics
 * ---------------------------------------------------------- */
void CAN_TestInit(void)
{
    HAL_StatusTypeDef f_status;
    HAL_StatusTypeDef s_status;
    HAL_StatusTypeDef n_status;

    /* Configure RX filter — accept all standard IDs */
    CAN_FilterTypeDef filter;

    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;

    filter.FilterIdHigh = 0x0000;
    filter.FilterIdLow  = 0x0000;

    filter.FilterMaskIdHigh = 0x0000;
    filter.FilterMaskIdLow  = 0x0000;

    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;

    f_status = HAL_CAN_ConfigFilter(&hcan1, &filter);

    printf("HAL_CAN_ConfigFilter = %d\r\n", f_status);

    /* Start CAN peripheral */
    s_status = HAL_CAN_Start(&hcan1);

    printf("HAL_CAN_Start = %d\r\n", s_status);
    printf("CAN error = 0x%08lX\r\n", HAL_CAN_GetError(&hcan1));

    printf("MCR = 0x%08lX\r\n", hcan1.Instance->MCR);
    printf("MSR = 0x%08lX\r\n", hcan1.Instance->MSR);

    /* Enable RX FIFO0 interrupt */
    n_status = HAL_CAN_ActivateNotification(
                    &hcan1,
                    CAN_IT_RX_FIFO0_MSG_PENDING);

    printf("HAL_CAN_ActivateNotification = %d\r\n",
           n_status);

    /* Configure TX header */
    tx_header.StdId = 0x123;
    tx_header.ExtId = 0;
    tx_header.IDE = CAN_ID_STD;
    tx_header.RTR = CAN_RTR_DATA;
    tx_header.DLC = 4;
    tx_header.TransmitGlobalTime = DISABLE;
}

/* ----------------------------------------------------------
 * Send one CAN frame
 * ---------------------------------------------------------- */
HAL_StatusTypeDef CAN_SendTestFrame(uint8_t counter)
{
    tx_data[0] = counter;
    tx_data[1] = counter + 1;
    tx_data[2] = counter + 2;
    tx_data[3] = counter + 3;

    return HAL_CAN_AddTxMessage(
                &hcan1,
                &tx_header,
                tx_data,
                &tx_mailbox);
}

/* ----------------------------------------------------------
 * RX interrupt callback
 * ---------------------------------------------------------- */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    printf(">>> IRQ CALLBACK FIRED <<<\r\n");

    if (HAL_CAN_GetRxMessage(
            hcan,
            CAN_RX_FIFO0,
            &rx_header,
            rx_data) == HAL_OK)
    {
        can_rx_flag = 1;
        can_rx_count++;
    }
}


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
#ifdef USE_FULL_ASSERT
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

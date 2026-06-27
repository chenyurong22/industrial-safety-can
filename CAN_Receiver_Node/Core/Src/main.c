/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * RECEIVER NODE CODE
  * @file           : main.c
  * @brief          : CAN Receiver Node (Node B)
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "crc8.h"
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
CAN_HandleTypeDef hcan1;

TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
CAN_RxHeaderTypeDef rx_header;
uint8_t rx_data[8] = {0};
volatile uint8_t can_rx_flag = 0;

/* Day 9 integrity tracking */
static uint8_t  expected_counter = 0;
static uint8_t  first_frame      = 1;
uint32_t rx_received   = 0;
uint32_t rx_lost       = 0;
uint32_t rx_crc_errors = 0;

/* Heartbeat tracking */
volatile uint8_t hb_rx_flag = 0;
uint8_t  hb_rx_data[2] = {0};
uint32_t hb_count = 0;
uint32_t last_hb_tick = 0;

/* Servo PWM pulse widths (µs) — TIM3_CH1 CCR maps 1:1 to microseconds */
#define SERVO_HOME      1000   /* 0°   — boot reference/home position */
#define SERVO_NORMAL    1167   /* 30°  */
#define SERVO_WARNING   1333   /* 60°  */
#define SERVO_CRITICAL  1500   /* 90°  */
#define SERVO_SAFE      2000   /* 180° — fail-safe full-open */

/* Rate-limited movement: step size and delay bound inrush current so the
   SG90 can run off the Nucleo 5V rail without browning out the board. */
#define SERVO_STEP_US      20   /* µs of pulse change per step  */
#define SERVO_STEP_DELAY    5   /* ms between steps             */

static uint16_t servo_current_us = SERVO_HOME;  /* tracks last commanded position */

/* Safe-state / watchdog tracking */
#define HEARTBEAT_TIMEOUT_MS  500
#define WATCHDOG_GRACE_MS     1000   /* no safe-state entry during first second after boot */
static uint8_t  in_safe_state = 0;
uint32_t watchdog_trips = 0;
static uint32_t last_safe_blink = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CAN1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */
int _write(int file, char *ptr, int len);
void CAN_ReceiverInit(void);
void servo_write(uint16_t pulse_us);
void servo_move_to(uint16_t target_us);
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
  MX_CAN1_Init();
  MX_USART2_UART_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  printf("\r\n=== CAN_Receiver_Node (Node B) booted ===\r\n");

  CAN_ReceiverInit();

  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);

  /* Home the servo to 0° as a known reference position, then move to NORMAL.
     Both moves are rate-limited to avoid browning out the Nucleo 5V rail. */
  servo_write(SERVO_HOME);          /* snap internal state to home  */
  HAL_Delay(400);                   /* let it settle visibly at 0°  */
  servo_move_to(SERVO_NORMAL);      /* gradual move to 30°          */
  printf("Servo homed to 0 deg, moved to NORMAL (30 deg) on TIM3_CH1 (PC6).\r\n");

  /* Seed the heartbeat timestamp so we don't trip safe state before the first beat */
  last_hb_tick = HAL_GetTick();

  printf("CAN ready. Validating CRC + counter integrity + heartbeat watchdog...\r\n\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

      /* ---- Sensor frame handling (Day 9) ---- */
      if (can_rx_flag)
      {
          can_rx_flag = 0;

          uint8_t rx_counter = rx_data[0];
          uint8_t crc_calc   = crc8_compute(rx_data, 7);
          uint8_t crc_recv   = rx_data[7];

          if (crc_calc != crc_recv)
          {
              rx_crc_errors++;
              printf("[%lu ms] CRC FAIL cnt=%u recv=0x%02X calc=0x%02X | recv=%lu lost=%lu crcErr=%lu\r\n",
                     HAL_GetTick(), rx_counter, crc_recv, crc_calc,
                     rx_received, rx_lost, rx_crc_errors);
          }
          else
          {
              if (first_frame) {
                  first_frame = 0;
              } else {
                  uint8_t gap = (uint8_t)(rx_counter - expected_counter);
                  if (gap > 0) {
                      rx_lost += gap;
                      printf(">>> LOST %u frame(s) (expected %u, got %u)\r\n",
                             gap, expected_counter, rx_counter);
                  }
              }
              expected_counter = (uint8_t)(rx_counter + 1);
              rx_received++;

              int8_t  temp_int  = (int8_t)rx_data[1];
              uint8_t temp_frac = rx_data[2];
              uint8_t state     = rx_data[3];

              printf("[%lu ms] RX cnt=%u T=%d.%02u st=%u OK | recv=%lu lost=%lu crcErr=%lu\r\n",
                     HAL_GetTick(), rx_counter, temp_int, temp_frac, state,
                     rx_received, rx_lost, rx_crc_errors);
          }
      }

      /* ---- Heartbeat handling (Day 10) ---- */
      if (hb_rx_flag)
      {
          hb_rx_flag = 0;
          hb_count++;
          last_hb_tick = HAL_GetTick();
          printf("  <HB #%u recv @ %lu ms, uptime_lo=%u>\r\n",
                 hb_rx_data[0], last_hb_tick, hb_rx_data[1]);
      }

      /* ---- Heartbeat-timeout watchdog + safe state (Day 11) ---- */
      uint32_t now = HAL_GetTick();
      uint32_t since_hb = now - last_hb_tick;

      /* Grace period: no safe-state entry during the first second after boot. */
      if (now > WATCHDOG_GRACE_MS && since_hb > HEARTBEAT_TIMEOUT_MS)
      {
          if (!in_safe_state)
          {
              in_safe_state = 1;
              watchdog_trips++;
              printf("\r\n*** SAFE STATE: heartbeat timeout (%lu ms) — servo->SAFE, trips=%lu ***\r\n",
                     since_hb, watchdog_trips);
              servo_move_to(SERVO_SAFE);   /* gradual move to 180° fail-safe */
          }

          if (now - last_safe_blink >= 100)
          {
              last_safe_blink = now;
              HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
          }
      }
      else
      {
          if (in_safe_state)
          {
              in_safe_state = 0;
              printf("\r\n*** RECOVERED: heartbeat restored — resuming normal operation ***\r\n");
          }

          switch (rx_data[3])
          {
              case 0:  servo_move_to(SERVO_NORMAL);   break;
              case 1:  servo_move_to(SERVO_WARNING);  break;
              case 2:  servo_move_to(SERVO_CRITICAL); break;
              default: servo_move_to(SERVO_NORMAL);   break;
          }
      }

      HAL_Delay(5);
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

/**
  * @brief CAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 6;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_11TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = ENABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = ENABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

  /* USER CODE END CAN1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 84-1;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 20000-1;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

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
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)ptr, len, HAL_MAX_DELAY);
    return len;
}

void CAN_ReceiverInit(void)
{
    CAN_FilterTypeDef filter;

    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;

    filter.FilterIdHigh = 0x0000;
    filter.FilterIdLow = 0x0000;

    filter.FilterMaskIdHigh = 0x0000;
    filter.FilterMaskIdLow = 0x0000;

    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;

    HAL_CAN_ConfigFilter(&hcan1, &filter);

    HAL_CAN_Start(&hcan1);

    HAL_CAN_ActivateNotification(
        &hcan1,
        CAN_IT_RX_FIFO0_MSG_PENDING);
}

/* Immediate (un-ramped) write — also syncs the tracked position. */
void servo_write(uint16_t pulse_us)
{
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pulse_us);
    servo_current_us = pulse_us;
}

/* Rate-limited move: step gradually from the current position to the target,
   bounding inrush current so the SG90 runs off the Nucleo 5V rail without
   browning out the board. No-op if already at the target. */
void servo_move_to(uint16_t target_us)
{
    while (servo_current_us != target_us)
    {
        if (target_us > servo_current_us)
        {
            servo_current_us = (target_us - servo_current_us > SERVO_STEP_US)
                               ? servo_current_us + SERVO_STEP_US : target_us;
        }
        else
        {
            servo_current_us = (servo_current_us - target_us > SERVO_STEP_US)
                               ? servo_current_us - SERVO_STEP_US : target_us;
        }
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, servo_current_us);
        HAL_Delay(SERVO_STEP_DELAY);
    }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef hdr;
    uint8_t data[8];

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &hdr, data) == HAL_OK)
    {
        if (hdr.StdId == 0x100) {          /* heartbeat */
            hb_rx_data[0] = data[0];
            hb_rx_data[1] = data[1];
            hb_rx_flag = 1;
        } else {                           /* sensor frame (0x123) */
            rx_header = hdr;
            for (int i = 0; i < 8; i++) rx_data[i] = data[i];
            can_rx_flag = 1;
        }
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

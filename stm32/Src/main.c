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
#include "lwip.h"
#include "string.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/tcp.h"
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

TIM_HandleTypeDef htim2;

UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
static struct tcp_pcb *http_pcb = NULL;

extern struct netif gnetif;

uint8_t ipPrinted = 0;

volatile uint8_t httpUpdatePending = 0;

uint32_t lastHttpUpdate = 0;

typedef enum {
  WAITING,
  ENTERING,
  RUNNING
} ChronometerState;

volatile ChronometerState state = WAITING;
volatile uint32_t newChronometer = 0;
volatile uint32_t timer_flag = 0; // Flag to indicate that the timer has elapsed
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART3_UART_Init(void);
/* USER CODE BEGIN PFP */
static void send_test_post(void);
static err_t http_connected(void *arg,struct tcp_pcb *tpcb, err_t err);
static err_t http_receive(void *arg,struct tcp_pcb *tpcb,struct pbuf *p,err_t err);
static void http_error(void *arg,err_t err);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
volatile uint32_t elapsedTime = 0; // counter for elapsed time in milliseconds
volatile uint32_t lastButtonPressTime = 0; // Variable to hold the last button press time
volatile uint32_t finalCount = 0;   // Saved TIM2 value at Stop press
volatile uint8_t stopEvent = 0;     // Tells main loop a stop occurred
char msg [100]; // Buffer to hold the message to be sent over UART
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

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
  MX_TIM2_Init();
  MX_USART3_UART_Init();
  MX_LWIP_Init();
  /* USER CODE BEGIN 2 */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    // Process incoming/outgoing Ethernet packets
    MX_LWIP_Process();

     if (ipPrinted == 0 &&
      netif_is_link_up(&gnetif) &&
      !ip4_addr_isany_val(*netif_ip4_addr(&gnetif)))
      {
      const ip4_addr_t *ip = netif_ip4_addr(&gnetif);

      int len = snprintf(
          msg,
          sizeof(msg),
          "STM32 IP: %u.%u.%u.%u\r\n",
          ip4_addr1(ip),
          ip4_addr2(ip),
          ip4_addr3(ip),
          ip4_addr4(ip)
        );

        HAL_UART_Transmit(
          &huart3,
          (uint8_t *)msg,
          len,
          HAL_MAX_DELAY
        );

          ipPrinted = 1;
          httpUpdatePending = 1;
         }

        // Request a periodic update every 60 seconds
        if (ipPrinted == 1 &&
        (HAL_GetTick() - lastHttpUpdate >= 60000))
          {
        httpUpdatePending = 1;
      }


      // Send update if one is pending and no HTTP connection is already active
        if (ipPrinted == 1 &&
        httpUpdatePending == 1 &&
        http_pcb == NULL)
      {
        httpUpdatePending = 0;
         lastHttpUpdate = HAL_GetTick();

         send_test_post();
      }

       if (state == WAITING && newChronometer == 0 && stopEvent == 0)
      {   
        
          HAL_TIM_OC_Stop_IT(&htim2, TIM_CHANNEL_1); // Stop the timer in output compare mode with interrupt
          state = WAITING; // Reset the state to waiting
          int len = snprintf(msg, sizeof(msg), "Press button to start \r\n");
          HAL_UART_Transmit(&huart3, (uint8_t *)msg, len, HAL_MAX_DELAY);
          HAL_GPIO_WritePin(EXT_GREEN_LED_GPIO_Port, EXT_GREEN_LED_Pin, GPIO_PIN_RESET); // Turn off the LED
          newChronometer = 1; // Set the flag to indicate that we are waiting for button press
       
    }
      else if (state == ENTERING)
      {
          __HAL_TIM_SET_COUNTER(&htim2, 0); // Set the counter value to 0
          __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 10000);// Set the compare value to 10000
          HAL_TIM_OC_Start_IT(&htim2, TIM_CHANNEL_1);// Start the timer in output compare mode with interrupt
          HAL_GPIO_WritePin(EXT_GREEN_LED_GPIO_Port, EXT_GREEN_LED_Pin, GPIO_PIN_SET); // Turn on the LED
          
          // Print the start/counting message once
          int len = snprintf(msg, sizeof(msg),"Counting... Press button to stop\r\n");
          HAL_UART_Transmit(&huart3, (uint8_t *)msg, len, HAL_MAX_DELAY);
          state = RUNNING; // Change the state to running

          // Send new RUNNING state to server
          httpUpdatePending = 1;
      }
      else if (state == RUNNING && timer_flag == 1) // Check if the timer has elapsed
          { 
              timer_flag = 0; // Reset the flag to indicate that we have handled the timer event
              int len = snprintf(msg, sizeof(msg),"Time in MS: %lu\r\n",elapsedTime / 1000); // Convert microseconds to milliseconds
              HAL_UART_Transmit(&huart3, (uint8_t *)msg, len, HAL_MAX_DELAY);
          }

      if (stopEvent == 1)
          {
            // Stop TIM2. We already saved the important counter value,
            // so it does not matter if CNT moved slightly before this executes.
          HAL_TIM_OC_Stop_IT(&htim2, TIM_CHANNEL_1);

          // Convert microseconds to milliseconds.
          uint32_t milliseconds = finalCount / 1000;

            int len = snprintf(msg, sizeof(msg), "Total time: %lu ms\r\n", milliseconds);
           HAL_UART_Transmit(&huart3,(uint8_t *)msg,len, HAL_MAX_DELAY);
           stopEvent = 0;

           // Tell Mongoose that the chronometer stopped
           httpUpdatePending = 1;  
          }
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
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 108;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
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

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
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

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 108-1;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4294967295;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OC_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_TIMING;
  sConfigOC.Pulse = 10000;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_OC_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

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
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(EXT_GREEN_LED_GPIO_Port, EXT_GREEN_LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : EXT_GREEN_LED_Pin */
  GPIO_InitStruct.Pin = EXT_GREEN_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(EXT_GREEN_LED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USER_BTN_EXTI_Pin */
  GPIO_InitStruct.Pin = USER_BTN_EXTI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USER_BTN_EXTI_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  uint32_t now = HAL_GetTick(); // Get the current tick value
  if(GPIO_Pin == USER_BTN_EXTI_Pin && now - lastButtonPressTime > 40) // Check if the button press is not a bounce
  {
    lastButtonPressTime = now;// Update the last button press time
  
  if (state == WAITING) // Check if the interrupt is from the user button
  {
    state = ENTERING; // Change the state to running
    newChronometer = 1; // Set the flag to indicate that we are running
  }

  if (state == RUNNING) // Check if the interrupt is from the user button and the state is running
  {
    finalCount = __HAL_TIM_GET_COUNTER(&htim2); // Save the current TIM2 value to finalCount
    stopEvent = 1; // Set the flag to indicate that a stop event has occurred
    state = WAITING; // Reset the state to waiting
    newChronometer = 0; // Reset the flag to indicate that we are waiting for button press
    timer_flag = 0; // Reset the flag to indicate that we have handled the timer event
  }

}
} 

void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2 &&
        htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
    {
        elapsedTime = __HAL_TIM_GET_COUNTER(htim); // Update the elapsed time counter
        uint32_t next_compare = __HAL_TIM_GET_COMPARE(htim, TIM_CHANNEL_1) + 10000U;
        __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, next_compare);
        timer_flag = 1; // Set the flag to indicate that the timer has elapsed
        
    }
}
static void http_error(void *arg, err_t err)
{
    http_pcb = NULL;
    int len = snprintf(msg, sizeof(msg), "HTTP TCP error: %d\r\n", err);
    HAL_UART_Transmit( &huart3,(uint8_t *)msg,len,HAL_MAX_DELAY);
}


static err_t http_receive(void *arg, struct tcp_pcb *tpcb, struct pbuf *p,err_t err)
{
    if (p == NULL)
    {
        // Server closed the connection
        tcp_close(tpcb);
        http_pcb = NULL;

        return ERR_OK;
    }

    if (err == ERR_OK)
    {
        // Tell LwIP that we received these bytes
        tcp_recved(tpcb, p->tot_len);

        // Print a header
        const char header[] = "\r\nServer response:\r\n";
        HAL_UART_Transmit(&huart3,(uint8_t *)header, sizeof(header) - 1,HAL_MAX_DELAY);

        // A TCP packet may consist of several chained pbufs.
        struct pbuf *q = p;

        while (q != NULL)
        {
            HAL_UART_Transmit(&huart3,(uint8_t *)q->payload,q->len,HAL_MAX_DELAY);
            q = q->next;
        }

        const char ending[] = "\r\n";

        HAL_UART_Transmit(
            &huart3,
            (uint8_t *)ending,
            sizeof(ending) - 1,
            HAL_MAX_DELAY
        );
    }

    pbuf_free(p);

    return ERR_OK;
}

static err_t http_connected(void *arg,struct tcp_pcb *tpcb,err_t err)
{
    if (err != ERR_OK)
    {
        return err;
    }

    int len = snprintf(msg,sizeof(msg),"Connected to Mongoose server\r\n");

    HAL_UART_Transmit(&huart3,(uint8_t *)msg,len,HAL_MAX_DELAY);

    // Receive the server response
    tcp_recv(
        tpcb,
        http_receive
    );

    // -------------------------------------------------
    // Get actual chronometer values
    // -------------------------------------------------
    const char *state_text;
    uint32_t current_ms;

    if (state == RUNNING)
    {
        state_text = "RUNNING";

        // TIM2 is counting microseconds
        current_ms =
            __HAL_TIM_GET_COUNTER(&htim2) / 1000;
    }

    else if (state == ENTERING)
    {
        state_text = "ENTERING";
        current_ms = 0;
    }

    else
    {
        state_text = "WAITING";

        // Show the last stopped time
        current_ms = finalCount / 1000;
    }

    // -------------------------------------------------
    // Create JSON body
    // -------------------------------------------------

    char body[100];

    snprintf(body,sizeof(body),"{\"state\":\"%s\",\"elapsed_ms\":%lu}",state_text,(unsigned long) current_ms);

    // -------------------------------------------------
    // Create HTTP request
    // -------------------------------------------------

    char request[300];

    int request_length = snprintf(
        request,
        sizeof(request),

        "POST /api/chronometer HTTP/1.1\r\n"
        "Host: 192.168.10.115:8000\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",

        (unsigned int) strlen(body),
        body
    );


    // -------------------------------------------------
    // Send HTTP request
    // -------------------------------------------------

    err_t result = tcp_write(tpcb,request,request_length,TCP_WRITE_FLAG_COPY);

    if (result == ERR_OK)
    {
        result = tcp_output(tpcb);
    }

    if (result == ERR_OK)
    {
        len = snprintf(msg,sizeof(msg),"HTTP POST sent: %s\r\n",body);

        HAL_UART_Transmit(&huart3,(uint8_t *)msg,len,HAL_MAX_DELAY);
    }

    return result;
}

static void send_test_post(void)
{
    ip_addr_t server_ip;
    IP4_ADDR(&server_ip,192, 168, 10, 115);http_pcb = tcp_new();

    if (http_pcb == NULL)
    {
        int len = snprintf(msg, sizeof(msg), "Could not create TCP connection\r\n");
        HAL_UART_Transmit(&huart3,(uint8_t *)msg, len, HAL_MAX_DELAY);
        return;
    }

    tcp_err(http_pcb,http_error);
    err_t result = tcp_connect(http_pcb, &server_ip,8000, http_connected);

    if (result != ERR_OK)
    {
        int len = snprintf(msg, sizeof(msg), "tcp_connect failed: %d\r\n", result);

        HAL_UART_Transmit(&huart3,(uint8_t *)msg,len, HAL_MAX_DELAY);
        tcp_abort(http_pcb);
        http_pcb = NULL;
    }
}

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

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
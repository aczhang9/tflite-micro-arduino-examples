/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#define EXCLUDE_AUDIO_PROVIDER
#ifndef EXCLUDE_AUDIO_PROVIDER

#include "audio_provider.h"
#include "tensorflow/lite/micro/micro_log.h"

#include "micro_features_micro_model_settings.h"
#include <PDM.h> //Include PDM library included with the Aruino_Apollo3 core
AP3_PDM myPDM;   //Create instance of PDM class

namespace {
//int16_t g_dummy_audio_data[kMaxAudioSampleSize];
int16_t pdmDataBuffer[kMaxAudioSampleSize];
int32_t g_latest_audio_timestamp = 0;
}  // namespace

TfLiteStatus GetAudioSamples(int start_ms, int duration_ms,
                             int* audio_samples_size, int16_t** audio_samples) {
  if (myPDM.begin() == false) // Turn on PDM with default settings, start interrupts
  {
    Serial.println("PDM Init failed. Are you sure these pins are PDM capable?");
    while (1);
  }
  Serial.println("PDM Initialized");
  printPDMConfig();

  if (myPDM.available())
  {
    //myPDM.getData(pdmDataBuffer, kMaxAudioSampleSize);
    Serial.printf("PDM available");
  }

  for (int i = 0; i < kMaxAudioSampleSize; ++i) {
    pdmDataBuffer[i] = 0;
  }
  *audio_samples_size = kMaxAudioSampleSize;
  *audio_samples = pdmDataBuffer;
  return kTfLiteOk;
}

int32_t LatestAudioTimestamp() {
  g_latest_audio_timestamp += 100;
  return g_latest_audio_timestamp;
}

//*****************************************************************************
//
// Print PDM configuration data.
//
//*****************************************************************************
void printPDMConfig(void)
{
  uint32_t PDMClk;
  uint32_t MClkDiv;
  float frequencyUnits;
  uint32_t sampleFreq;

  //
  // Read the config structure to figure out what our internal clock is set
  // to.
  //
  switch (myPDM.getClockDivider())
  {
  case AM_HAL_PDM_MCLKDIV_4:
    MClkDiv = 4;
    break;
  case AM_HAL_PDM_MCLKDIV_3:
    MClkDiv = 3;
    break;
  case AM_HAL_PDM_MCLKDIV_2:
    MClkDiv = 2;
    break;
  case AM_HAL_PDM_MCLKDIV_1:
    MClkDiv = 1;
    break;

  default:
    MClkDiv = 0;
  }

  switch (myPDM.getClockSpeed())
  {
  case AM_HAL_PDM_CLK_12MHZ:
    PDMClk = 12000000;
    break;
  case AM_HAL_PDM_CLK_6MHZ:
    PDMClk = 6000000;
    break;
  case AM_HAL_PDM_CLK_3MHZ:
    PDMClk = 3000000;
    break;
  case AM_HAL_PDM_CLK_1_5MHZ:
    PDMClk = 1500000;
    break;
  case AM_HAL_PDM_CLK_750KHZ:
    PDMClk = 750000;
    break;
  case AM_HAL_PDM_CLK_375KHZ:
    PDMClk = 375000;
    break;
  case AM_HAL_PDM_CLK_187KHZ:
    PDMClk = 187000;
    break;

  default:
    PDMClk = 0;
  }

  //
  // Record the effective sample frequency. We'll need it later to print the
  // loudest frequency from the sample.
  //
  sampleFreq = (PDMClk / (MClkDiv * 2 * myPDM.getDecimationRate()));

  frequencyUnits = (float)sampleFreq / (float)kMaxAudioSampleSize;

  Serial.printf("Settings:\n");
  Serial.printf("PDM Clock (Hz):         %12d\n", PDMClk);
  Serial.printf("Decimation Rate:        %12d\n", myPDM.getDecimationRate());
  Serial.printf("Effective Sample Freq.: %12d\n", sampleFreq);
  Serial.printf("FFT Length:             %12d\n\n", kMaxAudioSampleSize);
  Serial.printf("FFT Resolution: %15.3f Hz\n", frequencyUnits);
}
#endif // EXCLUDE_AUDIO_PROVIDER

/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

// AM_BSP_NUM_LEDS : LED initialization and management per EVB target (# of
// LEDs defined in EVB BSP) 

#include "audio_provider.h"

#include <limits>

// These are headers from Ambiq's Apollo3 SDK.
#include "am_bsp.h"         // NOLINT
#include "am_mcu_apollo.h"  // NOLINT
#include "am_util.h"        // NOLINT

#include "micro_features_micro_model_settings.h"
#include <PDM.h>
namespace {

// These are the raw buffers that are filled by the PDM during DMA
constexpr int kPdmNumSlots = 1;
constexpr int kPdmSamplesPerSlot = 256;
constexpr int kPdmSampleBufferSize = (kPdmNumSlots * kPdmSamplesPerSlot);
uint32_t g_ui32PDMSampleBuffer0[kPdmSampleBufferSize];
uint32_t g_ui32PDMSampleBuffer1[kPdmSampleBufferSize];
uint32_t g_PowerOff = 0;

// Controls the double buffering between the two DMA buffers.
int g_dma_destination_index = 0;
// PDM Device Handle.
static void* g_pdm_handle;
// PDM DMA error flag.
volatile bool g_pdm_dma_error;
// So the interrupt can use the passed-in error handler to report issues.
tflite::ErrorReporter* g_pdm_dma_error_reporter = nullptr;

// Holds a longer history of audio samples in a ring buffer.
constexpr int kAudioCaptureBufferSize = 16000;
int16_t g_audio_capture_buffer[kAudioCaptureBufferSize] = {};
int g_audio_capture_buffer_start = 0;
int64_t g_total_samples_captured = 0;
int32_t g_latest_audio_timestamp = 0;

// Copy of audio samples returned to the caller.
int16_t g_audio_output_buffer[kMaxAudioSampleSize];
bool g_is_audio_initialized = false; // if this flag is set to true, fault does not occur

//*****************************************************************************
//
// Globals
//
//*****************************************************************************

// ARPIT TODO : Implement low power configuration
void custom_am_bsp_low_power_init(void) {
  // Initialize for low power in the power control block
  // am_hal_pwrctrl_low_power_init();

  // Run the RTC off the LFRC.
  // am_hal_rtc_osc_select(AM_HAL_RTC_OSC_LFRC);

  // Stop the XTAL.
  // am_hal_clkgen_control(AM_HAL_CLKGEN_CONTROL_XTAL_STOP, 0);

  // Disable the RTC.
  // am_hal_rtc_osc_disable();

}  // am_bsp_low_power_init()

// Make sure the CPU is running as fast as possible.
void enable_burst_mode(tflite::ErrorReporter* error_reporter) {
  am_hal_burst_avail_e eBurstModeAvailable;
  am_hal_burst_mode_e eBurstMode;

  // Check that the Burst Feature is available.
  if (AM_HAL_STATUS_SUCCESS ==
      am_hal_burst_mode_initialize(&eBurstModeAvailable)) {
    if (AM_HAL_BURST_AVAIL == eBurstModeAvailable) {
      error_reporter->Report("Apollo3 Burst Mode is Available\n");
    } else {
      error_reporter->Report("Apollo3 Burst Mode is Not Available\n");
    }
  } else {
    error_reporter->Report("Failed to Initialize for Burst Mode operation\n");
  }

  // Put the MCU into "Burst" mode.
  if (AM_HAL_STATUS_SUCCESS == am_hal_burst_mode_enable(&eBurstMode)) {
    if (AM_HAL_BURST_MODE == eBurstMode) {
      error_reporter->Report("Apollo3 operating in Burst Mode (96MHz)\n");
    }
  } else {
    error_reporter->Report("Failed to Enable Burst Mode operation\n");
  }
}

}  // namespace

//*****************************************************************************
// PDM configuration information.
//*****************************************************************************
am_hal_pdm_config_t g_sPdmConfig = {
    .eClkDivider = AM_HAL_PDM_MCLKDIV_1,
    .eLeftGain = AM_HAL_PDM_GAIN_P165DB,
    .eRightGain = AM_HAL_PDM_GAIN_P165DB,
    .ui32DecimationRate =
        48,  // OSR = 1500/16 = 96 = 2*SINCRATE --> SINC_RATE = 48
    .bHighPassEnable = 1,
    .ui32HighPassCutoff = 0x2,
    .ePDMClkSpeed = AM_HAL_PDM_CLK_1_5MHZ,
    .bInvertI2SBCLK = 0,
    .ePDMClkSource = AM_HAL_PDM_INTERNAL_CLK,
    .bPDMSampleDelay = 0,
    .bDataPacking = 0,
    .ePCMChannels = AM_BSP_PDM_CHANNEL,
    .ui32GainChangeDelay = 1,
    .bI2SEnable = 0,
    .bSoftMute = 0,
    .bLRSwap = 0,
};

//*****************************************************************************
// PDM initialization.
//*****************************************************************************
extern "C" void pdm_init(void) {
  //
  // Initialize, power-up, and configure the PDM.
  //
  am_hal_pdm_initialize(0, &g_pdm_handle);
  am_hal_pdm_power_control(g_pdm_handle, AM_HAL_PDM_POWER_ON, false);
  am_hal_pdm_configure(g_pdm_handle, &g_sPdmConfig);

  //
  // Configure the necessary pins.
  //
  am_hal_gpio_pinconfig(MIC_CLOCK, g_AM_BSP_PDM_CLOCK);
  am_hal_gpio_pinconfig(MIC_DATA, g_AM_BSP_PDM_DATA);

  //
  // Configure and enable PDM interrupts (set up to trigger on DMA
  // completion).
  //
  am_hal_pdm_interrupt_enable(g_pdm_handle, ( AM_HAL_PDM_INT_DERR   | 
                                              AM_HAL_PDM_INT_DCMP   |
                                              AM_HAL_PDM_INT_UNDFL  | 
                                              AM_HAL_PDM_INT_OVF));

  NVIC_EnableIRQ(PDM_IRQn);

  // Enable PDM
  am_hal_pdm_enable(g_pdm_handle);
}

// Start the DMA fetch of PDM samples.
void pdm_start_dma(tflite::ErrorReporter* error_reporter) {
  // Configure DMA and target address.
  am_hal_pdm_transfer_t sTransfer;

  if (g_dma_destination_index == 0) {
    sTransfer.ui32TargetAddr = (uint32_t)g_ui32PDMSampleBuffer0;
  } else {
    sTransfer.ui32TargetAddr = (uint32_t)g_ui32PDMSampleBuffer1;
  }

  sTransfer.ui32TotalCount = 4 * kPdmSampleBufferSize;
  // PDM DMA count is in Bytes

  // Start the data transfer.
  if (AM_HAL_STATUS_SUCCESS != am_hal_pdm_dma_start(g_pdm_handle, &sTransfer)) {
    //error_reporter->Report("Error - configuring PDM DMA failed.");
  }

  // Reset the PDM DMA flags.
  g_pdm_dma_error = false;
}

// Interrupt handler for the PDM.
extern "C" void am_pdm_isr(void) {
  uint32_t ui32IntMask;

  // Read the interrupt status.
  if (AM_HAL_STATUS_SUCCESS !=
      am_hal_pdm_interrupt_status_get(g_pdm_handle, &ui32IntMask, false)) {
    g_pdm_dma_error_reporter->Report("Error reading PDM0 interrupt status.");
  }

  // Clear the PDM interrupt.
  if (AM_HAL_STATUS_SUCCESS !=
      am_hal_pdm_interrupt_clear(g_pdm_handle, ui32IntMask)) {
    g_pdm_dma_error_reporter->Report("Error clearing PDM interrupt status.");
  }

#if USE_DEBUG_GPIO
  // DEBUG : GPIO flag polling.
  am_hal_gpio_state_write(31, AM_HAL_GPIO_OUTPUT_SET);  // Slot1 AN pin
#endif

  // If we got a DMA complete, set the flag.
  if (ui32IntMask & AM_HAL_PDM_INT_OVF) {
    am_util_stdio_printf("\n%s\n", "\nPDM ISR OVF.");
  }
  if (ui32IntMask & AM_HAL_PDM_INT_UNDFL) {
    am_util_stdio_printf("\n%s\n", "\nPDM ISR UNDLF.");
  }
  if (ui32IntMask & AM_HAL_PDM_INT_DCMP) {
    uint32_t* source_buffer;
    if (g_dma_destination_index == 0) {
      source_buffer = g_ui32PDMSampleBuffer0;
      g_dma_destination_index = 1;
    } else {
      source_buffer = g_ui32PDMSampleBuffer1;
      g_dma_destination_index = 0;
    }
    pdm_start_dma(g_pdm_dma_error_reporter);

    uint32_t slotCount = 0;
    for (uint32_t indi = 0; indi < kPdmSampleBufferSize; indi++) {
      g_audio_capture_buffer[g_audio_capture_buffer_start] =
          source_buffer[indi];
      g_audio_capture_buffer_start =
          (g_audio_capture_buffer_start + 1) % kAudioCaptureBufferSize;
      slotCount++;
    }

    g_total_samples_captured += slotCount;
    g_latest_audio_timestamp =
        (g_total_samples_captured / (kAudioSampleFrequency / 1000));
  }

  // If we got a DMA error, set the flag.
  if (ui32IntMask & AM_HAL_PDM_INT_DERR) {
    g_pdm_dma_error = true;
  }

#if USE_DEBUG_GPIO
  // DEBUG : GPIO flag polling.
  am_hal_gpio_state_write(31, AM_HAL_GPIO_OUTPUT_CLEAR);  // Slot1 AN pin
#endif
}

TfLiteStatus InitAudioRecording(tflite::ErrorReporter* error_reporter) {
  // Set the clock frequency.
  if (AM_HAL_STATUS_SUCCESS !=
      am_hal_clkgen_control(AM_HAL_CLKGEN_CONTROL_SYSCLK_MAX, 0)) {
    error_reporter->Report("Error - configuring the system clock failed.");
    return kTfLiteError;
  }

  // Individually select elements of am_bsp_low_power_init
  custom_am_bsp_low_power_init();

  // Set the default cache configuration and enable it.
  if (AM_HAL_STATUS_SUCCESS !=
      am_hal_cachectrl_config(&am_hal_cachectrl_defaults)) {
    error_reporter->Report("Error - configuring the system cache failed.");
    return kTfLiteError;
  }
  if (AM_HAL_STATUS_SUCCESS != am_hal_cachectrl_enable()) {
    error_reporter->Report("Error - enabling the system cache failed.");
    return kTfLiteError;
  }

  // Configure Flash wait states.
  CACHECTRL->FLASHCFG_b.RD_WAIT = 1;      // Default is 3
  CACHECTRL->FLASHCFG_b.SEDELAY = 6;      // Default is 7
  CACHECTRL->FLASHCFG_b.LPM_RD_WAIT = 5;  // Default is 8

  // this block produces Mbed OS fault
  // Enable cache sleep states.
  uint32_t ui32LPMMode = CACHECTRL_FLASHCFG_LPMMODE_STANDBY;
  /*
  if (am_hal_cachectrl_control(AM_HAL_CACHECTRL_CONTROL_LPMMODE_SET,
                               &ui32LPMMode)) {
    error_reporter->Report("Error - enabling cache sleep state failed.");
  }

  // Enable Instruction & Data pre-fetching.
  MCUCTRL->SRAMMODE_b.DPREFETCH = 1;
  MCUCTRL->SRAMMODE_b.DPREFETCH_CACHE = 1;
  MCUCTRL->SRAMMODE_b.IPREFETCH = 1;
  MCUCTRL->SRAMMODE_b.IPREFETCH_CACHE = 1;

  // Enable the floating point module, and configure the core for lazy stacking.
  
  am_hal_sysctrl_fpu_enable();
  am_hal_sysctrl_fpu_stacking_enable(true);
  error_reporter->Report("FPU Enabled."); // this is reported
  
  // Ensure the CPU is running as fast as possible.
  // enable_burst_mode(error_reporter);

  // Configure, turn on PDM
  pdm_init();
  
  am_hal_interrupt_master_enable();
  am_hal_pdm_fifo_flush(g_pdm_handle);
  // Trigger the PDM DMA for the first time manually.
  g_pdm_dma_error_reporter = error_reporter;
  pdm_start_dma(g_pdm_dma_error_reporter);

  error_reporter->Report("\nPDM DMA Threshold = %d", PDMn(0)->FIFOTHR); // this is reported
  */
  return kTfLiteOk;
}

TfLiteStatus GetAudioSamples(tflite::ErrorReporter* error_reporter,
                             int start_ms, int duration_ms,
                             int* audio_samples_size, int16_t** audio_samples) {
  if (!g_is_audio_initialized) { //set this flag to true to skip this block and re-run
    TfLiteStatus init_status = InitAudioRecording(error_reporter); // fault here
    if (init_status != kTfLiteOk) {
      return init_status;
    }
    g_is_audio_initialized = true;
  }

  // This should only be called when the main thread notices that the latest
  // audio sample data timestamp has changed, so that there's new data in the
  // capture ring buffer. The ring buffer will eventually wrap around and
  // overwrite the data, but the assumption is that the main thread is checking
  // often enough and the buffer is large enough that this call will be made
  // before that happens.
  const int start_offset =
      (start_ms < 0) ? 0 : start_ms * (kAudioSampleFrequency / 1000);
  const int duration_sample_count =
      duration_ms * (kAudioSampleFrequency / 1000);
  for (int i = 0; i < duration_sample_count; ++i) {
    const int capture_index = (start_offset + i) % kAudioCaptureBufferSize;
    g_audio_output_buffer[i] = g_audio_capture_buffer[capture_index];
  }
  
  *audio_samples_size = kMaxAudioSampleSize;
  *audio_samples = g_audio_output_buffer;

  return kTfLiteOk;
}

int32_t LatestAudioTimestamp() { 
   g_latest_audio_timestamp += 100;
   return g_latest_audio_timestamp; 
   }

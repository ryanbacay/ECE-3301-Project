#ifndef LEVEL_COMPASS_DISPLAY_H
#define LEVEL_COMPASS_DISPLAY_H

#include "main.h"

#define DISPLAY_WIDTH       240U
#define DISPLAY_HEIGHT      240U
#define DISPLAY_BUFFER_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT / 2U)

/* Adafruit 4313, ST7789 TFT. These defaults use the repo's PA4 CS output.
 * If the TFT CS wire goes to PB0 instead, change ONLY these two definitions
 * to Display_CSB0_GPIO_Port and Display_CSB0_Pin before building.
 * Reset = PA0; D/C = PA1; SCK = PF1; MOSI = PA11.
 * The unused CS output is kept high. No microSD support is needed.
 */
#define DISPLAY_TFT_CS_PORT Display_CS_GPIO_Port
#define DISPLAY_TFT_CS_PIN  Display_CS_Pin

/* Call once AFTER MX_GPIO_Init() and MX_SPI2_Init(), from main(), not an ISR.
 * Configures SPI2 for byte transfers at about 10.625 MHz on this 170 MHz MCU.
 * Returns the HAL error if initialization or any SPI transfer fails.
 */
HAL_StatusTypeDef Display_Init(void);

/* Draw into the EXISTING main.c frame_buffer: two palette pixels per byte.
 * These functions do not allocate another framebuffer or send SPI data.
 * Level input: signed roll in degrees, positive toward the right.
 * Compass input: degrees clockwise from north (0=N, 90=E, 180=S, 270=W).
 */
void Display_DrawLevel(uint8_t *buffer, float angle_deg);
void Display_DrawCompass(uint8_t *buffer, float heading_deg);

/* Convert the packed buffer to RGB565 one row at a time and send to the TFT.
 * Call from main(), not an ISR. A zero-filled buffer (or NULL) clears to black.
 * Drawing and transfer must not run concurrently; this is a blocking driver.
 */
HAL_StatusTypeDef Display_Update(const uint8_t *buffer);

/* Integration additions for the owner of main.c (no existing files supplied):
 *
 * In USER CODE BEGIN Includes:
 *   #include "display.h"
 *   #include <math.h>    // existing telemetry uses lroundf()
 *   #include <string.h>  // existing sleep code uses memset()
 *
 * In USER CODE BEGIN 2, before BNO055 initialization:
 *   if (Display_Init() != HAL_OK) { Error_Handler(); }
 *   Display_DrawLevel(frame_buffer, NAN); // --- until the first sensor reading
 *   if (Display_Update(frame_buffer) != HAL_OK) { Error_Handler(); }
 *
 * Inside the EXISTING empty generate_levelframe():
 *   Display_DrawLevel(frame_buffer, orientation.roll_deg);
 *
 * Inside the EXISTING empty generate_headingframe():
 *   Display_DrawCompass(frame_buffer, compassHeading);
 *
 * Inside the EXISTING empty update_display():
 *   if (Display_Update(frame_buffer) != HAL_OK) { Error_Handler(); }
 *
 * Use orientation.roll_deg because the existing unsigned levelVector/tiltX
 * calculation loses the sign of negative angles. The team should fix that
 * calculation separately. Confirm sensor mounting gives positive roll when
 * tilted right; negate roll in the wrapper if the physical mounting reverses it.
 * Adding these two files alone cannot replace the static empty main.c hooks.
 * Sensor INT/PB6 must work for the existing main loop to request new frames.
 * Initialize SPI2 only before Display_Init(), not again afterward.
 */

#endif

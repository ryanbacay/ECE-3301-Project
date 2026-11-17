#include "display.h"
#include <math.h>
#include <string.h>

extern SPI_HandleTypeDef hspi2;

/* Palette indices stored in the existing 4-bits-per-pixel frame_buffer. */
enum { BLACK, WHITE, CYAN, GREEN, GRAY, YELLOW };
static const uint16_t palette[16] = {
    0x0000, 0xFFFF, 0x07FF, 0x07E0, 0x8410, 0xFFE0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Only 480 extra pixel bytes in RAM, rather than a 115,200-byte RGB frame. */
static uint8_t row_pixels[DISPLAY_WIDTH * 2U];
static uint8_t initialized;

/* Plain 5x7 block letters and digits, held in flash. No font library.
 * Each byte is one column; bit 0 is the top pixel.
 */
static const uint8_t letters[26][5] = {
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
};
static const uint8_t digits[10][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}
};

static void pixel(uint8_t *buffer, int x, int y, uint8_t color)
{
    if (x < 0 || x >= (int)DISPLAY_WIDTH ||
        y < 0 || y >= (int)DISPLAY_HEIGHT) return;
    unsigned int i = (unsigned int)(y * (int)DISPLAY_WIDTH + x) / 2U;
    if ((x & 1) == 0)
        buffer[i] = (uint8_t)((buffer[i] & 0x0FU) | (color << 4));
    else
        buffer[i] = (uint8_t)((buffer[i] & 0xF0U) | color);
}

static void line(uint8_t *buffer, int x, int y, int end_x, int end_y,
                 uint8_t color)
{
    int dx = end_x > x ? end_x - x : x - end_x;
    int dy = end_y > y ? y - end_y : end_y - y;
    int step_x = x < end_x ? 1 : -1;
    int step_y = y < end_y ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        pixel(buffer, x, y, color);
        if (x == end_x && y == end_y) break;
        int twice_error = 2 * error;
        if (twice_error >= dy) { error += dy; x += step_x; }
        if (twice_error <= dx) { error += dx; y += step_y; }
    }
}

static void circle(uint8_t *buffer, int cx, int cy, int radius, uint8_t color)
{
    int x = radius, y = 0, error = 1 - radius;
    while (x >= y) {
        pixel(buffer,cx+x,cy+y,color); pixel(buffer,cx+y,cy+x,color);
        pixel(buffer,cx-y,cy+x,color); pixel(buffer,cx-x,cy+y,color);
        pixel(buffer,cx-x,cy-y,color); pixel(buffer,cx-y,cy-x,color);
        pixel(buffer,cx+y,cy-x,color); pixel(buffer,cx+x,cy-y,color);
        y++;
        if (error < 0) error += 2 * y + 1;
        else { x--; error += 2 * (y - x) + 1; }
    }
}

static void dot(uint8_t *buffer, int cx, int cy, int radius, uint8_t color)
{
    for (int y = -radius; y <= radius; y++)
        for (int x = -radius; x <= radius; x++)
            if (x*x + y*y <= radius*radius)
                pixel(buffer, cx+x, cy+y, color);
}

static uint8_t glyph_column(char c, int column)
{
    if (c >= 'A' && c <= 'Z') return letters[c-'A'][column];
    if (c >= '0' && c <= '9') return digits[c-'0'][column];
    if (c == '-') return 0x08;
    if (c == '+') return column == 2 ? 0x3E : 0x08;
    if (c == '.') return column == 2 ? 0x60 : 0;
    return 0; /* Space or an unused character. */
}

static void text(uint8_t *buffer, int x, int y, const char *s,
                 int scale, uint8_t color)
{
    for (; *s; s++, x += 6 * scale)
        for (int col = 0; col < 5; col++) {
            uint8_t bits = glyph_column(*s, col);
            for (int row = 0; row < 7; row++)
                if (bits & (1U << row))
                    for (int j = 0; j < scale; j++)
                        for (int i = 0; i < scale; i++)
                            pixel(buffer, x+col*scale+i, y+row*scale+j, color);
        }
}

static void centered_text(uint8_t *buffer, int y, const char *s,
                          int scale, uint8_t color)
{
    int width = (int)strlen(s) * 6 * scale - scale;
    text(buffer, ((int)DISPLAY_WIDTH-width)/2, y, s, scale, color);
}

/* Values are assembled as integers so printf float support is unnecessary. */
static void angle_text(uint8_t *buffer, int tenths, int show_sign,
                       int y, int scale, uint8_t color)
{
    char number[10];
    unsigned int used = 0;
    unsigned int magnitude = (unsigned int)(tenths < 0 ? -tenths : tenths);
    if (show_sign) number[used++] = tenths < 0 ? '-' : '+';
    if (magnitude >= 1000U) number[used++] = (char)('0' + magnitude/1000U);
    if (magnitude >= 100U) number[used++] = (char)('0' + (magnitude/100U)%10U);
    number[used++] = (char)('0' + (magnitude/10U)%10U);
    number[used++] = '.';
    number[used++] = (char)('0' + magnitude%10U);
    number[used] = '\0';
    int total_width = (int)used * 6 * scale + 3 * scale;
    int x = ((int)DISPLAY_WIDTH-total_width)/2;
    text(buffer, x, y, number, scale, color);
    circle(buffer, x+(int)used*6*scale+scale, y+scale, scale, color);
}

void Display_DrawLevel(uint8_t *buffer, float angle_deg)
{
    if (buffer == NULL) return;
    memset(buffer, 0, DISPLAY_BUFFER_SIZE);
    centered_text(buffer, 14, "LEVEL", 3, WHITE);
    line(buffer, 24,146,216,146,WHITE);
    line(buffer, 24,139,24,153,WHITE);
    line(buffer,216,139,216,153,WHITE);
    line(buffer,110,131,110,161,GREEN);
    line(buffer,130,131,130,161,GREEN);
    text(buffer, 15,177,"-90",2,GRAY);
    text(buffer,115,177,"0",2,GRAY);
    text(buffer,190,177,"+90",2,GRAY);

    if (!isfinite(angle_deg) || angle_deg < -180.0f || angle_deg > 180.0f) {
        centered_text(buffer, 62, "---", 4, YELLOW);
        return;
    }
    int tenths = (int)(angle_deg*10.0f + (angle_deg < 0 ? -0.5f : 0.5f));
    uint8_t color = (tenths >= -5 && tenths <= 5) ? GREEN : CYAN;
    angle_text(buffer, tenths, 1, 62, 4, color);
    float shown_angle = angle_deg;
    if (shown_angle < -90.0f) shown_angle = -90.0f;
    if (shown_angle >  90.0f) shown_angle =  90.0f;
    int x = (int)(120.0f + shown_angle * 96.0f / 90.0f + 0.5f);
    dot(buffer, x,146,6,color);
}

void Display_DrawCompass(uint8_t *buffer, float heading_deg)
{
    if (buffer == NULL) return;
    memset(buffer, 0, DISPLAY_BUFFER_SIZE);
    centered_text(buffer, 14, "COMPASS", 3, WHITE);
    circle(buffer,120,142,68,WHITE);
    text(buffer,115, 83,"N",2,WHITE);
    text(buffer,171,136,"E",2,WHITE);
    text(buffer,115,185,"S",2,WHITE);
    text(buffer, 57,136,"W",2,WHITE);
    if (!isfinite(heading_deg)) {
        centered_text(buffer, 48, "---", 3, YELLOW);
        return;
    }
    heading_deg = fmodf(heading_deg, 360.0f);
    if (heading_deg < 0) heading_deg += 360.0f;
    int tenths = (int)(heading_deg * 10.0f + 0.5f);
    if (tenths >= 3600) tenths = 0;
    /* Use the same rounded angle for pointer, direction label and number. */
    heading_deg = tenths / 10.0f;
    angle_text(buffer, tenths, 0, 48, 3, CYAN);
    static const char * const directions[8] = {
        "NORTH", "NORTH-EAST", "EAST", "SOUTH-EAST",
        "SOUTH", "SOUTH-WEST", "WEST", "NORTH-WEST"
    };
    unsigned int sector = (unsigned int)((heading_deg+22.5f)/45.0f) % 8U;
    centered_text(buffer,219,directions[sector],2,WHITE);

    float radians = heading_deg * 0.01745329252f;
    float dx = sinf(radians), dy = -cosf(radians);
    int tip_x = (int)(120.0f + 42.0f*dx + 0.5f);
    int tip_y = (int)(142.0f + 42.0f*dy + 0.5f);
    line(buffer,120,142,tip_x,tip_y,CYAN);
    line(buffer,tip_x,tip_y,
         (int)(tip_x-10.0f*dx+5.0f*dy+0.5f),
         (int)(tip_y-10.0f*dy-5.0f*dx+0.5f),CYAN);
    line(buffer,tip_x,tip_y,
         (int)(tip_x-10.0f*dx-5.0f*dy+0.5f),
         (int)(tip_y-10.0f*dy+5.0f*dx+0.5f),CYAN);
    dot(buffer,120,142,3,CYAN);
}

static HAL_StatusTypeDef command(uint8_t cmd, const uint8_t *data, uint16_t count)
{
    HAL_GPIO_WritePin(DISPLAY_TFT_CS_PORT,DISPLAY_TFT_CS_PIN,GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DC_Pin_GPIO_Port,DC_Pin_Pin,GPIO_PIN_RESET);
    HAL_StatusTypeDef result = HAL_SPI_Transmit(&hspi2,&cmd,1,100);
    if (result == HAL_OK && count != 0U) {
        HAL_GPIO_WritePin(DC_Pin_GPIO_Port,DC_Pin_Pin,GPIO_PIN_SET);
        result = HAL_SPI_Transmit(&hspi2,(uint8_t *)data,count,100);
    }
    HAL_GPIO_WritePin(DISPLAY_TFT_CS_PORT,DISPLAY_TFT_CS_PIN,GPIO_PIN_SET);
    return result;
}

HAL_StatusTypeDef Display_Init(void)
{
    initialized = 0;
    /* Both outputs already exist in MX_GPIO_Init(). Select only the TFT CS.
     * PB0 remains deselected when the default PA4 TFT CS mapping is used.
     */
    HAL_GPIO_WritePin(Display_CS_GPIO_Port,Display_CS_Pin,GPIO_PIN_SET);
    HAL_GPIO_WritePin(Display_CSB0_GPIO_Port,Display_CSB0_Pin,GPIO_PIN_SET);

    /* Four-bit palette pixels are unrelated to SPI's bits per transfer.
     * Correct the repo's 4-bit SPI setting here, inside the new display code.
     * At 170 MHz / 16, full-frame wire time is at least 87 ms (~11 FPS max).
     */
    hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    HAL_StatusTypeDef result = HAL_SPI_Init(&hspi2);
    if (result != HAL_OK) return result;

    GPIO_InitTypeDef pins = {0};
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_NOPULL;
    pins.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    pins.Alternate = GPIO_AF5_SPI2;
    pins.Pin = GPIO_PIN_1;   /* PF1 = SPI2 SCK */
    HAL_GPIO_Init(GPIOF,&pins);
    pins.Pin = GPIO_PIN_11;  /* PA11 = SPI2 MOSI */
    HAL_GPIO_Init(GPIOA,&pins);

    HAL_GPIO_WritePin(OLED_RST_GPIO_Port,OLED_RST_Pin,GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(OLED_RST_GPIO_Port,OLED_RST_Pin,GPIO_PIN_SET);
    HAL_Delay(120);
    result = command(0x01,NULL,0); /* Software reset */
    if (result != HAL_OK) return result;
    HAL_Delay(150);
    result = command(0x11,NULL,0); /* Leave sleep mode */
    if (result != HAL_OK) return result;
    HAL_Delay(120);
    const uint8_t rgb565 = 0x55;
    result = command(0x3A,&rgb565,1);
    if (result != HAL_OK) return result;
    HAL_Delay(10);
    const uint8_t orientation = 0xC0; /* Adafruit's rotation 0, RGB order */
    result = command(0x36,&orientation,1);
    if (result != HAL_OK) return result;
    result = command(0x21,NULL,0); /* IPS panel inversion on */
    if (result != HAL_OK) return result;
    HAL_Delay(10);
    result = command(0x13,NULL,0); /* Normal mode */
    if (result != HAL_OK) return result;
    HAL_Delay(10);
    /* Clear display RAM before switching on, to avoid startup garbage. */
    initialized = 1;
    result = Display_Update(NULL);
    if (result == HAL_OK) result = command(0x29,NULL,0); /* Display on */
    if (result != HAL_OK) initialized = 0;
    HAL_Delay(20);
    return result;
}

HAL_StatusTypeDef Display_Update(const uint8_t *buffer)
{
    if (!initialized) return HAL_ERROR;
    const uint8_t columns[4] = {0,0,0,239};
    /* The Adafruit 240x240 panel occupies rows 80..319 in rotation 0. */
    const uint8_t rows[4] = {0,80,1,63};
    HAL_StatusTypeDef result = command(0x2A,columns,4);
    if (result != HAL_OK) return result;
    result = command(0x2B,rows,4);
    if (result != HAL_OK) return result;

    HAL_GPIO_WritePin(DISPLAY_TFT_CS_PORT,DISPLAY_TFT_CS_PIN,GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DC_Pin_GPIO_Port,DC_Pin_Pin,GPIO_PIN_RESET);
    uint8_t ram_write = 0x2C;
    result = HAL_SPI_Transmit(&hspi2,&ram_write,1,100);
    HAL_GPIO_WritePin(DC_Pin_GPIO_Port,DC_Pin_Pin,GPIO_PIN_SET);
    for (unsigned int y = 0; y < DISPLAY_HEIGHT && result == HAL_OK; y++) {
        for (unsigned int x = 0; x < DISPLAY_WIDTH; x++) {
            uint8_t packed = buffer ? buffer[(y*DISPLAY_WIDTH+x)/2U] : 0;
            uint8_t index = (x & 1U) ? packed & 0x0FU : packed >> 4;
            uint16_t color = palette[index];
            row_pixels[2U*x] = (uint8_t)(color >> 8); /* MSB first */
            row_pixels[2U*x+1U] = (uint8_t)color;
        }
        result = HAL_SPI_Transmit(&hspi2,row_pixels,sizeof(row_pixels),100);
    }
    HAL_GPIO_WritePin(DISPLAY_TFT_CS_PORT,DISPLAY_TFT_CS_PIN,GPIO_PIN_SET);
    return result;
}

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pio_matrix.pio.h"
#include "inc/ssd1306.h"
#include "inc/font.h"
#include "frames.c"
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C

uint32_t lastTime = 0;
ssd1306_t ssd; // Inicializa a estrutura do display
PIO pio;
uint sm;
int* sequenciaDeCoresAtual;
int tamanhoSequencia = 1;
char c;

//rotina da interrupção
static void gpio_irq_handler(uint gpio, uint32_t events) {
  uint32_t currentTime = to_us_since_boot(get_absolute_time());
   
  if(currentTime - lastTime > 300000) { // Debounce
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    if (gpio == 5) { // Atualiza o estado do led verde
      gpio_put(11, !gpio_get(11));
      printf("Botão A pressionado\n");
    } else if (gpio == 6) { // Atualiza o estado do ler azul
      gpio_put(12, !gpio_get(12));
      printf("Botão B pressionado\n");
    }

    // Atualiza o estado do display
    ssd1306_draw_string(&ssd, gpio_get(11)?"LED VERDE ON!":"LED VERDE OFF!", 8, 10);
    ssd1306_draw_string(&ssd, gpio_get(12)?"LED AZUL ON!":"LED AZUL OFF!", 8, 25);
    ssd1306_draw_string(&ssd, "CARACTERE LIDO: ", 8, 40);
    ssd1306_draw_char(&ssd, c, 105, 40);
    ssd1306_send_data(&ssd);
    lastTime = currentTime;
  }

  // Esse if evita que o polling seja aplicado ao scanf, dessa forma uma string pode ser lida sem que o polling interfira
  if(gpio == 0) {
    ssd1306_draw_string(&ssd, "CARACTERE LIDO: ", 8, 40);
    ssd1306_draw_char(&ssd, c, 105, 40);
    ssd1306_send_data(&ssd);
  }
}

//rotina para definição da intensidade de cores do led
uint32_t matrix_rgb(double b, double r, double g) {
    unsigned char R, G, B;
    R = r * 255;
    G = g * 255;
    B = b * 255;
    return (G << 24) | (R << 16) | (B << 8);
}

//rotina para acionar a matrix de leds - ws2812b
void desenho_pio(double *desenho, PIO pio, uint sm, int cor) {
    uint32_t valor_led;
  
    for (int16_t i = 0; i < 25; i++) {
        valor_led = matrix_rgb(cor==2?desenho[24-i]:0, cor==1 || cor==3?desenho[24-i]:0, cor==0 || cor==3?desenho[24-i]:0);
        pio_sm_put_blocking(pio, sm, valor_led);
    }
}

void drawOnLedMatrix() {
  srand(time(NULL)); // Inicializa o gerador de números aleatórios

  sequenciaDeCoresAtual[tamanhoSequencia-1] = rand() % 4; // Gera um número aleatório entre 0 e 3

  for(int i = 0; i < tamanhoSequencia; i++) {
    desenho_pio(numeros[sequenciaDeCoresAtual[i]], pio, sm, sequenciaDeCoresAtual[i]);
    sleep_ms(500);
  }

  tamanhoSequencia++;

  int *temp = realloc(sequenciaDeCoresAtual, tamanhoSequencia * sizeof(int));
  if (temp == NULL) {
      printf("Erro ao realocar memória!\n");
      free(sequenciaDeCoresAtual);  // ainda precisamos liberar a memória original
  }
  sequenciaDeCoresAtual = temp;
}

int main() {
  stdio_init_all(); // Inicializa a comunicação serial
  
  gpio_init(5); // Inicializa o pino 5 para uso do botão
  gpio_set_dir(5, GPIO_IN);
  gpio_pull_up(5);

  gpio_init(6); // Inicializa o pino 6 para uso do botão
  gpio_set_dir(6, GPIO_IN);
  gpio_pull_up(6);

  gpio_init(11); // Inicializa o pino 11 para uso do led
  gpio_set_dir(11, GPIO_OUT);

  gpio_init(12); // Inicializa o pino 12 para uso do led
  gpio_set_dir(12, GPIO_OUT);

  i2c_init(I2C_PORT, 400 * 1000); // I2C Initialisation. Using it at 400Khz.
  set_sys_clock_khz(128000, false); // Set the system clock to 128Mhz

  // Configurando a máquina PIO
  pio = pio0;
  uint offset = pio_add_program(pio, &pio_matrix_program);
  sm = pio_claim_unused_sm(pio, true);
  pio_matrix_program_init(pio, sm, offset, 7);

  gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); // Set the GPIO pin function to I2C
  gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); // Set the GPIO pin function to I2C
  gpio_pull_up(I2C_SDA); // Pull up the data line
  gpio_pull_up(I2C_SCL); // Pull up the clock line
  ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
  ssd1306_config(&ssd); // Configura o display
  ssd1306_send_data(&ssd); // Envia os dados para o display

  // Limpa o display. O display inicia com todos os pixels apagados.
  ssd1306_fill(&ssd, false);
  ssd1306_send_data(&ssd);

  // Configura as interrupções  
  gpio_set_irq_enabled_with_callback(5, GPIO_IRQ_EDGE_FALL, 1, &gpio_irq_handler);
  gpio_set_irq_enabled_with_callback(6, GPIO_IRQ_EDGE_FALL, 1, &gpio_irq_handler);

  sequenciaDeCoresAtual = (int*)malloc(tamanhoSequencia * sizeof(int));
  if (sequenciaDeCoresAtual == NULL) {printf("Erro ao alocar memória!\n"); return 1;}

  while(true) {
    drawOnLedMatrix(); // Chama a função para desenhar na matriz de LEDs
    sleep_ms(2000); // Aguarda 1 segundo
  }
}
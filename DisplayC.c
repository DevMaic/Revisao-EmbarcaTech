#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pio_matrix.pio.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "pico/time.h"
#include "inc/ssd1306.h"
#include "inc/font.h"
#include "frames.c"
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C

uint32_t lastTime = 0;
ssd1306_t ssd; // Inicializa a estrutura do display
int adcPositionY = 28, adcPositionX = 60; // Posição inicial do cursor na tela
PIO pio;
uint sm;
int* sequenciaDeCoresAtual;
int tamanhoSequencia = 0;
char c;
int indiceAtual = 0;
struct repeating_timer timerSequencia;
bool isReadingUserInput = false; // Flag para indicar se os valores do joystick estão sendo lido
bool isReadyToReadJoyStick = false; // Flag para indicar se uma leitura individual do joystick foi concluída
bool isTimerRunning = false; // Flag para indicar se o timer está rodando
int* tentativaAtualDoPlayer;
int tamanhoTentativaAtual = 0;

void desenho_pio(double *desenho, PIO pio, uint sm, int cor);
uint32_t matrix_rgb(double b, double r, double g);
void gpioInit();
void i2cInit();
void pioInit();

bool desenharComTimer(struct repeating_timer *t) {

  if (indiceAtual >= tamanhoSequencia) {
    cancel_repeating_timer(t);  // Para o timer depois da última cor

    // Prepara para próxima rodada
    tamanhoSequencia++;
    int *temp = realloc(sequenciaDeCoresAtual, tamanhoSequencia * sizeof(int));
    if (temp == NULL) {
        printf("Erro ao realocar memória!\n");
        free(sequenciaDeCoresAtual);
        return false;
    }
    sequenciaDeCoresAtual = temp;

    // Gera nova cor
    sequenciaDeCoresAtual[tamanhoSequencia - 1] = rand() % 4;

    isReadingUserInput = true; // Ativa a leitura do joystick
    isTimerRunning = false; // Desativa o timer

    int cor = sequenciaDeCoresAtual[indiceAtual];
    desenho_pio(numeros[cor], pio, sm, cor);
    printf("cor: %d\n", cor);

    return false; // Encerra o timer
  }

  int cor = sequenciaDeCoresAtual[indiceAtual];
  desenho_pio(numeros[cor], pio, sm, cor);
  printf("cor: %d\n", cor);

  indiceAtual++;
  return true; // Continua o timer
}

//rotina da interrupção
static void gpio_irq_handler(uint gpio, uint32_t events) {
  uint32_t currentTime = to_us_since_boot(get_absolute_time());

  if(currentTime - lastTime > 300000) { // Debounce

    if (gpio == 5) { // Atualiza o estado do led verde
      add_repeating_timer_ms(500, desenharComTimer, NULL, &timerSequencia);
      indiceAtual = 0; // Reseta o índice atual
    } else if (gpio == 6) { // Atualiza o estado do ler azul
      for(int i = 0; i < tamanhoSequencia; i++) {
        printf("%d ", sequenciaDeCoresAtual[i]);
      }
      printf("\n");
    }

    lastTime = currentTime;
  }

  // Esse if evita que o polling seja aplicado ao scanf, dessa forma uma string pode ser lida sem que o polling interfira
  if(gpio == 0) {
    ssd1306_draw_string(&ssd, "CARACTERE LIDO: ", 8, 40);
    ssd1306_draw_char(&ssd, c, 105, 40);
    ssd1306_send_data(&ssd);
  }
}

void adicionarLeitura(int readValue) {
  // Prepara para próxima rodada
  tamanhoTentativaAtual++;
  int *temp = realloc(tentativaAtualDoPlayer, tamanhoTentativaAtual * sizeof(int));
  if (temp == NULL) {
    printf("Erro ao realocar memória!\n");
    free(tentativaAtualDoPlayer);
  }
  tentativaAtualDoPlayer = temp;
  tentativaAtualDoPlayer[tamanhoTentativaAtual - 1] = readValue;

  if(tentativaAtualDoPlayer[tamanhoTentativaAtual - 1] != sequenciaDeCoresAtual[tamanhoTentativaAtual - 1]) {
    printf("Tentativa errada! Tente novamente.\n");
    isReadingUserInput = false; // Desativa a leitura do joystick
    //resetar jogo, game over
  }

  for(int i = 0; i < tamanhoTentativaAtual; i++) {
    printf("t%d ", tentativaAtualDoPlayer[i]);
  }
  printf("\n");
}

void readUserInput() {
  if(tamanhoSequencia == tamanhoTentativaAtual) {
    isReadingUserInput = false; // Desativa a leitura do joystick
    tamanhoTentativaAtual = 0; // Reseta o tamanho da tentativa atual
    isTimerRunning = false; // Desativa o timer
  }
  // Lê o eixo Y
  adc_select_input(0); // ADC0 (GPIO 26)
  uint16_t adcPositionY = adc_read();

  // Lê o eixo X
  adc_select_input(1); // ADC1 (GPIO 27)
  uint16_t adcPositionX = adc_read();

  // Limiares para considerar como "centro"
  bool noInput = (adcPositionX > 1700 && adcPositionX < 2300) && 
                  (adcPositionY > 1700 && adcPositionY < 2300);

  if (!isReadyToReadJoyStick) {
    // Reativa o joystick apenas se ele estiver em repouso
    if (noInput) {
      isReadyToReadJoyStick = true;
    }
    return;
  }

  if (adcPositionX < 1500 && adcPositionY > 3500) {
    // printf("CIMA ESQUERDA\n");
    isReadyToReadJoyStick = false;
    adicionarLeitura(0);
  }
  else if (adcPositionX > 3500 && adcPositionY > 3500) {
    // printf("CIMA DIREITA\n");
    isReadyToReadJoyStick = false;
    adicionarLeitura(1);
  }
  else if (adcPositionX < 1500 && adcPositionY < 1500) {
    // printf("BAIXO ESQUERDA\n");
    isReadyToReadJoyStick = false;
    adicionarLeitura(3);
  }
  else if (adcPositionX > 3500 && adcPositionY < 1500) {
    // printf("BAIXO DIREITA\n");
    isReadyToReadJoyStick = false;
    adicionarLeitura(2);
  }
}


int main() {
  adc_init(); // Inicializa o ADC
  stdio_init_all(); // Inicializa a comunicação serial
  gpioInit(); // Inicializa os pinos GPIO
  i2cInit(); // Inicializa o I2C
  pioInit(); // Inicializa o PIO

  // Configura as interrupções  
  gpio_set_irq_enabled_with_callback(5, GPIO_IRQ_EDGE_FALL, 1, &gpio_irq_handler);
  gpio_set_irq_enabled_with_callback(6, GPIO_IRQ_EDGE_FALL, 1, &gpio_irq_handler);

  sequenciaDeCoresAtual = (int*)malloc(sizeof(int));
  if (sequenciaDeCoresAtual == NULL) {printf("Erro ao alocar memória!\n"); return 1;}

  tentativaAtualDoPlayer = (int*)malloc(sizeof(int));
  if (tentativaAtualDoPlayer == NULL) {printf("Erro ao alocar memória!\n"); return 1;}

  srand(to_us_since_boot(get_absolute_time())); // Inicializa o gerador de números aleatórios

  sleep_ms(2000);
  while(true) {
    // Lê o eixo Y
    adc_select_input(0); // ADC0 (GPIO 26)
    uint16_t adcPositionY = 55-adc_read()*55/4095;

    // Lê o eixo X
    adc_select_input(1); // ADC1 (GPIO 27)
    uint16_t adcPositionX = adc_read()*119/4095;

    if(adcPositionX < 0)
      adcPositionX = 0;
    if(adcPositionX > 127)
      adcPositionX = 127;
    if(adcPositionY < 0)    
      adcPositionY = 0;
    if(adcPositionY > 63)
      adcPositionY = 63;
    
    // Desenha o cursor na tela
    ssd1306_fill(&ssd, false);
    ssd1306_rect(&ssd, adcPositionY, adcPositionX, 8, 8, true, true);

    // Manda os dados pro display
    ssd1306_send_data(&ssd);

    if(isReadingUserInput) {
      readUserInput(); // Chama a função para ler o caractere do usuário
      printf("r");
    } else {
      if(!isTimerRunning) {
        isTimerRunning = true; // Ativa o timer
        add_repeating_timer_ms(1000, desenharComTimer, NULL, &timerSequencia);
        indiceAtual = 0; // Reseta o índice atual
        printf("d");
      }
    }
  }
}

void pioInit() {
  // Configurando a máquina PIO
  pio = pio0;
  uint offset = pio_add_program(pio, &pio_matrix_program);
  sm = pio_claim_unused_sm(pio, true);
  pio_matrix_program_init(pio, sm, offset, 7);
}

void i2cInit() {
  i2c_init(I2C_PORT, 400 * 1000); // I2C Initialisation. Using it at 400Khz.
  set_sys_clock_khz(128000, false); // Set the system clock to 128Mhz

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
}

void gpioInit() {
  // Configura os pinos ADC
  adc_gpio_init(26);
  adc_gpio_init(27); 
  
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
// DANYELLE MAGALHÃES MACHADO

// INCLUSÃO DE BIBLIOTECAS 
#include <stdio.h>  
#include <string.h>
#include "pico/stdlib.h"  
#include "inc/ssd1306.h"
#include "hardware/adc.h" 
#include "hardware/i2c.h"
#include "hardware/pwm.h"

// DEFINIÇÃO DOS PINOS DO DISPLAY SSD1506
const uint DISPLAY_SDA = 14;
const uint DISPLAY_SCL = 15;

// DEFINIÇÃO DOS PINOS DO SENSOR DE PESO HX711
#define HX711_DATA 17  
#define HX711_CLK  18  

// DEFINIÇÃO DOS PINOS DO JOYSTICK
#define EIXO_X 26  
#define EIXO_Y 27  
#define BTN_SW 22 

// DEFINIÇÃO DOS PINOS DOS OUTROS COMPONENTES
#define BTN_ACAO 6      // BOTÃO DE CONFIRMAÇÃO/CANCELAMENTO
#define BUZZER 5        // BUZZER PARA ALARME SONORO
#define SERVO_1 20      // SERVO MOTOR DO RECIPIENTE DO INGREDIENTE 1
#define SERVO_2 21      // SERVO MOTOR DO RECIPIENTE DO INGREDIENTE 2

// ----- REESCREVER ----- DEFINIÇÃO DOS VALORES DE PWM PARA CÁLCULO DO DUTY CYCLE 
#define PWM_WRAP 24999  // 50HZ
#define PWM_CLK_DIV 100 // VALOR PARA DAR PERÍODO CERTO

// VARIÁVEIS PARA CONTROLE DO MENU DE RECEITAS
bool ativo = false;     // INDICA SE DISPENSER ESTÁ FUNCIONANDO NO MOMENTO
int indice_menu = 0;    // INDICA POSIÇÃO NO MENU DE RECEITAS
bool btn_joystick_estado = false;
bool btn_js_estado_anterior = false;  

// ARRAY COM NOMES DAS RECEITAS
char *receitas[] = {" " ,"Cookies", "Brownie", "Bolo Simples"}; 

// ARRAY COM PESO DOS INGREDIENTES DE CADA RECEITA
const float pesos_receitas[][2] = {
    {0.0, 0.0},          
    {300.0, 150.0},      
    {500.0, 170.0},      
    {400.0, 600.0}       
};




// ------ FUNÇÕES DE CONFIGURAÇÃO
// FUNÇÃO DO DISPLAY
void display_iniciar(){
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);  // INICIALIZA O I2C
    gpio_set_function(DISPLAY_SDA, GPIO_FUNC_I2C);  
    gpio_set_function(DISPLAY_SCL, GPIO_FUNC_I2C);  
    gpio_pull_up(DISPLAY_SDA);  
    gpio_pull_up(DISPLAY_SCL);  
}

// FUNÇÕES DA BALANÇA
// VERIFICA SE O SENSOR ESTÁ PRONTO
bool hx711_is_ready() {
    return gpio_get(HX711_DATA) == 0;
}

// FUÇÃO DE LER O SENSOR DE CARGA
long hx711_read() {
    while (!hx711_is_ready()) {
        sleep_us(10);  // EVITA SOBRECARGA DE CPU
    }

    long value = 0;
    for (int i = 0; i < 24; i++) {
        gpio_put(HX711_CLK, 1);
        sleep_us(1);
        value = (value << 1) | gpio_get(HX711_DATA);
        gpio_put(HX711_CLK, 0);
        sleep_us(1);
    }

    // PULSO EXTRA PARA DEFINIR ESCALA
    gpio_put(HX711_CLK, 1);
    sleep_us(1);
    gpio_put(HX711_CLK, 0);
    sleep_us(1);

    // AJUSTE DE SINAL
    if (value & 0x800000) {
        value |= 0xFF000000;
    }

    return value;
}

// FUNÇÃO PRA CONVERTER VALOR DO SENSOR PARA PESO
float convert_to_weight(long raw_value, float scale_factor, float offset) {
    return (raw_value - offset) / scale_factor;
}


// FUNÇÃO DO JOYSTICK
void joystick_iniciar(){
    adc_gpio_init(EIXO_X);  // INICIALIZA EIXO X
    adc_gpio_init(EIXO_Y);  // INICIALIZA EIXO Y
    gpio_init(BTN_SW);      // INICIALIZA BOTÃO DO JOYSTICK
    gpio_set_dir(BTN_SW, GPIO_IN);  
    gpio_pull_up(BTN_SW);  
}

// FUNÇÃO DO BUZZER
void tone(unsigned int frequency, unsigned int duration) {  
    unsigned long period = 1000000 / frequency, end_time = time_us_64() + (duration * 1000);  
    while (time_us_64() < end_time) {  
        gpio_put(BUZZER, 1);   
        sleep_us(period / 2);  
        gpio_put(BUZZER, 0);   
        sleep_us(period / 2);  
    }  
}


// FUNÇÕES DO SERVO MOTOR - PWM
uint32_t us_to_level(uint32_t us) {
    return (uint32_t)(us * 1.25f); // 1µs ≈ 1.25 unidades
}

// Função para inicializar o PWM
void pwm_iniciar() {
    gpio_set_function(SERVO_1, GPIO_FUNC_PWM);
    gpio_set_function(SERVO_2, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(SERVO_1);
    uint slice_num_dois = pwm_gpio_to_slice_num(SERVO_2);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, PWM_CLK_DIV); // DIVISOR DE CLOCK
    pwm_config_set_wrap(&config, PWM_WRAP);      // WRAP
    pwm_init(slice_num, &config, true);
    pwm_init(slice_num_dois, &config, true);
}


// ------ FUNÇÕES DE FUNCIONAMENTO
void mostrar_display(uint8_t *ssd, struct render_area *frame_area, int y1, int y2, char *text1, char *text2){
    memset(ssd, 0, ssd1306_buffer_length);       // LIMPA DISPLAY
    ssd1306_draw_string(ssd, 15, y1, text1);     // ESCREVE NA PRIMEIRA LINHA
    ssd1306_draw_string(ssd, 15, y2, text2);     // ESCREVE NA SEGUNDA LINHA
    render_on_display(ssd, frame_area);          // RENDERIZA NO DISPLAY
}


int main() {
    stdio_init_all();  

    // BALANÇA
    gpio_init(HX711_DATA);
    gpio_set_dir(HX711_DATA, GPIO_IN);
    gpio_pull_up(HX711_DATA);

    gpio_init(HX711_CLK);
    gpio_set_dir(HX711_CLK, GPIO_OUT);
    gpio_put(HX711_CLK, 0);

    float scale_factor = 2100.0 / 5000.0; // CALIBRADO PARA 5KG (máximo de 2100 como valor bruto)
    float offset = 0;                     // VALOR INICIAL QUANDO BALANÇA ESTÁ NO ZERO

    // JOYSTICK
    adc_init();                           // INICIALIZA O ADC
    joystick_iniciar();                   // INICIALIZA O JOYSTICK

    uint16_t eixo_x_estado_anterior = 2048;       // VALOR INICIAL DO EIXO X
    bool btn_js_estado_anterior = false;  // ULTIMO ESTADO DO SWITCH

    // BOTOES
    gpio_init(BTN_ACAO); 
    gpio_set_dir(BTN_ACAO, GPIO_IN); 
    gpio_pull_up(BTN_ACAO);  

    // SERVO
    uint slice_num = pwm_gpio_to_slice_num(SERVO_1);
    uint slice_num_dois = pwm_gpio_to_slice_num(SERVO_2);
    pwm_iniciar();

    pwm_set_gpio_level(SERVO_1, us_to_level(1470)); // SERVO 1 NA POSIÇÃO DE 90º
    sleep_ms(50);
    pwm_set_gpio_level(SERVO_2, us_to_level(1470)); // SERVO 2 NA POSIÇÃO DE 90º
    sleep_ms(50);

    // DISPLAY
    display_iniciar();
    ssd1306_init();                        

    // ÁREA DE RENDERIZAÇÃO DO DISPLAY
    struct render_area frame_area = {.start_column = 0, .end_column = ssd1306_width - 1, .start_page = 0, .end_page = ssd1306_n_pages - 1};
     
    calculate_render_area_buffer_length(&frame_area);                    // CALCULA BUFFER PARA RENDERIZAÇÃO
    
    uint8_t ssd[ssd1306_buffer_length];                                  // BUFFER DO DISPLAY
    mostrar_display(ssd, &frame_area, 10, 30, "ESCOLHA UMA", "RECEITA"); // MOSTRA TELA INICIAL

    // BUZZER
    gpio_init(BUZZER);  
    gpio_set_dir(BUZZER, GPIO_OUT);  
    gpio_put(BUZZER, 0);  

    
    // BLOCO LOOP
    while (true) {
        // JOYSTICK
        adc_select_input(0);                            // SELECIONA CANAL DO JOYSTICK
        uint16_t eixo_x_valor = adc_read();             // LÊ VALOR DO EIXO DO JOYSTICK
        bool btn_joystick_estado = !gpio_get(BTN_SW);   // VERIFICA ESTADO DO BOTÃO DO JOYSTICK
        bool btn_acao_pressed = !gpio_get(BTN_ACAO);    // VERIFICA ESTADO DO BOTÃO DE AÇÃO

        // BALANÇA
        long raw_value = hx711_read();                  // ARMAZENA VALOR BRUTO DO SENSOR DE CARGA
        float peso_balanca = convert_to_weight(raw_value, scale_factor, offset); // TRANSFORMA BRUTO EM PESO
        char array[16];                                                          // BUFFER PARA CONVERTER FLOAT EM STRING
        sprintf(array, "%.2f g", peso_balanca);

        // SE O DISPENSER AINDA NÃO ESTÁ ATIVO
        if (!ativo) {
            // PERCORRE O MENU COM O JOYSTICK
            if (eixo_x_valor > 1000 && eixo_x_estado_anterior <= 1000) {  
                indice_menu = (indice_menu + 1) % 4; 
                tone(500, 10); 
                mostrar_display(ssd, &frame_area, 30, 0, receitas[indice_menu], "");
            } else if (eixo_x_valor < 3000 && eixo_x_estado_anterior >= 3000) {  
                indice_menu = (indice_menu - 1 + 4) % 4; 
                tone(500, 10); 
                mostrar_display(ssd, &frame_area, 30, 0, receitas[indice_menu], "");
            }

            // SE BOTÃO DO JOYSTICK OU BOTÃO DE AÇÃO FOR PRESSIONADO, SAI DO IF INICIAL
            if ((btn_joystick_estado || btn_acao_pressed) && !btn_js_estado_anterior && indice_menu > 0) { 
                ativo = true;           // INICIA PROCESSO DE PESAGEM
                tone(500, 10);
                mostrar_display(ssd, &frame_area, 10, 30, "RECEITA", "ESCOLHIDA");
                sleep_ms(1000);

                // SE BALANÇA ESTIVER COM PESO
                if (peso_balanca > 0){
                    tone(500, 10);
                    mostrar_display(ssd, &frame_area, 10, 30, "REMOVER", "PESO");
                    sleep_ms(1000);
                    ativo = false;      // VOLTA PARA MENU INICIAL
                    indice_menu = 0;    // VOLTA PARA ÍNDICE 0 
                    mostrar_display(ssd, &frame_area, 10, 30, "ESCOLHA UMA", "RECEITA");
                }
            } 
        } else {
            // DISPENSER ATIVO
            // SE ESTIVER NA RECEITA
            if (indice_menu >= 1 && indice_menu < sizeof(receitas) / sizeof(receitas[0])) {
                const float *pesos = pesos_receitas[indice_menu]; // ACESSA PESOS DA RECEITA SELECIONADA
                float peso_ingrediente_1 = pesos[0];              // ARMAZENA PESO DO PRIMEIRO INGREDIENTE
                float peso_ingrediente_2 = pesos[1];              // ARMAZENA PESO DO SEGUNDO INGREDIENTE
                float peso_receita = peso_ingrediente_1 + peso_ingrediente_2; // PESO TOTOAL DA RECEITA
            
                // LIMPA DISPLAY
                memset(ssd, 0, ssd1306_buffer_length);
            
                // SE O PESO FOR MENOR QUE O DO PRIMEIRO INGREDIENTE, DEIXA SERVO 1 ABERTO
                if (peso_balanca < peso_ingrediente_1) {
                    pwm_set_gpio_level(SERVO_1, us_to_level(985)); 
                    sleep_ms(50);

                    // EXIBE PESO
                    char array[10];
                    sprintf(array, "%.2f g", peso_balanca); 
                    ssd1306_draw_string(ssd, 15, 30, array);

                } 
                // SE O PESO FOR MAIOR OU IGUAL AO PESO DO PRIMEIRO INGREDIENTE E MENOR QUE O PESO TOTAL
                // FECHA SERVO 1 E ABRE SERVO 2
                else if (peso_balanca >= peso_ingrediente_1 && peso_balanca < peso_receita) {
                    pwm_set_gpio_level(SERVO_1, us_to_level(1470));  
                    sleep_ms(50);
                    pwm_set_gpio_level(SERVO_2, us_to_level(985));   
            
                } 
                // SE PESO DA BALANÇA CHEGAR AO PESO DA RECEITA, FECHA O ÚLTIMO SERVO E FINALIZA OPERAÇÃO
                else if (peso_balanca >= peso_receita) {
                    pwm_set_gpio_level(SERVO_2, us_to_level(1470)); 
                    sleep_ms(50);
                    tone(500, 10); 
                    ssd1306_draw_string(ssd, 15, 30, "FIM");
                    render_on_display(ssd, &frame_area);
                    sleep_ms(2000);
            
                    ativo = false;      // VOLTA PARA MENU INICIAL
                    indice_menu = 0;    // VOLTA PARA ÍNDICE 0 
                    mostrar_display(ssd, &frame_area, 10, 30, "ESCOLHA UMA", "RECEITA");
                } 
                
                // SE PESO DA BALANÇA FOR MENOR QUE O PESO DA RECEITA, PERMANECE MOSTRANDO O PESO NO DISPLAY
                if (peso_balanca < peso_receita) {
                    char array[10];
                    sprintf(array, "%.2f g", peso_balanca); 
                    ssd1306_draw_string(ssd, 15, 30, array);
                    
                }
                
                // RENDERIZA O DISPLAY
                render_on_display(ssd, &frame_area);
            } 
            

            // SE BOTÃO DE AÇÃO OU DO JOYSTICK FOR PRESSIONADO, VOLTA AO MENU PRINCIPAL E FECHA SERVOS
            if (btn_acao_pressed && !btn_js_estado_anterior) { 
                ativo = false; 
                indice_menu = 0; 
                tone(500, 10);
                pwm_set_gpio_level(SERVO_1, us_to_level(1470));
                pwm_set_gpio_level(SERVO_2, us_to_level(1470));
                mostrar_display(ssd, &frame_area, 10, 0, "VOLTANDO", " ");
                sleep_ms(1000);
                mostrar_display(ssd, &frame_area, 10, 30, "ESCOLHA UMA", "RECEITA");
            }
            
        }
        
        
        eixo_x_estado_anterior = eixo_x_valor; // ATUALIZA VALOR DO EIXO X
        btn_js_estado_anterior = btn_joystick_estado; // ATUALIZA ESTADO DO SWITCH
        sleep_ms(100); 
    }
    return 0; 
}
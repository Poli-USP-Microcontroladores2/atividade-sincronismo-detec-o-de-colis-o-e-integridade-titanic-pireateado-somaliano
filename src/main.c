/*
 * Echobot com thread periódica simulando interrupção
 * UART0 (USB) ↔ UART1 (PTE0/PTE1)
 * FRDM-KL25Z + Zephyr
 *
 * - Digite algo no Serial Monitor (UART0)
 * - A mensagem é enviada pela UART1
 * - A thread periódica (5s) lê da UART1 e imprime no terminal
 * - LED vermelho acende enquanto a "interrupção" está ativa
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>

#define UART1_NODE DT_NODELABEL(uart1)
#define UART_CONSOLE_NODE DT_CHOSEN(zephyr_console)

#define MSG_SIZE 128
#define READ_PERIOD_MS 5000
#define RX_TIMEOUT_MS 500

/* LED vermelho (PTB18) */
#define LED_RED_NODE DT_ALIAS(led2)
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);

/* Devices */
static const struct device *uart1;
static const struct device *uart0;

/* Buffer de recepção */
static char rx_buf[MSG_SIZE];
static int rx_pos = 0;

/* === Thread periódica de leitura UART1 === */
void uart_reader_thread(void *p1, void *p2, void *p3)
{
    printk("[INT] Thread de recepcao ativada.\n");
gpio_pin_set_dt(&led_red, 1);
k_sleep(K_MSEC(200));
gpio_pin_set_dt(&led_red, 0);

    uint8_t c;
    int64_t last_rx = 0;
    bool receiving = false;

    while (1) {
        /* Aguarda o próximo “disparo” da interrupção */
        k_sleep(K_MSEC(READ_PERIOD_MS));

        /* LED vermelho ON = interrupção ativa */
        gpio_pin_set_dt(&led_red, 1);

        while (uart_poll_in(uart1, &c) == 0) {
            receiving = true;
            last_rx = k_uptime_get();

            if (c == '\r')
                continue;

            if (c == '\n' && rx_pos > 0) {
                rx_buf[rx_pos] = '\0';
                printk("[UART1->USB] %s\n", rx_buf);
                rx_pos = 0;
                receiving = false;
                break; // terminou a leitura dessa rodada
            }

            if (rx_pos < MSG_SIZE - 1) {
                rx_buf[rx_pos++] = c;
            }
        }

        /* Timeout: se demorou demais sem bytes novos */
        if (receiving && (k_uptime_get() - last_rx) > RX_TIMEOUT_MS) {
            if (rx_pos > 0) {
                rx_buf[rx_pos] = '\0';
                printk("[UART1->USB][timeout] %s\n", rx_buf);
                rx_pos = 0;
            }
            receiving = false;
        }

        /* LED vermelho OFF = interrupção terminou */
        gpio_pin_set_dt(&led_red, 0);
    }
}

/* === Função principal === */
void main(void)
{
    uart1 = DEVICE_DT_GET(UART1_NODE);
    uart0 = DEVICE_DT_GET(UART_CONSOLE_NODE);

    if (!device_is_ready(uart1) || !device_is_ready(uart0)) {
        printk("❌ UARTs não estão prontas!\n");
        return;
    }

    /* Configura UART1 @ 230400 baud */
    struct uart_config cfg = {
        .baudrate = 230400,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE
    };
    uart_configure(uart1, &cfg);

    struct uart_config check;
    uart_config_get(uart1, &check);
    printk("✅ UART1 configurada: %d baud\n", check.baudrate);

    /* Configura LED vermelho */
    if (!device_is_ready(led_red.port)) {
        printk("❌ LED vermelho não pronto!\n");
        return;
    }
    gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);

    /* Cria thread periódica de leitura */
    static K_THREAD_STACK_DEFINE(uart_reader_stack, 1024);
    static struct k_thread uart_reader_data;
    k_thread_create(&uart_reader_data, uart_reader_stack, 1024,
                    uart_reader_thread, NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);

    printk("=== Echobot com interrupções a cada 5s ===\n");
    printk("Digite algo no terminal USB (UART0):\n");

    uint8_t c;

    while (1) {
        /* Lê do terminal USB (UART0) e envia pela UART1 */
        if (uart_poll_in(uart0, &c) == 0) {
            uart_poll_out(uart1, c);  // envia caractere digitado
        }

        k_sleep(K_MSEC(10));
    }
}

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <stdbool.h>

#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)
#define PERIOD_MS 5000
#define MSG_SIZE 64
#define MSGQ_MAX_MSGS 10

// Altere o ID para cada placa (1 ou 2)
#define ID_PLACA 1

// LEDs
#define LED_VERDE_NODE DT_ALIAS(led0)
#define LED_VERMELHO_NODE DT_ALIAS(led2)
static const struct gpio_dt_spec led_verde = GPIO_DT_SPEC_GET(LED_VERDE_NODE, gpios);
static const struct gpio_dt_spec led_vermelho = GPIO_DT_SPEC_GET(LED_VERMELHO_NODE, gpios);

// Botão PTB1
static const struct device *gpio_b = DEVICE_DT_GET(DT_NODELABEL(gpiob));
#define BTN_PIN 1

K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, MSGQ_MAX_MSGS, 4);
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);
static char rx_buf[MSG_SIZE];
static int rx_pos = 0;

static volatile bool sync_flag = false;

// UART callback
void uart_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart_dev) || !uart_irq_rx_ready(uart_dev))
		return;

	while (uart_fifo_read(uart_dev, &c, 1) == 1) {
		if ((c == '\n' || c == '\r') && rx_pos > 0) {
			rx_buf[rx_pos] = '\0';
			k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);
			if (strcmp(rx_buf, "SYNC") == 0) {
				sync_flag = true;
			}
			rx_pos = 0;
		} else if (rx_pos < sizeof(rx_buf) - 1) {
			rx_buf[rx_pos++] = c;
		}
	}
}

// UART send helper
void uart_send(const char *buf)
{
	for (int i = 0; buf[i] != '\0'; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}

// Atualiza LEDs
void atualiza_leds(bool envio)
{
	gpio_pin_set_dt(&led_verde, envio);
	gpio_pin_set_dt(&led_vermelho, !envio);
}

// Le botão com debounce
bool le_botao(void)
{
	static bool last_state = true;  // pull-up, não pressionado = 1
	static int64_t last_time = 0;
	bool pressed = false;

	bool state = gpio_pin_get(gpio_b, BTN_PIN);
	int64_t now = k_uptime_get();

	if (!state && last_state && (now - last_time) > 200) { // borda de descida + debounce 200ms
		pressed = true;
		last_time = now;
	}

	last_state = state;
	return pressed;
}

// MAIN
void main(void)
{
	char rx_msg[MSG_SIZE];
	const char *msg_envio = "Arerê o Santos vai jogar a Série B\r\n";

	if (!device_is_ready(uart_dev)) {
		printk("UART not ready\n");
		return;
	}
	if (!device_is_ready(led_verde.port) || !device_is_ready(led_vermelho.port) ||
	    !device_is_ready(gpio_b)) {
		printk("GPIOB ou LEDs não prontos\n");
		return;
	}

	// Configura LEDs
	gpio_pin_configure_dt(&led_verde, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_vermelho, GPIO_OUTPUT_INACTIVE);

	// Configura botão PTB1 como input pull-up
	gpio_pin_configure(gpio_b, BTN_PIN, GPIO_INPUT | GPIO_PULL_UP);

	// Configura UART
	uart_irq_callback_user_data_set(uart_dev, uart_cb, NULL);
	uart_irq_rx_enable(uart_dev);

	// Estado inicial
	int modo_envio = (ID_PLACA == 1);
	atualiza_leds(modo_envio);
	int64_t t0 = k_uptime_get();

	printk("=== UART Sync alternador iniciado (Placa %d) ===\n", ID_PLACA);
	printk("Botão em PTB1 configurado (polling)\n");
	printk("Modo inicial: %s\n", modo_envio ? "Tx (verde)" : "Rx (vermelho)");

	while (1) {
		// Polling do botão
		if (le_botao()) {
			printk("Botão pressionado!\n");
			uart_send("SYNC\n");
			sync_flag = true;
		}

		// Recebeu SYNC
		if (sync_flag) {
			sync_flag = false;
			t0 = k_uptime_get();
			modo_envio = (ID_PLACA == 1);
			atualiza_leds(modo_envio);
			printk("Sincronizado! Voltando ao estado inicial (%s)\n",
			       modo_envio ? "Tx (verde)" : "Rx (vermelho)");
		}

		// Alterna a cada 5s
		if (k_uptime_get() - t0 >= PERIOD_MS) {
			modo_envio = !modo_envio;
			atualiza_leds(modo_envio);
			printk("Alternando para %s\n", modo_envio ? "Tx (verde)" : "Rx (vermelho)");
			t0 = k_uptime_get();
		}

		// Modo Tx / Rx
		if (modo_envio) {
			uart_send(msg_envio);
			k_sleep(K_MSEC(500));
		} else {
			if (k_msgq_get(&uart_msgq, &rx_msg, K_MSEC(100)) == 0) {
				printk("Recebido: %s\n", rx_msg);
			}
		}

		k_sleep(K_MSEC(20)); // pequena pausa para não travar CPU
	}
}
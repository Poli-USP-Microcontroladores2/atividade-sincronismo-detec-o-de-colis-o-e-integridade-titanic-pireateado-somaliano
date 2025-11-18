/*
 * Echo bot adaptado — alterna entre modo de envio e recepção a cada 5 segundos.
 *
 * Envia: "Arerê o Santos vai jogar a Série B"
 * Recebe: imprime mensagens que chegam pela UART.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <string.h>

#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)
#define MSG_SIZE 64
#define MSGQ_MAX_MSGS 10
#define PERIOD_MS 5000  // 5 segundos

K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, MSGQ_MAX_MSGS, 4);

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);
static char rx_buf[MSG_SIZE];
static int rx_buf_pos;

/* Callback de interrupção para recepção de dados */
void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart_dev) || !uart_irq_rx_ready(uart_dev)) {
		return;
	}

	while (uart_fifo_read(uart_dev, &c, 1) == 1) {
		if ((c == '\n' || c == '\r') && rx_buf_pos > 0) {
			rx_buf[rx_buf_pos] = '\0';
			k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);
			rx_buf_pos = 0;
		} else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
			rx_buf[rx_buf_pos++] = c;
		}
	}
}

/* Função auxiliar para enviar texto via UART */
void print_uart(const char *buf)
{
	for (int i = 0; buf[i] != '\0'; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}

/* Thread principal */
int main(void)
{
	char rx_msg[MSG_SIZE];
	const char *msg_envio = "Arerê o Santos vai jogar a Série B\r\n";
	int modo_envio = 1;  // começa enviando

	if (!device_is_ready(uart_dev)) {
		printk("UART device not found!\n");
		return 0;
	}

	int ret = uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
	if (ret < 0) {
		printk("Erro configurando callback UART: %d\n", ret);
		return 0;
	}

	uart_irq_rx_enable(uart_dev);

	print_uart("=== Echo bot async-simples iniciado ===\r\n");

	while (1) {
		if (modo_envio) {
			print_uart("\r\n[Modo Envio - 5s]\r\n");
			int64_t start = k_uptime_get();

			while (k_uptime_get() - start < PERIOD_MS) {
				print_uart((char *)msg_envio);
				k_sleep(K_MSEC(500)); // envia a cada 0,5 s
			}

			modo_envio = 0;
		} else {
			print_uart("\r\n[Modo Recepção - 5s]\r\n");
			int64_t start = k_uptime_get();

			while (k_uptime_get() - start < PERIOD_MS) {
				if (k_msgq_get(&uart_msgq, &rx_msg, K_MSEC(100)) == 0) {
					print_uart("Recebido: ");
					print_uart(rx_msg);
					print_uart("\r\n");
				}
			}

			modo_envio = 1;
		}
	}
	return 0;
}
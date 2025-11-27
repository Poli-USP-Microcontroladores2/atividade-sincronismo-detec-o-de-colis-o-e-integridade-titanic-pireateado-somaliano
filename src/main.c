#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>

#define UART0_NODE DT_NODELABEL(uart0) // Serial monitor do PC
#define UART1_NODE DT_NODELABEL(uart1) // Comunicação entre placas

void main(void)
{
    const struct device *uart0 = DEVICE_DT_GET(UART0_NODE);
    const struct device *uart1 = DEVICE_DT_GET(UART1_NODE);

    if (!device_is_ready(uart0) || !device_is_ready(uart1)) {
        printk("UART0 ou UART1 não estão prontas!\n");
        return;
    }

    printk("=== UART Bridge: UART0 <-> UART1 ===\n");


    while (1) {
    uint8_t c;
        // ----------- Lê da UART0 (serial monitor) e envia para UART1 -----------
        if (uart_poll_in(uart0, &c) == 0) {
            uart_poll_out(uart1, c);
        }

        // ----------- Lê da UART1 (outra placa) e envia imediatamente para UART0 -----------
        if (uart_poll_in(uart1, &c) == 0) {
            uart_poll_out(uart0, c); // retransmite imediatamente para o serial monitor
        }

         // pequeno delay para não travar CPU

        
    }
}

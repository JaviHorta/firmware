#include <xparameters.h>
#include <xgpio_l.h>
#include <lcd_ip.h>
#include <xuartlite_l.h>
#include <mb_interface.h>
#include <xtmrctr_l.h>
#include <xintc_l.h>
//Probando Rama
/*********** Definiciones ***********/

#define TMR_BASE 12500000 // Base de conteo para obtener una frecuencia de 4 Hz
#define DEBOUNCE_DELAY 10000	// Conteo para la demora para eliminar el rebote
#define CHANGE_BUTTON 0x02	
#define OK_BUTTON 0x04
#define delay(counter_delay) for(count = 0; count < counter_delay; count++);	// Macro para realizar una demora por software

/*********** Definiciones de tipo ***********/

typedef enum {IDLE, ALARMA_ACTIVA, CONF_ALRMA, CONF_ZONA_1, CONF_ZONA_2}modo;	// Definicion de tipo modo

/*********** Variables globales ***********/

volatile int count;		// Variable para usar como contador
modo current_mode;	// Modo actual en que se encuentra el sistema
char display_RAM[32];	// RAM de display. Cada posicion se corresponde con un recuadro de la LCD
char sel;		// Variable para seleccionar las opciones de los diferentes menus. El rango de valores es 0 - 3

/*********** Prototipos de funciones ***********/

/*Subrutina de atencion a los botones*/
void buttons_isr();
//void timer_0_isr();

int main()
{
	current_mode = IDLE;
	sel = 0;
	lcd_init_delay();
	/* Configurando el timer */
	XTmrCtr_SetLoadReg(XPAR_XPS_TIMER_0_BASEADDR, 0, TMR_BASE);
	XTmrCtr_LoadTimerCounterReg(XPAR_XPS_TIMER_0_BASEADDR, 0);
	XTmrCtr_SetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR, 0, XTC_CSR_ENABLE_INT_MASK | XTC_CSR_AUTO_RELOAD_MASK | XTC_CSR_DOWN_COUNT_MASK);
	
	/* Configurando interrupciones en INT Controller*/
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_BUTTONS_3BIT_IP2INTC_IRPT_INTR, (XInterruptHandler) buttons_isr, NULL);
	XIntc_EnableIntr(XPAR_INTC_0_BASEADDR, XPAR_BUTTONS_3BIT_IP2INTC_IRPT_MASK);
	
	/* Configurando interrupciones en GPIO y TIMER0*/
	XGpio_WriteReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_GIE_OFFSET, XGPIO_GIE_GINTR_ENABLE_MASK);
	XGpio_WriteReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_IER_OFFSET, 1);
	XTmrCtr_EnableIntr(XPAR_XPS_TIMER_0_BASEADDR, 0);

	/* Habilitando interrupciones */
	XIntc_MasterEnable(XPAR_INTC_0_BASEADDR);
	microblaze_enable_interrupts();

	XTmrCtr_Enable(XPAR_XPS_TIMER_0_BASEADDR, 0);

	while(1);
}

void buttons_isr()
{
	int button_reg;
	delay(DEBOUNCE_DELAY);
	button_reg = XGpio_ReadReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_DATA_OFFSET);
	XGpio_WriteReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_ISR_OFFSET, XGpio_ReadReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_ISR_OFFSET)); // Limpiar bandera de interrupcion
	XGpio_WriteReg(XPAR_LEDS_8BIT_BASEADDR, XGPIO_DATA_OFFSET, 0xFF);
	if(button_reg == 0)		// Si se entro por liberacion de boton, se sale
	{
		return;
	}
	switch (button_reg)
	{
	case CHANGE_BUTTON:
		sel = (sel = 3) ? 0 : (sel + 1); // Si sel = 3 se le asigna 0 si no se le asigna sel + 1 (se incrementa)
		break;
	case OK_BUTTON:
		switch (current_mode)
		{
		case IDLE:
			if(sel == 0)
			{
				current_mode = CONF_ALRMA;
			}
			break;
		case CONF_ALRMA:
			if(sel == 1)
			{
				current_mode = CONF_ZONA_1;
			}
		default:
			break;
		}
		break;
	
	default:
		break;
	}
	return;
}  

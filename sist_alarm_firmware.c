#include <xparameters.h>
#include <xgpio_l.h>
#include <lcd_ip.h>
#include <xuartlite_l.h>
#include <mb_interface.h>
#include <xtmrctr_l.h>
#include <xintc_l.h>
#include <stdbool.h>

/*********** Definiciones ***********/

#define TMR_BASE 12500000 // Base de conteo para obtener una frecuencia de 4 Hz
#define DEBOUNCE_DELAY 10000	// Conteo para la demora para eliminar el rebote
#define CHANGE_BUTTON 0x02	
#define OK_BUTTON 0x04
#define PIN_KEY 1304
#define delay(counter_delay) for(count = 0; count < counter_delay; count++);	// Macro para realizar una demora por software
//Codigo de las diferentes alarmas (definir parametros luego)
#define Codigo_I1 1 
#define Codigo_P1 2 
#define Codigo_I2 3 
#define Codigo_P2 4 
/*********** Definiciones de tipo ***********/

typedef enum {IDLE, ALARMA_ACTIVA, CONF_ALRMA, CONF_ZONA_1, CONF_ZONA_2}modo;	// Definicion de tipo modo
typedef enum {IDLE,Incendio_Z1,Presencia_Z1,Incendio_Z2,Presencia_Z2} alarma;
typedef enum {IDLE, ALARMA_ACTIVA, CONF_ALARMA, CONF_ZONA_1, CONF_ZONA_2, PIN_MODE, ERROR_PIN}modo;	// Definicion de tipo modo
typedef struct
{
	bool hab_zona;			// Para habilitar la activacion de las alarmas de la zona
	bool hab_incendio;		// Habilita la alrma contra incendios
	bool hab_presencia;		// Habilita la alarma de presencia
	bool state_incendio;	// Indica el estado de la alarma contra indendio
	bool state_presencia;	// Indica el estado de la alarma de presencia
}zona;

/*********** Variables globales ***********/

volatile int count;		// Variable para usar como contador
modo current_mode;	// Modo actual en que se encuentra el sistema
char display_RAM[32];	// RAM de display. Cada posicion se corresponde con un recuadro de la LCD
char sel;		// Variable para seleccionar las opciones de los diferentes menus. El rango de valores es 0 - 3
alarma current_alarm; // Estado actual de las alarma

bool hab_global; 	// Habilitacion Global de las alarmas
zona zona_1;
zona zona_2;		
short pin;		// Clave para entrar al modo de configuracion de Alarmas
char num_pin_count;		// Contador para ver el la cantidad de cifras introducidas durante la introduccion del PIN
bool blink;		// Indicador que se utiliza en el parpadeo de los leds

/*********** Prototipos de funciones ***********/

/*Subrutina de atencion a los botones*/
void buttons_isr();
void timer_0_isr();


//Subrutina de atencion al UART Lite
void UART_isr();

int main()
{
	current_mode = IDLE;
	current_alarm= IDLE;
	sel = 0;
	pin = 0;
	blink = false;
	lcd_init_delay();	// Espera necesaria para inicializar la LCD
	/* Configurando el timer */
	XTmrCtr_SetLoadReg(XPAR_XPS_TIMER_0_BASEADDR, 0, TMR_BASE);
	XTmrCtr_LoadTimerCounterReg(XPAR_XPS_TIMER_0_BASEADDR, 0);
	XTmrCtr_SetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR, 0, XTC_CSR_ENABLE_INT_MASK | XTC_CSR_AUTO_RELOAD_MASK | XTC_CSR_DOWN_COUNT_MASK);
	
	/* Configurando interrupciones en INT Controller*/
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_BUTTONS_3BIT_IP2INTC_IRPT_INTR, (XInterruptHandler) buttons_isr, NULL);
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_XPS_TIMER_0_INTERRUPT_INTR, (XInterruptHandler) timer_0_isr, NULL);
	XIntc_EnableIntr(XPAR_INTC_0_BASEADDR, XPAR_BUTTONS_3BIT_IP2INTC_IRPT_MASK);

	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_RS232_DCE_INTERRUPT_INTR, (XInterruptHandler) UART_isr,NULL);
	XIntc_EnableIntr(XPAR_INTC_0_BASEADDR,XPAR_RS232_DCE_INTERRUPT_MASK);

	/* Configurando interrupciones en GPIO -- TIMER0 -- UART*/
	XGpio_WriteReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_GIE_OFFSET, XGPIO_GIE_GINTR_ENABLE_MASK);
	XGpio_WriteReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_IER_OFFSET, 1);
	XTmrCtr_EnableIntr(XPAR_XPS_TIMER_0_BASEADDR, 0);
	XUartLite_EnableIntr(XPAR_RS232_DCE_BASEADDR);

	/* Habilitando interrupciones */
	XIntc_MasterEnable(XPAR_INTC_0_BASEADDR);
	microblaze_enable_interrupts();

	XTmrCtr_Enable(XPAR_XPS_TIMER_0_BASEADDR, 0);

	while(1);
}

void buttons_isr()
{
	int data_buttons, data_switches;

	delay(DEBOUNCE_DELAY);
	data_buttons = XGpio_ReadReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_DATA_OFFSET);
	XGpio_WriteReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_ISR_OFFSET, XGpio_ReadReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_ISR_OFFSET)); // Limpiar bandera de interrupcion
	if(data_buttons == 0)		// Si se entro por liberacion de boton, se sale
	{
		return;
	}
	switch (data_buttons)
	{
	case CHANGE_BUTTON:
		if(current_mode != IDLE)
		{
			sel = (sel = 3) ? 0 : (sel + 1); // Si sel = 3 se le asigna 0 si no se le asigna sel + 1 (se incrementa)
		}
		else
		{
			sel = (sel = 1) ? 0 : (sel + 1); // En el modo IDLE solo hay dos opciones
		}

		break;

	case OK_BUTTON:
		switch (current_mode)
		{
		case IDLE:
			if(sel == 0)
			{
				current_mode = PIN_MODE;
			}
			break;
		
		case CONF_ALARMA:
			switch(sel)
			{
				case 0:
					hab_global = !hab_global;
					break;
				case 1:
					current_mode = CONF_ZONA_1;
					break;
				case 2:
					current_mode = CONF_ZONA_2;
					break;
				case 3:
					sel = 0;
					current_mode = IDLE;
					break;
			}
		
		case CONF_ZONA_1:
			switch(sel)
			{
				case 0:
					zona_1.hab_zona = !zona_1.hab_zona;
					break;
				case 1:
					zona_1.hab_incendio = !zona_1.hab_incendio;
					break;
				case 2:
					zona_1.hab_presencia = !zona_1.hab_presencia;
					break;
				case 3:
					current_mode = CONF_ALARMA;
					break;
			}

		case CONF_ZONA_2:
			switch(sel)
			{
				case 0:
					zona_2.hab_zona = !zona_2.hab_zona;
					break;
				case 1:
					zona_2.hab_incendio = !zona_2.hab_incendio;
					break;
				case 2:
					zona_2.hab_presencia = !zona_2.hab_presencia;
					break;
				case 3:
					current_mode = CONF_ALARMA;
					break;
			}

		case PIN_MODE:
			data_switches = XGpio_ReadReg(XPAR_DIP_SWITCHES_4BIT_BASEADDR, XGPIO_DATA_OFFSET);
			pin = pin * 10 + data_switches;
			num_pin_count++;
			if(num_pin_count == 4)
			{
				num_pin_count = 0;
				pin = 0;
				if (pin = PIN_KEY)
				{
					current_mode = CONF_ALARMA;
				}
				else
				{
					current_mode = ERROR_PIN;
				}
			}
			break;
		
		case ERROR_PIN:
			current_mode = PIN_MODE;
			break;

		case ALARMA_ACTIVA:					// Pendiente de revision
			current_mode = IDLE;
			blink = false;
			break;

		default:
			break;
		}
		break;
	
	default:
		break;
	}
	return;
}  

void UART_isr()
{
	char Alarm_code;
	if (XUartLite_IsReceiveEmpty(XPAR_RS232_DCE_BASEADDR)!=TRUE) //Comprobar si la interrupcion es por recepcion
	{
		Alarm_code=XUartLite_RecvByte(XPAR_RS232_DCE_BASEADDR);
		if(Alarm_code==Codigo_I1||Codigo_I2||Codigo_P1||Codigo_P2){
				current_mode=ALARMA_ACTIVA;
				switch (Alarm_code)
				{
					case Codigo_I1:
						current_alarm=Incendio_Z1;
						break;
					case Codigo_P1:
						current_alarm=Presencia_Z1;
						break;
					case Codigo_I2:
						current_alarm=Incendio_Z2;
						break;
					case Codigo_P2:
						current_alarm=Presencia_Z2;
						break;
					default:
						current_alarm=IDLE;
						break;
				}
		}
		
	}
	
}
void timer_0_isr()
{
	char i;
	XTmrCtr_SetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR, 0, XTmrCtr_GetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR,0));	// Limpiar bandera de solicitud de intr
	for(i = 0; i < 32; i++)	// Refrescamiento de pantalla
	{
		lcd_write_data(display_RAM[i]);
	}
	if (current_mode == ALARMA_ACTIVA)	// Parpadeo de los LEDs cuando se activa el modo Alarma
	{
		if(blink)
		{
			XGpio_WriteReg(XPAR_LEDS_8BIT_BASEADDR, XGPIO_DATA_OFFSET, 0xFF);
		}
		else
		{
			XGpio_WriteReg(XPAR_LEDS_8BIT_BASEADDR, XGPIO_DATA_OFFSET, 0x00);
		}
		blink = !blink;
	}
	
	return;
}

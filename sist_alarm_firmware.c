#include <xparameters.h>
#include <xgpio_l.h>
#include <lcd_ip.h>
#include <xuartlite_l.h>
#include <mb_interface.h>
#include <xtmrctr_l.h>
#include <xintc_l.h>
#include <stdbool.h>

/*********** Definiciones ***********/

#define TMR_BASE 12500000 		// Base de conteo para obtener una frecuencia de 4 Hz
#define DEBOUNCE_DELAY 10000	// Conteo para la demora para eliminar el rebote
#define CHANGE_BUTTON 0x02		// Mascara de opresion del boton de cambio 
#define OK_BUTTON 0x04			// Mascara de opresion del boton de OK
#define PIN_KEY 1304			// Clave para acceder al modo de configuracion de alarma
#define ARROW_CHAR 0x7E			// Codigo ASCII de la flecha
#define MARK_CHAR 0x78			// Codigo ASCII para el simbolo de marcado
#define delay(counter_delay) for(count = 0; count < counter_delay; count++);	// Macro para realizar una demora por software
//Codigo de las diferentes alarmas (definir parametros luego)
#define Codigo_I1 1 
#define Codigo_P1 2 
#define Codigo_I2 3 
#define Codigo_P2 4

/*********** Definiciones de tipo ***********/

typedef enum {Incendio_Z1,Presencia_Z1,Incendio_Z2,Presencia_Z2} alarma;
typedef enum {IDLE, ALARMA_ACTIVA, CONF_ALARMA, CONF_ZONA_1, CONF_ZONA_2, PIN_MODE, ERROR_PIN}modo;	// Definicion de tipo modo
typedef struct
{
	bool hab_zona;			// Para habilitar la activacion de las alarmas de la Zona
	bool hab_incendio;		// Habilita la alrma contra incendios
	bool hab_presencia;		// Habilita la alarma de presencia
	bool state_incendio;	// Indica el estado de la alarma contra indendio
	bool state_presencia;	// Indica el estado de la alarma de presencia
}Zona;

/*********** Variables globales ***********/

volatile int count;		// Variable para usar como contador
modo current_mode;		// Modo actual en que se encuentra el sistema
char display_RAM[32];	// RAM de display. Cada posicion se corresponde con un recuadro de la LCD los

/*Variable para seleccionar las opciones de los diferentes menus. El rango de valores es 0 - 3. En todos 
menus la flecha puede ocupar hasta un maximo de 4 posiciones. Estas posiciones se corresponden con los 
recuadros 0, 8, 16 y 24 de la LCD. Ello se puede homologar con los valores 0, 1, 2 y 3 que toma esta 
variable
*/
char sel;

alarma current_alarm; 	// Estado actual de las alarma

bool hab_global; 	// Habilitacion Global de las alarmas
Zona zona_1;
Zona zona_2;		
short pin;			// Clave para entrar al modo de configuracion de Alarmas
char num_pin_count;	// Contador para ver el la cantidad de cifras introducidas durante la introduccion del PIN
bool blink;			// Indicador que se utiliza en el parpadeo de los leds

/*********** Prototipos de funciones ***********/

/*Subrutina de atencion a los botones*/
void buttons_isr();
/*Subrutinad de atencion al temporizador*/
void timer_0_isr();
/*Funcion para actualizar la RAM de display*/
void update_display_ram();
//Subrutina de atencion al UART Lite
void UART_isr();
/*Funcion para inicializar las estructuras a sus valores por defecto*/
void init_zone(Zona* zone_to_init);

int main()
{
	current_mode = IDLE;
	current_alarm= IDLE;
	sel = 2;
	pin = 0;
	blink = false;
	lcd_init_delay();	// Espera necesaria para inicializar la LCD
	init_zone(&zona_1);
	init_zone(&zona_2);
	update_display_ram();
	/* Configurando el timer */
	XTmrCtr_SetLoadReg(XPAR_XPS_TIMER_0_BASEADDR, 0, TMR_BASE);
	XTmrCtr_LoadTimerCounterReg(XPAR_XPS_TIMER_0_BASEADDR, 0);
	XTmrCtr_SetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR, 0, XTC_CSR_ENABLE_INT_MASK | XTC_CSR_AUTO_RELOAD_MASK | XTC_CSR_DOWN_COUNT_MASK);
	
	/* Configurando interrupciones en INT Controller*/
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_BUTTONS_3BIT_IP2INTC_IRPT_INTR, (XInterruptHandler) buttons_isr, NULL);
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_XPS_TIMER_0_INTERRUPT_INTR, (XInterruptHandler) timer_0_isr, NULL);
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_RS232_DCE_INTERRUPT_INTR, (XInterruptHandler) UART_isr,NULL);
	XIntc_EnableIntr(XPAR_INTC_0_BASEADDR, XPAR_BUTTONS_3BIT_IP2INTC_IRPT_MASK | XPAR_RS232_DCE_INTERRUPT_MASK | XPAR_XPS_TIMER_0_INTERRUPT_MASK);

	/* Configurando interrupciones en GPIO -- TIMER0 -- UART*/
	XGpio_WriteReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_GIE_OFFSET, XGPIO_GIE_GINTR_ENABLE_MASK);
	XGpio_WriteReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_IER_OFFSET, 1);
	XUartLite_EnableIntr(XPAR_RS232_DCE_BASEADDR);

	/* Habilitando interrupciones */
	XIntc_MasterEnable(XPAR_INTC_0_BASEADDR);
	microblaze_enable_interrupts();

	XTmrCtr_Enable(XPAR_XPS_TIMER_0_BASEADDR, 0);	// Disparar temporizador

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
		if(current_mode != ALARMA_ACTIVA && current_mode != PIN_MODE)	// Si no se esta en modos donde aparece el cursor
		{
			if (current_mode != IDLE && current_mode != ERROR_PIN)
				sel = (sel == 3) ? 0 : (sel + 1);	// Si sel = 3 se le asigna 0 si no se le asigna sel + 1 (se incrementa)
			else
				sel = (sel == 3) ? 2 : (sel + 1);	// En el modo IDLE y ERROR_PIN solo hay dos opciones
		}
		break;

	case OK_BUTTON:
		switch (current_mode)
		{
		case IDLE:
			if (sel == 2)
			{
				current_mode = PIN_MODE;
			}
			break;
		
		case CONF_ALARMA:
			switch (sel)
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
					sel = 2;
					current_mode = IDLE;
					break;
			}
			break;
		
		case CONF_ZONA_1:
			switch (sel)
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
			break;

		case CONF_ZONA_2:
			switch (sel)
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
			break;

		case PIN_MODE:
			data_switches = XGpio_ReadReg(XPAR_DIP_SWITCHES_4BIT_BASEADDR, XGPIO_DATA_OFFSET);
			pin = pin * 10 + data_switches;
			num_pin_count++;
			if(num_pin_count == 4)
			{
				num_pin_count = 0;
				if (pin == PIN_KEY)
				{
					current_mode = CONF_ALARMA;
				}
				else
				{
					current_mode = ERROR_PIN;
					sel = 2;	// Cursor a la posocion 2 en el siguente menu 
				}
				pin = 0;
			}
			break;
		
		case ERROR_PIN:
			if (sel == 2) 
			{
				current_mode = PIN_MODE;
			}
			else
			{
				current_mode = IDLE;
			}	
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
	update_display_ram();
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
						//current_alarm=IDLE;
						break;
				}
		}
		
	}
	
}

void timer_0_isr()
{
	char i;
	XTmrCtr_SetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR, 0, XTmrCtr_GetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR,0));	// Limpiar bandera de solicitud de intr
	lcd_SetAddress(0x00);
	for (i = 0; i < 32; i++)	// Refrescamiento de pantalla
	{
		if (i == 16)
			lcd_SetAddress(0x40);
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

void update_display_ram()
{
	char i;

	for (i = 0; i < 32; i++)
	{
		display_RAM[i] = ' ';	// Se limpia toda la RAM de display
	}

	if (current_mode != PIN_MODE && current_mode != ALARMA_ACTIVA)
		display_RAM[sel*8] = ARROW_CHAR;	// Se coloca la flecha segun el selector
	
	switch (current_mode)
	{
	case IDLE:
		display_RAM[17] = 'C';
		display_RAM[18] = 'o';
		display_RAM[19] = 'n';
		display_RAM[20] = 'f';
		display_RAM[21] = 'A';
		display_RAM[22] = 'l';
		display_RAM[23] = 'm';

		display_RAM[25] = 'C';
		display_RAM[26] = 'o';
		display_RAM[27] = 'n';
		display_RAM[28] = 'f';
		display_RAM[29] = 'R';
		display_RAM[30] = 'l';
		display_RAM[31] = 'j';
		break;

	case PIN_MODE:
		display_RAM[1] = 'I';
		display_RAM[2] = 'n';
		display_RAM[3] = 't';
		display_RAM[4] = 'r';
		display_RAM[5] = 'o';
		display_RAM[6] = 'd';
		display_RAM[7] = 'u';
		display_RAM[8] = 'z';
		display_RAM[9] = 'c';
		display_RAM[10] = 'a';

		display_RAM[12] = 'P';
		display_RAM[13] = 'I';
		display_RAM[14] = 'N';
		for (i = 0; i < num_pin_count; i++)
			display_RAM[22 + i] = '*';
		break;

	case ERROR_PIN:
		display_RAM[1] = 'P';
		display_RAM[2] = 'I';
		display_RAM[3] = 'N';

		display_RAM[5] = 'I';
		display_RAM[6] = 'n';
		display_RAM[7] = 'c';
		display_RAM[8] = 'o';
		display_RAM[9] = 'r';
		display_RAM[10] = 'r';
		display_RAM[11] = 'e';
		display_RAM[12] = 'c';
		display_RAM[13] = 't';
		display_RAM[14] = 'o';

		display_RAM[17] = 'R';
		display_RAM[18] = 'e';
		display_RAM[19] = 'p';
		display_RAM[20] = 'e';
		display_RAM[21] = 't';
		display_RAM[22] = 'i';
		display_RAM[23] = 'r';

		display_RAM[25] = 'S';
		display_RAM[26] = 'a';
		display_RAM[27] = 'l';
		display_RAM[28] = 'i';
		display_RAM[29] = 'r';
		break;

	case CONF_ALARMA:
		display_RAM[1] = 'H';
		display_RAM[2] = '.';
		display_RAM[3] = 'G';
		display_RAM[4] = 'l';
		display_RAM[5] = 'o';
		display_RAM[6] = 'b';
		display_RAM[7] = (hab_global == true) ? MARK_CHAR : ' ';

		display_RAM[9] = 'Z';
		display_RAM[10] = 'o';
		display_RAM[11] = 'n';
		display_RAM[12] = 'a';
		display_RAM[14] = '1';

		display_RAM[17] = 'Z';
		display_RAM[18] = 'o';
		display_RAM[19] = 'n';
		display_RAM[20] = 'a';
		display_RAM[22] = '2';

		display_RAM[25] = 'H';
		display_RAM[26] = 'e';
		display_RAM[27] = 'c';
		display_RAM[28] = 'h';
		display_RAM[29] = 'o';
		break;

	case CONF_ZONA_1:
		display_RAM[1] = 'H';
		display_RAM[2] = '.';
		display_RAM[3] = 'Z';
		display_RAM[4] = 'o';
		display_RAM[5] = 'n';
		display_RAM[6] = 'a';
		display_RAM[7] = (zona_1.hab_zona == true) ? MARK_CHAR : ' ';
		
		display_RAM[9] = 'I';
		display_RAM[10] = 'n';
		display_RAM[11] = 'c';
		display_RAM[12] = 'e';
		display_RAM[13] = 'n';
		display_RAM[14] = 'd';
		display_RAM[15] = (zona_1.hab_incendio == true) ? MARK_CHAR : ' ';

		display_RAM[17] = 'P';
		display_RAM[18] = 'r';
		display_RAM[19] = 'e';
		display_RAM[20] = 's';
		display_RAM[21] = 'e';
		display_RAM[22] = 'n';
		display_RAM[23] = (zona_1.hab_presencia == true) ? MARK_CHAR : ' ';

		display_RAM[25] = 'H';
		display_RAM[26] = 'e';
		display_RAM[27] = 'c';
		display_RAM[28] = 'h';
		display_RAM[29] = 'o';
		break;

	case CONF_ZONA_2:
		display_RAM[1] = 'H';
		display_RAM[2] = '.';
		display_RAM[3] = 'Z';
		display_RAM[4] = 'o';
		display_RAM[5] = 'n';
		display_RAM[6] = 'a';
		display_RAM[7] = (zona_2.hab_zona == true) ? MARK_CHAR : ' ';
		
		display_RAM[9] = 'I';
		display_RAM[10] = 'n';
		display_RAM[11] = 'c';
		display_RAM[12] = 'e';
		display_RAM[13] = 'n';
		display_RAM[14] = 'd';
		display_RAM[15] = (zona_2.hab_incendio == true) ? MARK_CHAR : ' ';

		display_RAM[17] = 'P';
		display_RAM[18] = 'r';
		display_RAM[19] = 'e';
		display_RAM[20] = 's';
		display_RAM[21] = 'e';
		display_RAM[22] = 'n';
		display_RAM[23] = (zona_2.hab_presencia == true) ? MARK_CHAR : ' ';

		display_RAM[25] = 'H';
		display_RAM[26] = 'e';
		display_RAM[27] = 'c';
		display_RAM[28] = 'h';
		display_RAM[29] = 'o';
		break;

	case ALARMA_ACTIVA:
		display_RAM[3] = 'A';
		display_RAM[4] = 'l';
		display_RAM[5] = 'a';
		display_RAM[6] = 'r';
		display_RAM[7] = 'm';
		display_RAM[8] = 'a';
		display_RAM[9] = '!';
		display_RAM[10] = '!';
		display_RAM[11] = '!';

		if (zona_1.state_incendio == true)
		{
			display_RAM[18] = 'I';
			display_RAM[19] = '1';
		}
		
		if (zona_1.state_presencia == true)
		{
			display_RAM[21] = 'P';
			display_RAM[22] = '1';
		}

		if (zona_2.state_incendio == true)
		{
			display_RAM[25] = 'I';
			display_RAM[26] = '2';
		}
		
		if (zona_2.state_presencia == true)
		{
			display_RAM[28] = 'P';
			display_RAM[29] = '2';
		}
		break;

	default:
		break;
	}
	return;
}

void init_zone(Zona* zone_to_init)
{
	zone_to_init->hab_zona = true;
	zone_to_init->hab_incendio = true;
	zone_to_init->hab_presencia = true;
	zone_to_init->state_incendio = false;
	zone_to_init->state_presencia = false;
	return;
}

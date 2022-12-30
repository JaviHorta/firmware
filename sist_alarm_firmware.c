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
#define DEBOUNCE_DELAY 12000	// Conteo para la demora para eliminar el rebote
#define CHANGE_BUTTON 0x02		// Mascara de opresion del boton de cambio 
#define OK_BUTTON 0x04			// Mascara de opresion del boton de OK
#define DEFAULT_PIN_KEY 1304	// Clave para acceder al modo de configuracion de alarma
#define PUK_CODE 13042999		// Clave para desbloquear sistema
#define ARROW_CHAR 0x7E			// Codigo ASCII de la flecha
#define MARK_CHAR 0x00			// Codigo ASCII para el simbolo de marcado
#define delay(counter_delay) for(count = 0; count < counter_delay; count++);	// Macro para realizar una demora por software
//Codigo de las diferentes alarmas (definir parametros luego)
#define Codigo_I1 1 
#define Codigo_P1 2 
#define Codigo_I2 3 
#define Codigo_P2 4

/*********** Definiciones de tipo ***********/

typedef enum {IDLE, ALARMA_ACTIVA, CONF_ALARMA, CONF_ZONA_1, CONF_ZONA_2, PIN_MODE, 
			  WRONG_PIN, CONF_PIN, CONF_PIN_SUCCESSFULLY, CONF_RELOJ, PUK_MODE, WRONG_PUK} modo;	// Definicion de tipo modo
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

bool hab_global; 			// Habilitacion Global de las alarmas
Zona zona_1;
Zona zona_2;		

int horas,minutos,segundos; //Variables que configuran el reloj
volatile char Registro[1000]; //Espacio en memoria para almacenar el registro de las alarmas
int Cont_alarmas=0; //Contador que cuenta la cantidad de alarmas ocurridas
int num_in;					// Clave introducida por el usuario para entrar al modo de configuracion de Alarmas o de cambio de PIN
short current_pin;			// Clave establecida para entrar al modo de configuracion de Alarmas o de cambio de PIN
char num_in_count;			// Contador para ver el la cantidad de cifras introducidas durante la introduccion del PIN
bool blink;					// Indicador que se utiliza en el parpadeo de los LEDs
bool blinking_on;			// Se usa para activar el parpadeo de los LEDs
bool is_conf_alarm_chosen;	// Indica si la opcion elegida fue configurar alarma. Se usa para distinguir entre CONF_ALARMA y CONF_PIN al momento de introducir el PIN
char posescalador;			// Posescalador para el conteo de 1 segundo
char wrong_pin_count;		// Se usa para contar la cantidad de intentos fallidos al introducir el PIN


/*********** Prototipos de funciones ***********/

/*Subrutina de atencion a los botones*/
void buttons_isr();
/*Subrutinad de atencion al temporizador*/
void timer_0_isr();
/*Funcion para actualizar la RAM de display*/
void update_display_ram();
//Subrutina de atencion al UART Lite
void UART_MDM_isr();
/*Funcion para inicializar las estructuras a sus valores por defecto*/
void init_zone(Zona* zone_to_init);
/*Funcion de ajuste del reloj*/
//void ajuste_reloj(int data_buttons);

int main()
{
	current_mode = IDLE;
	current_pin = DEFAULT_PIN_KEY;
	sel = 2;
	num_in = 0;
	blink = false;
	blinking_on = false;
	hab_global = true;
	wrong_pin_count = 0;
	lcd_init_delay();	// Espera necesaria para inicializar la LCD
	init_zone(&zona_1);
	init_zone(&zona_2);
//====== Creando caracter personalizado MARK_CHAR ======
	lcd_write_cmd(0x40);	// Comando Set CG RAM Address para establecer la direccion 0x00 de la CG RAM
	lcd_write_data(0x00);
	lcd_write_data(0x00);
	lcd_write_data(0x01);
	lcd_write_data(0x02);
	lcd_write_data(0x14);
	lcd_write_data(0x08);
	lcd_write_data(0x00);
	lcd_write_data(0x00);
//======================================================
	update_display_ram();
	microblaze_enable_icache();
	horas=0;
	minutos=0;
	segundos=0;
	/* Configurando el timer */
	XTmrCtr_SetLoadReg(XPAR_XPS_TIMER_0_BASEADDR, 0, TMR_BASE);
	XTmrCtr_LoadTimerCounterReg(XPAR_XPS_TIMER_0_BASEADDR, 0);
	XTmrCtr_SetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR, 0, XTC_CSR_ENABLE_INT_MASK | XTC_CSR_AUTO_RELOAD_MASK | XTC_CSR_DOWN_COUNT_MASK);
	
	/* Configurando interrupciones en INT Controller*/
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_BUTTONS_3BIT_IP2INTC_IRPT_INTR, (XInterruptHandler) buttons_isr, NULL);
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_XPS_TIMER_0_INTERRUPT_INTR, (XInterruptHandler) timer_0_isr, NULL);
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, /*Anadir mascara de INT*/, (XInterruptHandler) UART_MDM_isr,NULL);
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
	typedef enum {hora,minuto,segundo,done} parametros; //Parametros necesarios para configurar el reloj
	parametros current_option=hora; //Variable que indica en la opcion en la que se encuentra

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
		if(current_mode != ALARMA_ACTIVA && current_mode != PIN_MODE && current_mode != PUK_MODE && current_mode != WRONG_PIN && current_mode != WRONG_PUK && current_mode != CONF_PIN_SUCCESSFULLY)	// Si no se esta en modos donde aparece el cursor
		{
			if (current_mode != IDLE && current_mode != WRONG_PIN)
				sel = (sel == 3) ? 0 : (sel + 1);	// Si sel = 3 se le asigna 0 si no se le asigna sel + 1 (se incrementa)
			else
				sel = (sel == 3) ? 1 : (sel + 1);	// En el modo IDLE y WRONG_PIN solo hay tres opciones
		}
		break;

	case OK_BUTTON:
		switch (current_mode)
		{
		case IDLE:
			switch (sel)
			{
			case 1:		// Opcion de configurar PIN
				current_mode = PIN_MODE;
				is_conf_alarm_chosen = false;
				break;
			case 2:		// Opcion de configurar alarmas
				current_mode = PIN_MODE;
				is_conf_alarm_chosen = true;
				sel = 0;
				break;
			case 3:		// Opcion de configurar reloj
				current_mode = CONF_RELOJ;
				sel = 0;
				break;
			default:
				break;
			}
			break;
		
		case CONF_ALARMA:
			switch (sel)
			{
				case 0:
					current_mode = CONF_ZONA_1;
					sel = 0;
					break;
				case 1:
					current_mode = CONF_ZONA_2;
					sel = 0;
					break;
				case 2:
					hab_global = !hab_global;
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
					zona_1.hab_presencia = !zona_1.hab_presencia;
					break;
				case 1:
					zona_1.hab_incendio = !zona_1.hab_incendio;
					break;
				case 2:
					zona_1.hab_zona = !zona_1.hab_zona;
					break;
				case 3:
					current_mode = CONF_ALARMA;
					sel = 0;
					break;
			}
			break;

		case CONF_ZONA_2:
			switch (sel)
			{
				case 0:
					zona_2.hab_presencia = !zona_2.hab_presencia;
					break;
				case 1:
					zona_2.hab_incendio = !zona_2.hab_incendio;
					break;
				case 2:
					zona_2.hab_zona = !zona_2.hab_zona;
					break;
				case 3:
					current_mode = CONF_ALARMA;
					sel = 0;
					break;
			}
			break;

		case PIN_MODE:
			data_switches = XGpio_ReadReg(XPAR_DIP_SWITCHES_4BIT_BASEADDR, XGPIO_DATA_OFFSET);
			num_in = num_in * 10 + data_switches;
			num_in_count++;
			if(num_in_count == 4)
			{
				num_in_count = 0;
				if (num_in == current_pin)
				{
					current_mode = (is_conf_alarm_chosen == true) ? CONF_ALARMA : CONF_PIN;
					wrong_pin_count = 0;
				}
				else
				{
					wrong_pin_count++;
					if (wrong_pin_count > 2)
					{
						current_mode = PUK_MODE;
						blinking_on = true;	
					}
					else
						current_mode = WRONG_PIN;
					sel = 2;	// Cursor a la posocion 2 en el siguente menu 
				}
				num_in = 0;
			}
			break;
		
		case WRONG_PIN:
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
			sel = 2;
			blink = false;
			blinking_on = false;
			break;

		case CONF_RELOJ:
			switch (sel)
			{
				case hora:
					horas++;
					if(horas>23)
						horas=0;
					break;
				case minuto:
					minutos++;
					if(minutos>59)
						minutos=0;
			 		break;
				case segundo:
					segundos=0;
					break;
				case done:        //Condicion para salir de la configuracion del reloj 
					current_mode=IDLE;
					sel = 2;
					break;							
			  default:
				  break;
			}
      		break;

		case CONF_PIN:
			data_switches = XGpio_ReadReg(XPAR_DIP_SWITCHES_4BIT_BASEADDR, XGPIO_DATA_OFFSET);
			num_in = num_in * 10 + data_switches;
			num_in_count++;
			if(num_in_count == 4)
			{
				num_in_count = 0;
				current_pin = num_in;
				current_mode = CONF_PIN_SUCCESSFULLY;
				num_in = 0;
				sel = 2;	// Cursor a la posicion 2 en el siguiente menú
			}
			break;
		
		case CONF_PIN_SUCCESSFULLY:
			current_mode = IDLE;
			sel = 2;
			break;

		case PUK_MODE:
			data_switches = XGpio_ReadReg(XPAR_DIP_SWITCHES_4BIT_BASEADDR, XGPIO_DATA_OFFSET);
			num_in = num_in * 10 + data_switches;
			num_in_count++;
			if(num_in_count == 8)
			{
				num_in_count = 0;
				if (num_in == PUK_CODE)
				{
					current_mode = IDLE;
					sel = 2;
					blinking_on = false;
					wrong_pin_count = 0;
				}
				else
					current_mode = WRONG_PUK;
				num_in = 0;
				sel = 2;	// Cursor a la posicion 2 en el siguiente menú
			}
			break;

		case WRONG_PUK:
			current_mode = PUK_MODE;
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

void UART_MDM_isr()
{
	if (XUartLite_IsReceiveEmpty(XPAR_UARTLITE_0_BASEADDR)!=TRUE) //Comprobar si la interrupcion es por recepcion
	{
		/*Almacenamiento e identificacion del codigo de la alarma*/
		Registro[Cont_alarmas] = XUartLite_RecvByte(XPAR_UARTLITE_0_BASEADDR);
		if(Registro[Cont_alarmas] == Codigo_I1 || Registro[Cont_alarmas] == Codigo_I2 || Registro[Cont_alarmas] == Codigo_P1 || Registro[Cont_alarmas] == Codigo_P2)
		{
				current_mode=ALARMA_ACTIVA;
				blinking_on = true;
				switch (Registro[Cont_alarmas])
				{
					case Codigo_I1:
						zona_1.state_incendio=TRUE;
						break;
					case Codigo_P1:
						zona_1.state_presencia=TRUE;
						break;
					case Codigo_I2:
						zona_2.state_incendio=TRUE;
						break;
					case Codigo_P2:
						zona_2.state_presencia=TRUE;
						break;
					default:
						break;
				}
				/*Transmision del codigo de la alarma*/      
				XUartLite_SendByte(XPAR_UARTLITE_0_BASEADDR,Registro[Cont_alarmas]);
				Cont_alarmas++;
		}
		else 
			Registro[Cont_alarmas]=0;

		
	}
	return;
}

void timer_0_isr()
{
	char i;

	XTmrCtr_SetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR, 0, XTmrCtr_GetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR,0));	// Limpiar bandera de solicitud de intr
	posescalador++;
	if (posescalador == 4)
	{
		posescalador = 0;
		if (segundos == 59)
		{
			if (minutos == 59)
			{
				horas = (horas == 23) ? 0 : horas + 1;
				minutos = 0;
			}
			else
				minutos++;
			segundos = 0;
		}
		else
			segundos++;
	}
	if (current_mode == IDLE || current_mode == CONF_RELOJ)
	{
		display_RAM[0] = horas / 10 + 0x30;
		display_RAM[1] = horas % 10 + 0x30;
		display_RAM[2] = ':';
		display_RAM[3] = minutos / 10 + 0x30;
		display_RAM[4] = minutos % 10 + 0x30;
		display_RAM[5] = ':';
		display_RAM[6] = segundos / 10 + 0x30;
		display_RAM[7] = segundos % 10 + 0x30;
		if (current_mode == CONF_RELOJ)
		{
			if (blink == true)
			{
				switch (sel)
				{
				case 0:
					display_RAM[0] = ' ';
					display_RAM[1] = ' ';
					break;
				case 1:
					display_RAM[3] = ' ';
					display_RAM[4] = ' ';
					break;
				case 2:
					display_RAM[6] = ' ';
					display_RAM[7] = ' ';
					break;
				default:
					break;
				}
			}
			
		}
		
	}
	lcd_SetAddress(0x00);
	for (i = 0; i < 32; i++)	// Refrescamiento de pantalla
	{
		if (i == 16)
			lcd_SetAddress(0x40);
		lcd_write_data(display_RAM[i]);
	}
	if (blinking_on)	// Parpadeo de los LEDs
	{
		if(blink)
		{
			XGpio_WriteReg(XPAR_LEDS_8BIT_BASEADDR, XGPIO_DATA_OFFSET, 0xFF);
		}
		else
		{
			XGpio_WriteReg(XPAR_LEDS_8BIT_BASEADDR, XGPIO_DATA_OFFSET, 0x00);
		}
	}
	else
		XGpio_WriteReg(XPAR_LEDS_8BIT_BASEADDR, XGPIO_DATA_OFFSET, 0x00);
	blink = !blink;
	return;
}

void update_display_ram()
{
//	typedef enum {hora,minuto,segundo,done} parametros; //Parametros necesarios para colocar la flecha
	char i;

	for (i = 0; i < 32; i++)
	{
		display_RAM[i] = ' ';	// Se limpia toda la RAM de display
	}

	if (current_mode != PIN_MODE && current_mode != ALARMA_ACTIVA && current_mode != CONF_RELOJ && current_mode != PUK_MODE)
		display_RAM[sel*8] = ARROW_CHAR;	// Se coloca la flecha segun el selector
	
	switch (current_mode)
	{
	case IDLE:
		display_RAM[9] = 'C';
		display_RAM[10] = 'o';
		display_RAM[11] = 'n';
		display_RAM[12] = 'f';
		display_RAM[13] = 'P';
		display_RAM[14] = 'I';
		display_RAM[15] = 'N';

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
		display_RAM[3] = 'E';
		display_RAM[4] = 'n';
		display_RAM[5] = 't';
		display_RAM[6] = 'e';
		display_RAM[7] = 'r';
		display_RAM[9] = 'P';
		display_RAM[10] = 'I';
		display_RAM[11] = 'N';
		for (i = 0; i < num_in_count; i++)
			display_RAM[22 + i] = '*';
		break;

	case WRONG_PIN:
		display_RAM[0] = 'P';
		display_RAM[1] = 'I';
		display_RAM[2] = 'N';

		display_RAM[4] = 'W';
		display_RAM[5] = 'r';
		display_RAM[6] = 'o';
		display_RAM[7] = 'n';
		display_RAM[8] = 'g';
		
		display_RAM[10] = (3 - wrong_pin_count) + 0x30;

		display_RAM[12] = 'a';
		display_RAM[13] = 't';
		display_RAM[14] = 't';
		display_RAM[15] = '.';

		display_RAM[17] = 'A';
		display_RAM[18] = 'g';
		display_RAM[19] = 'a';
		display_RAM[20] = 'i';
		display_RAM[21] = 'n';

		display_RAM[25] = 'C';
		display_RAM[26] = 'a';
		display_RAM[27] = 'n';
		display_RAM[28] = 'c';
		display_RAM[29] = 'e';
		display_RAM[30] = 'l';
		break;

	case CONF_ALARMA:
		display_RAM[1] = 'Z';
		display_RAM[2] = 'o';
		display_RAM[3] = 'n';
		display_RAM[4] = 'e';
		display_RAM[6] = '1';

		display_RAM[9] = 'Z';
		display_RAM[10] = 'o';
		display_RAM[11] = 'n';
		display_RAM[12] = 'e';
		display_RAM[14] = '2';

		display_RAM[17] = 'E';
		display_RAM[18] = 'n';
		display_RAM[19] = 'G';
		display_RAM[20] = 'l';
		display_RAM[21] = 'o';
		display_RAM[22] = 'b';
		display_RAM[23] = (hab_global == true) ? MARK_CHAR : ' ';

		display_RAM[25] = 'O';
		display_RAM[26] = 'K';
		break;

	case CONF_ZONA_1:
		display_RAM[17] = 'E';
		display_RAM[18] = 'n';
		display_RAM[19] = 'a';
		display_RAM[20] = 'b';
		display_RAM[21] = 'l';
		display_RAM[22] = 'e';
		display_RAM[23] = (zona_1.hab_zona == true) ? MARK_CHAR : ' ';
		
		display_RAM[9] = 'F';
		display_RAM[10] = 'i';
		display_RAM[11] = 'r';
		display_RAM[12] = 'e';
		display_RAM[13] = (zona_1.hab_incendio == true) ? MARK_CHAR : ' ';

		display_RAM[1] = 'P';
		display_RAM[2] = 'r';
		display_RAM[3] = 'e';
		display_RAM[4] = 's';
		display_RAM[5] = 'e';
		display_RAM[6] = 'n';
		display_RAM[7] = (zona_1.hab_presencia == true) ? MARK_CHAR : ' ';

		display_RAM[25] = 'O';
		display_RAM[26] = 'K';
		break;

	case CONF_ZONA_2:
		display_RAM[17] = 'E';
		display_RAM[18] = 'n';
		display_RAM[19] = 'a';
		display_RAM[20] = 'b';
		display_RAM[21] = 'l';
		display_RAM[22] = 'e';
		display_RAM[23] = (zona_2.hab_zona == true) ? MARK_CHAR : ' ';
		
		display_RAM[9] = 'F';
		display_RAM[10] = 'i';
		display_RAM[11] = 'r';
		display_RAM[12] = 'e';
		display_RAM[13] = (zona_2.hab_incendio == true) ? MARK_CHAR : ' ';

		display_RAM[1] = 'P';
		display_RAM[2] = 'r';
		display_RAM[3] = 'e';
		display_RAM[4] = 's';
		display_RAM[5] = 'e';
		display_RAM[6] = 'n';
		display_RAM[7] = (zona_2.hab_presencia == true) ? MARK_CHAR : ' ';

		display_RAM[25] = 'O';
		display_RAM[26] = 'K';
		break;

	case ALARMA_ACTIVA:
		display_RAM[3] = 'A';
		display_RAM[4] = 'l';
		display_RAM[5] = 'a';
		display_RAM[6] = 'r';
		display_RAM[7] = 'm';
		display_RAM[8] = '!';
		display_RAM[9] = '!';
		display_RAM[10] = '!';

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

	case CONF_RELOJ:
		display_RAM[16] = (sel == 3) ? ARROW_CHAR : ' ';
		display_RAM[17] = 'O';
		display_RAM[18] = 'K';
    	break;

	case CONF_PIN:
		display_RAM[0] = 'E';
		display_RAM[1] = 'n';
		display_RAM[2] = 't';
		display_RAM[3] = 'e';
		display_RAM[4] = 'r';

		display_RAM[6] = 'n';
		display_RAM[7] = 'e';
		display_RAM[8] = 'w';
		
		display_RAM[10] = 'P';
		display_RAM[11] = 'I';
		display_RAM[12] = 'N';
		for (i = 0; i < num_in_count; i++)
			display_RAM[22 + i] = '*';
		break;

	case CONF_PIN_SUCCESSFULLY:
		display_RAM[0] = 'P';
		display_RAM[1] = 'I';
		display_RAM[2] = 'N';
		
		display_RAM[4] = 'w';
		display_RAM[5] = 'a';
		display_RAM[6] = 's';

		display_RAM[8] = 'c';
		display_RAM[9] = 'h';
		display_RAM[10] = 'a';
		display_RAM[11] = 'n';
		display_RAM[12] = 'g';
		display_RAM[13] = 'e';
		display_RAM[14] = 'd';

		display_RAM[17] = 'O';
		display_RAM[18] = 'K';
		break;

	case PUK_MODE:
		display_RAM[0] = 'B';		
		display_RAM[1] = 'l';		
		display_RAM[2] = 'o';		
		display_RAM[3] = 'c';		
		display_RAM[4] = 'k';		
		display_RAM[5] = 'e';		
		display_RAM[6] = 'd';		
		display_RAM[7] = ',';		
		display_RAM[8] = 'e';		
		display_RAM[9] = 'n';		
		display_RAM[10] = 't';		
		display_RAM[11] = '.';		
		display_RAM[13] = 'P';		
		display_RAM[14] = 'U';		
		display_RAM[15] = 'K';
		for (i = 0; i < num_in_count; i++)
			display_RAM[20 + i] = '*';
		break;

	case WRONG_PUK:
		display_RAM[2] = 'W';		
		display_RAM[3] = 'r';		
		display_RAM[4] = 'o';		
		display_RAM[5] = 'n';		
		display_RAM[6] = 'g';		
		display_RAM[8] = 'P';		
		display_RAM[9] = 'U';		
		display_RAM[10] = 'K';		
		display_RAM[11] = '!';		
		display_RAM[12] = '!';		
		display_RAM[13] = '!';
		display_RAM[17] = 'O';
		display_RAM[18] = 'K';
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


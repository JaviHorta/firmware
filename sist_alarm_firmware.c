#include <xparameters.h>
#include <xgpio_l.h>
#include <lcd_ip.h>
#include <xuartlite_l.h>
#include <mb_interface.h>
#include <xtmrctr_l.h>
#include <xintc_l.h>
#include <xps2_l.h>
#include <stdbool.h>

/*********** Definiciones ***********/

#define TMR_BASE 12500000	 	// Base de conteo para obtener una frecuencia de 4 Hz
#define DEBOUNCE_DELAY 12000 	// Conteo para la demora para eliminar el rebote
#define CHANGE_BUTTON 0x02	 	// Mascara de opresion del boton de cambio
#define OK_BUTTON 0x04		 	// Mascara de opresion del boton de OK
#define DEFAULT_PIN_KEY 1304 	// Clave para acceder al modo de configuracion de alarma
#define PUK_CODE 13042999	 	// Clave para desbloquear sistema
#define ARROW_CHAR 0x7E		 	// Codigo ASCII de la flecha
#define MARK_CHAR 0x00		 	// Codigo ASCII para el simbolo de marcado
#define HISTORY_SIZE 100	 	// Tamaño del Historial de Alarmas
#define DELETE_KEY 0x71		 	// Scan code de la tecla Delete
#define MOVE_L_CMD 0x10			// Comando para mover el cursor de la LCD una posicion a la izquierda
#define delay(counter_delay)                        \
	for (count = 0; count < counter_delay; count++) \
		; // Macro para realizar una demora por software
// Codigo de las diferentes alarmas (definir parametros luego)
#define Codigo_I1 0x31
#define Codigo_P1 0x32
#define Codigo_I2 0x33
#define Codigo_P2 0x34

/*********** Definiciones de tipo ***********/

typedef enum
{
	IDLE,
	ALARMA_ACTIVA,
	CONF_ALARMA,
	CONF_ZONA_1,
	CONF_ZONA_2,
	PIN_MODE,
	WRONG_PIN,
	CONF_PIN,
	CONF_PIN_SUCCESSFULLY,
	CONF_RELOJ,
	PUK_MODE,
	WRONG_PUK,
	ALARM_HISTORY,
	ZONAS_ALARMAS_EN
} modo; // Definicion de tipo modo
typedef enum
{
	Fire,
	Presence
} type_alarm;
typedef enum
{
	Menu_Alarma,
	Menu_Reloj
} menu;
typedef struct
{
	bool hab_zona;		  // Para habilitar la activacion de las alarmas de la Zona
	bool hab_incendio;	  // Habilita la alrma contra incendios
	bool hab_presencia;	  // Habilita la alarma de presencia
	bool state_incendio;  // Indica el estado de la alarma contra indendio
	bool state_presencia; // Indica el estado de la alarma de presencia
} Zona;
typedef struct
{
	type_alarm alarm;
	u8 zone;
	u8 hour;
	u8 min;
	u8 day;
	u8 month;
	u8 year;
} History_Entry;

/*********** Variables globales ***********/

volatile int count;	  // Variable para usar como contador
modo current_mode;	  // Modo actual en que se encuentra el sistema
menu current_menu;	  // Menu actual en el que se encuentra el sistema
char display_RAM[32]; // RAM de display. Cada posicion se corresponde con un recuadro de la LCD los

/*Variable para seleccionar las opciones de los diferentes menus. El rango de valores es 0 - 3. En todos
menus la flecha puede ocupar hasta un maximo de 4 posiciones. Estas posiciones se corresponden con los
recuadros 0, 8, 16 y 24 de la LCD. Ello se puede homologar con los valores 0, 1, 2 y 3 que toma esta
variable
*/
u8 sel;
u8 clock_sel;
bool hab_global; // Habilitacion Global de las alarmas
Zona zona_1;
Zona zona_2;
History_Entry history[HISTORY_SIZE];
u8 horas, minutos, segundos; // Variables que configuran el reloj
u8 day, month, year;		   // Variables que establecen la fecha
// volatile char Registro[1000]; //Espacio en memoria para almacenar el registro de las alarmas
//int Cont_alarmas = 0;			  // Contador que cuenta la cantidad de alarmas ocurridas
u32 num_in;						  // Numero introducido por el usuario en los modos donde se solicita introducir el PIN o el PUK
u16 current_pin;				  // Clave establecida para entrar al modo de configuracion de Alarmas o de cambio de PIN
u8 num_in_count;				  // Contador para ver el la cantidad de cifras introducidas por el usuario en los modos donde se solicita introducir el PIN o el PUK
bool blink;						  // Indicador que se utiliza en el parpadeo de los LEDs
bool blinking_on;				  // Se usa para activar el parpadeo de los LEDs
bool is_conf_alarm_chosen;		  // Indica si la opcion elegida fue configurar alarma. Se usa para distinguir entre CONF_ALARMA y CONF_PIN al momento de introducir el PIN
u8 posescalador;				  // Posescalador para el conteo de 1 segundo
u8 wrong_pin_count;			  // Se usa para contar la cantidad de intentos fallidos al introducir el PIN
bool RotEnc_ignore_next;		  // Se usa para la rutina del encoder
bool ps2_ignore_next = false;	  // Se usa en la rutina de atencion al teclado ps2
u8 entry_hist_counter; // Contador para la cantidad de elemntos en el historial de alarmas
u8 offset_history;	  // Variable que se usa para desplazarse dentro del arreglo de historial de alarmas
bool leap_year_flag;			  // Bandera que indica año bisiesto
u8 month_limit;					  // Establece el limite de la cantidad de dias del mes
u8 limit_num_in;				  // Establece el limite de la cantidad de numeros que el usuario puede introducir en dependencia del modo actual
u8 uart_rx_data;					// Para almacenar el byte recibido por el UART
int cont_menus;

/*********** Prototipos de funciones ***********/

/*Subrutina de atencion a los botones*/
void buttons_isr();

/*Subrutina de atencion al temporizador*/
void timer_0_isr();

/*Funcion para actualizar la RAM de display*/
void update_display_ram();

// Subrutina de atencion al UART Lite
void UART_MDM_isr();

/*Funcion para inicializar las estructuras a sus valores por defecto*/
void init_zone(Zona *zone_to_init);

/*Subrutina de atencion al encoder*/
void encoder_isr(void);

/*Subrutina de atencion a los switches*/
void switches_isr(void);

/*Subrutina de atencion al teclado PS/2*/
void ps2_keyboard_isr(void);

/** Introduce un dato en el buffer de Historal de Alarmas
 * @param alarm_in tipo de alarma que se activo
 * @param zone_id numero de la zona a la que pertenece la alarma
 * @param hour_in hora en que se activo
 * @param min_in minuto en que se activo
 * @param day_in dia en que se activo
 * @param mes_in mes en que se activo
 * @param year_in año en que se activo
 */
void push_History_entry(type_alarm alarm_in, u8 zone_id, u8 hour_in, u8 min_in, u8 day_in, u8 month_in, u8 year_in);

/*Actualiza la variable month_limit calculando el limite de cantidad de dias del mes vigente*/
void calc_month_limit(void);

/** Determina si un año es bisiesto
 * @param year_in año que se desea evaluar
 * @return true si el año es bisieto, false si no es bisiesto
 */
bool is_leap_year(u16 year_in);

int main()
{
	current_mode = IDLE;
	current_menu = Menu_Alarma;
	current_pin = DEFAULT_PIN_KEY;
	sel = 0;
	num_in = 0;
	blink = false;
	blinking_on = false;
	hab_global = true;
	wrong_pin_count = 0;
	entry_hist_counter = 0;
	day = 1;
	month = 1;
	year = 22;
	month_limit = 31;
	leap_year_flag = false;
	lcd_init_delay(); // Espera necesaria para inicializar la LCD
	init_zone(&zona_1);
	init_zone(&zona_2);
	cont_menus = 0;
	//====== Creando caracter personalizado MARK_CHAR ======
	lcd_write_cmd(0x40); // Comando Set CG RAM Address para establecer la direccion 0x00 de la CG RAM
	lcd_write_data(0x00);
	lcd_write_data(0x00);
	lcd_write_data(0x01);
	lcd_write_data(0x02);
	lcd_write_data(0x14);
	lcd_write_data(0x08);
	lcd_write_data(0x00);
	lcd_write_data(0x00);
	//======================================================
	//	Estos pushes son para comprobar el funcionamiento del Historial de Alarmas
	/* 	push_History_entry(Fire, 1, 12, 33, 30, 12, 22);
		push_History_entry(Presence, 2, 11, 30, 15, 4, 23);
		push_History_entry(Presence, 1, 18, 0, 13, 6, 23);
		push_History_entry(Fire, 2, 12, 40, 16, 9, 23); */
	//======================================================
	update_display_ram();
	microblaze_enable_icache();
	horas = 0;
	minutos = 0;
	segundos = 0;
	/* Configurando el timer */
	XTmrCtr_SetLoadReg(XPAR_XPS_TIMER_0_BASEADDR, 0, TMR_BASE);
	XTmrCtr_LoadTimerCounterReg(XPAR_XPS_TIMER_0_BASEADDR, 0);
	XTmrCtr_SetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR, 0, XTC_CSR_ENABLE_INT_MASK | XTC_CSR_AUTO_RELOAD_MASK | XTC_CSR_DOWN_COUNT_MASK);

	/* Configurando interrupciones en INT Controller*/
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_BUTTONS_3BIT_IP2INTC_IRPT_INTR, (XInterruptHandler)buttons_isr, NULL);
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_XPS_TIMER_0_INTERRUPT_INTR, (XInterruptHandler)timer_0_isr, NULL);
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_ROTARY_ENCODER_IP2INTC_IRPT_INTR, (XInterruptHandler)encoder_isr, NULL);
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_MDM_0_INTERRUPT_INTR, (XInterruptHandler)UART_MDM_isr, NULL);
	XIntc_RegisterHandler(XPAR_INTC_0_BASEADDR, XPAR_XPS_INTC_0_XPS_PS2_0_IP2INTC_IRPT_1_INTR, (XInterruptHandler) ps2_keyboard_isr, NULL);
	XIntc_EnableIntr(XPAR_INTC_0_BASEADDR, XPAR_BUTTONS_3BIT_IP2INTC_IRPT_MASK | 
										   XPAR_XPS_TIMER_0_INTERRUPT_MASK | 
										   XPAR_ROTARY_ENCODER_IP2INTC_IRPT_MASK | 
										   XPAR_MDM_0_INTERRUPT_MASK |
										   XPAR_XPS_PS2_0_IP2INTC_IRPT_1_MASK |
										   XPAR_DIP_SWITCHES_4BIT_IP2INTC_IRPT_MASK);
	/* Configurando interrupciones en GPIO -- TIMER0 -- UART -- PS/2*/
	XGpio_WriteReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_GIE_OFFSET, XGPIO_GIE_GINTR_ENABLE_MASK);
	XGpio_WriteReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_IER_OFFSET, 1);
	XGpio_WriteReg(XPAR_ROTARY_ENCODER_BASEADDR, XGPIO_GIE_OFFSET, XGPIO_GIE_GINTR_ENABLE_MASK);
	XGpio_WriteReg(XPAR_ROTARY_ENCODER_BASEADDR, XGPIO_IER_OFFSET, 1);
/* 	XGpio_WriteReg(XPAR_DIP_SWITCHES_4BIT_BASEADDR, XGPIO_GIE_OFFSET, XGPIO_GIE_GINTR_ENABLE_MASK);
	XGpio_WriteReg(XPAR_DIP_SWITCHES_4BIT_BASEADDR, XGPIO_IER_OFFSET, 1); */
	XUartLite_EnableIntr(XPAR_UARTLITE_0_BASEADDR);
	XPs2_WriteReg(XPAR_PS2_0_BASEADDR, XPS2_GIER_OFFSET, XPS2_GIER_GIE_MASK);
    XPs2_WriteReg(XPAR_PS2_0_BASEADDR, XPS2_IPIER_OFFSET, XPS2_IPIXR_RX_FULL);
	// Configurar interrupcion del switch

	/* Habilitando interrupciones */
	XIntc_MasterEnable(XPAR_INTC_0_BASEADDR);
	microblaze_enable_interrupts();

	XTmrCtr_Enable(XPAR_XPS_TIMER_0_BASEADDR, 0); // Disparar temporizador

	while (1)
		;
}

void buttons_isr()
{
	int data_buttons, data_switches;
	typedef enum
	{
		hora,
		minuto,
		segundo,
		dia,
		mes,
		ano,
		done
	} parametros;					  // Parametros necesarios para configurar el reloj
	parametros current_option = hora; // Variable que indica en la opcion en la que se encuentra

	delay(DEBOUNCE_DELAY);
	data_buttons = XGpio_ReadReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_DATA_OFFSET);
	XGpio_WriteReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_ISR_OFFSET, XGpio_ReadReg(XPAR_BUTTONS_3BIT_BASEADDR, XGPIO_ISR_OFFSET)); // Limpiar bandera de interrupcion
	if (current_menu == Menu_Alarma)
	{
		if (data_buttons == 0)																									   // Si se entro por liberacion de boton, se sale
		{
			return;
		}
		switch (data_buttons)
		{
		case CHANGE_BUTTON:
			switch (current_mode)
			{
			case IDLE:
				sel = (sel == 3) ? 0 : (sel + 1);
				break;
			case WRONG_PIN:
				sel = (sel == 3) ? 2 : (sel + 1);
				break;
			case CONF_ALARMA:
			case CONF_ZONA_1:
			case CONF_ZONA_2:
				sel = (sel == 3) ? 0 : (sel + 1);
				break;
			case CONF_RELOJ:
				clock_sel = clock_sel == 6 ? 0 : clock_sel + 1;
				break;
			default:
				break;
			}
			break;

		case OK_BUTTON:
			switch (current_mode)
			{
			case IDLE:
				switch (sel)
				{
				case 0: // Opcion de visualizar las zonas y alarmas habilitadas
					current_mode = ZONAS_ALARMAS_EN;
					break;
				case 1: // Opcion de configurar PIN
					current_mode = PIN_MODE;
					limit_num_in = 4;
					is_conf_alarm_chosen = false;
					break;
				case 2: // Opcion de configurar alarmas
					current_mode = PIN_MODE;
					limit_num_in = 4;
					is_conf_alarm_chosen = true;
					sel = 0;
					break;
				case 3: // Opcion de configurar reloj
					current_mode = CONF_RELOJ;
					clock_sel = 0;
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
				if (num_in_count == 4)
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
							limit_num_in = 8;
							blinking_on = true;
						}
						else
							current_mode = WRONG_PIN;
						sel = 2; // Cursor a la posocion 2 en el siguente menu
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

			case ALARMA_ACTIVA: // Pendiente de revision
				current_mode = IDLE;
				sel = 2;
				blink = false;
				blinking_on = false;
				break;

			case CONF_RELOJ:
				switch (clock_sel)
				{
				case hora:
					horas++;
					if (horas > 23)
						horas = 0;
					break;
				case minuto:
					minutos++;
					if (minutos > 59)
						minutos = 0;
					break;
				case segundo:
					segundos = 0;
					break;
				case dia:
					day = day == month_limit ? 1 : day + 1;
					break;
				case mes:
					month = month == 12 ? 1 : month + 1;
					calc_month_limit();
					break;
				case ano:
					year = year == 99 ? 0 : year + 1;
					leap_year_flag = is_leap_year(year + 2000);
					calc_month_limit();
					break;
				case done: // Condicion para salir de la configuracion del reloj
					current_mode = IDLE;
					sel = 2;
					break;
				default:
					break;
				}
				break;

			case CONF_PIN:
				if (num_in_count == 4)
				{
					num_in_count = 0;
					current_pin = num_in;
					current_mode = CONF_PIN_SUCCESSFULLY;
					num_in = 0;
					sel = 2; // Cursor a la posicion 2 en el siguiente menú
				}
				break;

			case CONF_PIN_SUCCESSFULLY:
				current_mode = IDLE;
				sel = 2;
				break;

			case PUK_MODE:
				if (num_in_count == 8)
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
					sel = 2; // Cursor a la posicion 2 en el siguiente menú
				}
				break;

			case WRONG_PUK:
				current_mode = PUK_MODE;
				break;

			case ZONAS_ALARMAS_EN:
				current_mode = IDLE;
				break;

			default:
				break;
			}
			break;

		default:
			break;
		}
		update_display_ram();
	}
	return;
}

void encoder_isr()
{
	char data_encoder;
	delay(DEBOUNCE_DELAY);
	data_encoder = XGpio_ReadReg(XPAR_ROTARY_ENCODER_BASEADDR, XGPIO_DATA_OFFSET);
	switch (data_encoder)
	{
	case 0x00:
		RotEnc_ignore_next = true;
		break;

	case 0x04: // Derecha
		if (RotEnc_ignore_next == false)
		{
			offset_history = offset_history == (entry_hist_counter - 1) ? offset_history : offset_history + 1;
		}
		break;

	case 0x02: // Izquierda
		if (RotEnc_ignore_next == false)
		{
			offset_history = offset_history == 0 ? 0 : offset_history - 1;
		}
		break;

	case 0x07: // Boton
		if (current_mode == IDLE || current_mode == ALARM_HISTORY)
			current_mode = current_mode == IDLE ? ALARM_HISTORY : IDLE;
		offset_history = 0;
		break;

	case 0x06:
		RotEnc_ignore_next = false;
		break;

	default:
		break;
	}
	XGpio_WriteReg(XPAR_ROTARY_ENCODER_BASEADDR, XGPIO_ISR_OFFSET, XGpio_ReadReg(XPAR_ROTARY_ENCODER_BASEADDR, XGPIO_ISR_OFFSET)); // Limpiar bandera
	update_display_ram();
	return;
}

void UART_MDM_isr()
{
	if (XUartLite_IsReceiveEmpty(XPAR_UARTLITE_0_BASEADDR) != TRUE) // Comprobar si la interrupcion es por recepcion
	{
		/*Almacenamiento e identificacion del codigo de la alarma*/
		uart_rx_data = XUartLite_RecvByte(XPAR_UARTLITE_0_BASEADDR);
		if (hab_global) // Realizar solo si la habilitacion global esta activada
			if (uart_rx_data == Codigo_I1 || uart_rx_data == Codigo_I2 || uart_rx_data == Codigo_P1 || uart_rx_data == Codigo_P2)
			{
				switch (uart_rx_data)
				{
				case Codigo_I1:
					if (zona_1.hab_zona)
						if (zona_1.hab_incendio == true)
						{
							zona_1.state_incendio = TRUE;
							current_mode = ALARMA_ACTIVA;
							blinking_on = true;
							push_History_entry(Fire, 1, horas, minutos, day, month, year);
						}
					break;
				case Codigo_P1:
					if (zona_1.hab_zona)
						if (zona_1.hab_presencia == true)
						{
							zona_1.state_presencia = TRUE;
							current_mode = ALARMA_ACTIVA;
							blinking_on = true;
							push_History_entry(Presence, 1, horas, minutos, day, month, year);
						}
					break;
				case Codigo_I2:
					if (zona_2.hab_zona)
						if (zona_2.hab_incendio == true)
						{
							zona_2.state_incendio = TRUE;
							current_mode = ALARMA_ACTIVA;
							blinking_on = true;
							push_History_entry(Fire, 2, horas, minutos, day, month, year);
						}
					break;
				case Codigo_P2:
					if (zona_2.hab_zona)
						if (zona_2.hab_presencia == true)
						{
							zona_2.state_presencia = TRUE;
							current_mode = ALARMA_ACTIVA;
							blinking_on = true;
							push_History_entry(Presence, 2, horas, minutos, day, month, year);
						}
					break;
				default:
					break;
				}
				/*Transmision del codigo de la alarma*/
				//			XUartLite_SendByte(XPAR_UARTLITE_0_BASEADDR,uart_rx_data);
			}
			else
				uart_rx_data = 0;
	}
	update_display_ram();
	return;
}

void timer_0_isr()
{
	char i;

	XTmrCtr_SetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR, 0, XTmrCtr_GetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR, 0)); // Limpiar bandera de solicitud de intr
	posescalador++;
	if (posescalador == 4)
	{
		posescalador = 0;
		if (segundos == 59)
		{
			if (minutos == 59)
			{
				if (horas == 23)
				{
					if (day == month_limit)
					{
						if (month == 12)
						{
							year = year == 99 ? 0 : year + 1;
							month = 1;
							month_limit = 31;
							leap_year_flag = is_leap_year(year + 2000);
						}
						else
							month++;
						calc_month_limit();
						day = 1;
					}
					else
						day++;
					horas = 0;
				}
				else
					horas++;
				minutos = 0;
			}
			else
				minutos++;
			segundos = 0;
		}
		else
			segundos++;
	}
	if (current_mode == CONF_RELOJ || current_menu == Menu_Reloj)
	{
		display_RAM[0] = horas / 10 + 0x30;
		display_RAM[1] = horas % 10 + 0x30;
		display_RAM[2] = ':';
		display_RAM[3] = minutos / 10 + 0x30;
		display_RAM[4] = minutos % 10 + 0x30;
		display_RAM[5] = ':';
		display_RAM[6] = segundos / 10 + 0x30;
		display_RAM[7] = segundos % 10 + 0x30;

		display_RAM[16] = day / 10 + 0x30;
		display_RAM[17] = day % 10 + 0x30;
		display_RAM[18] = '/';
		display_RAM[19] = month / 10 + 0x30;
		display_RAM[20] = month % 10 + 0x30;
		display_RAM[21] = '/';
		display_RAM[22] = year / 10 + 0x30;
		display_RAM[23] = year % 10 + 0x30;
		if (blink == true)
		{
			switch (clock_sel)
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
			case 3:
				display_RAM[16] = ' ';
				display_RAM[17] = ' ';
				break;
			case 4:
				display_RAM[19] = ' ';
				display_RAM[20] = ' ';
				break;
			case 5:
				display_RAM[22] = ' ';
				display_RAM[23] = ' ';
				break;
			default:
				break;
			}
		}
	}
	lcd_SetAddress(0x00);
	for (i = 0; i < 32; i++) // Refrescamiento de pantalla
	{
		if (i == 16)
			lcd_SetAddress(0x40);
		lcd_write_data(display_RAM[i]);
	}
	if (blinking_on) // Parpadeo de los LEDs
	{
		if (blink)
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
		display_RAM[i] = ' '; // Se limpia toda la RAM de display
	}

	if (current_mode != PIN_MODE && current_mode != ALARMA_ACTIVA && current_mode != CONF_RELOJ && current_mode != PUK_MODE && current_mode != ALARM_HISTORY && current_mode != ZONAS_ALARMAS_EN)
		display_RAM[sel * 8] = ARROW_CHAR; // Se coloca la flecha segun el selector

	switch (current_mode)
	{
	case IDLE:
		display_RAM[1] = 'E';
		display_RAM[2] = 'n';
		display_RAM[3] = 'a';
		display_RAM[4] = 'b';
		display_RAM[5] = 'l';
		display_RAM[6] = 'e';
		display_RAM[7] = 'd';

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
		display_RAM[25] = (clock_sel == 6) ? ARROW_CHAR : ' ';
		display_RAM[26] = 'O';
		display_RAM[27] = 'K';
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

	case ALARM_HISTORY:
		if (entry_hist_counter == 0)
		{
			display_RAM[0] = 'E';
			display_RAM[1] = 'm';
			display_RAM[2] = 'p';
			display_RAM[3] = 't';
			display_RAM[4] = 'y';
			display_RAM[6] = 'H';
			display_RAM[7] = 'i';
			display_RAM[8] = 's';
			display_RAM[9] = 't';
			display_RAM[10] = 'o';
			display_RAM[11] = 'r';
			display_RAM[12] = 'y';
		}
		else
		{
			display_RAM[0] = offset_history / 10 + 0x30;
			display_RAM[1] = offset_history % 10 + 0x30;
			display_RAM[3] = history[offset_history].hour / 10 + 0x30;
			display_RAM[4] = history[offset_history].hour % 10 + 0x30;
			display_RAM[5] = ':';
			display_RAM[6] = history[offset_history].min / 10 + 0x30;
			display_RAM[7] = history[offset_history].min % 10 + 0x30;
			display_RAM[9] = 'Z';
			display_RAM[10] = 'o';
			display_RAM[11] = 'n';
			display_RAM[12] = 'e';
			display_RAM[13] = history[offset_history].zone + 0x30;
			display_RAM[14] = offset_history == 0 ? ' ' : '<';
			display_RAM[15] = offset_history == (entry_hist_counter - 1) ? ' ' : '>';
			display_RAM[16] = history[offset_history].day / 10 + 0x30;
			display_RAM[17] = history[offset_history].day % 10 + 0x30;
			display_RAM[18] = '/';
			display_RAM[19] = history[offset_history].month / 10 + 0x30;
			display_RAM[20] = history[offset_history].month % 10 + 0x30;
			display_RAM[21] = '/';
			display_RAM[22] = history[offset_history].year / 10 + 0x30;
			display_RAM[23] = history[offset_history].year % 10 + 0x30;

			if (history[offset_history].alarm == Fire)
			{
				display_RAM[25] = 'F';
				display_RAM[26] = 'i';
				display_RAM[27] = 'r';
				display_RAM[28] = 'e';
			}
			else
			{
				display_RAM[25] = 'P';
				display_RAM[26] = 'r';
				display_RAM[27] = 'e';
				display_RAM[28] = 's';
				display_RAM[29] = 'e';
				display_RAM[30] = 'n';
				display_RAM[31] = '.';
			}
		}
		break;

	case ZONAS_ALARMAS_EN:
		// Informacion de Zona 1
		if (zona_1.hab_zona == TRUE)
			display_RAM[0] = MARK_CHAR;

		if (zona_1.hab_incendio == TRUE)
			display_RAM[8] = MARK_CHAR;

		if (zona_1.hab_presencia == TRUE)
			display_RAM[12] = MARK_CHAR;

		display_RAM[1] = 'Z';
		display_RAM[2] = 'o';
		display_RAM[3] = 'n';
		display_RAM[4] = 'a';
		display_RAM[5] = '1';
		display_RAM[6] = ':';

		display_RAM[9] = 'I';
		display_RAM[10] = '1';

		display_RAM[13] = 'P';
		display_RAM[14] = '1';

		// Informacion de Zona 2
		if (zona_2.hab_zona == TRUE)
			display_RAM[15] = MARK_CHAR;

		if (zona_2.hab_incendio == TRUE)
			display_RAM[24] = MARK_CHAR;

		if (zona_2.hab_presencia == TRUE)
			display_RAM[28] = MARK_CHAR;

		display_RAM[17] = 'Z';
		display_RAM[18] = 'o';
		display_RAM[19] = 'n';
		display_RAM[20] = 'a';
		display_RAM[21] = '2';
		display_RAM[22] = ':';

		display_RAM[25] = 'I';
		display_RAM[26] = '2';

		display_RAM[29] = 'P';
		display_RAM[30] = '2';

		break;

	default:
		break;
	}
	return;
}

void init_zone(Zona *zone_to_init)
{
	zone_to_init->hab_zona = true;
	zone_to_init->hab_incendio = true;
	zone_to_init->hab_presencia = true;
	zone_to_init->state_incendio = false;
	zone_to_init->state_presencia = false;
	return;
}

void push_History_entry(type_alarm alarm_in, u8 zone_id, u8 hour_in, u8 min_in, u8 day_in, u8 month_in, u8 year_in)
{
	if (entry_hist_counter == 100)
		entry_hist_counter = 0;
	history[entry_hist_counter].alarm = alarm_in;
	history[entry_hist_counter].zone = zone_id;
	history[entry_hist_counter].hour = hour_in;
	history[entry_hist_counter].min = min_in;
	history[entry_hist_counter].day = day_in;
	history[entry_hist_counter].month = month_in;
	history[entry_hist_counter].year = year_in;
	entry_hist_counter++;
	return;
}

void calc_month_limit(void)
{
	if (month >= 3 && month <= 12 || month == 1)
	{
		if (month <= 7)
		{
			if (month % 2 == 0)
				month_limit = 30;
			else
				month_limit = 31;
		}
		else
		{
			if (month % 2 == 0)
				month_limit = 31;
			else
				month_limit = 30;
		}
	}
	else // Febrero
	{
		if (leap_year_flag == 1)
			month_limit = 29; // Año bisiseto
		else
			month_limit = 28;
	}
	return;
}

bool is_leap_year(u16 year_in)
{
	if ((year_in % 4) == 0)
	{
		if ((year_in % 100) == 0)
		{
			if ((year_in % 400) == 0)
				return true;
		}
		else
			return true;
	}
	else
	{
		return false;
	}
}

void switches_isr(void)
{
	// Modificar el valor del contador del menu en dependencia de el valor de los switches
	return;
}

void ps2_keyboard_isr(void)
{
	u8 scan_code, numkey_in;
	bool ignore_key = false;

	XPs2_WriteReg(XPAR_PS2_0_BASEADDR, XPS2_IPISR_OFFSET, XPs2_ReadReg(XPAR_PS2_0_BASEADDR, XPS2_IPISR_OFFSET));    // Limpiar bandera
	if (current_mode != CONF_PIN && current_mode != PIN_MODE && current_mode != PUK_MODE)	// Si no se esta en los modos especificados se retorna
		return;
    scan_code = XPs2_ReadReg(XPAR_PS2_0_BASEADDR, XPS2_RX_DATA_OFFSET);
    if (scan_code == 0xF0)
    {
        ps2_ignore_next = true;
        return;
    }
    if (ps2_ignore_next)
    {
        ps2_ignore_next = false;
        return;
    }
	if (scan_code >= 0x69 && scan_code <= 0x7D)		// Scan codes del Numpad del teclado
	{
		switch (scan_code)
		{
		case 0x70:
			numkey_in = 0;
			break;
		case 0x69:
			numkey_in = 1;
			break;
		case 0x72:
			numkey_in = 2;
			break;
		case 0x7A:
			numkey_in = 3;
			break;
		case 0x6B:
			numkey_in = 4;
			break;
		case 0x73:
			numkey_in = 5;
			break;
		case 0x74:
			numkey_in = 6;
			break;
		case 0x6C:
			numkey_in = 7;
			break;
		case 0x75:
			numkey_in = 8;
			break;
		case 0x7D:
			numkey_in = 9;
			break;
		case DELETE_KEY:
			numkey_in = DELETE_KEY;
			break;
		default:
			ignore_key = true;
			break;
		}
		if(!ignore_key)
		{
			if (numkey_in == DELETE_KEY)
			{
				num_in = num_in / 10;
				num_in_count = num_in_count == 0 ? 0 : num_in_count - 1;
			}
			else if (num_in_count < limit_num_in)
			{
				num_in = num_in * 10 + numkey_in;
//				num_in_count = num_in_count > 4 ? 4 : num_in_count + 1;
				num_in_count++;
			}
		}
	}
	update_display_ram();
	return;
}

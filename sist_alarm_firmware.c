#include <xparameters.h>
#include <xgpio_l.h>
#include <lcd_ip.h>
#include <xuartlite_l.h>
#include <mb_interface.h>
#include <xtmrctr_l.h>
#include <xintc_l.h>

/*********** Definiciones ***********/
#define TMR_BASE 12500000 // Base de conteo para obtener una frecuencia de 4 Hz

/*********** Definiciones de tipo ***********/
typedef enum {IDLE, ALARMA_ACTIVA, CONF_ALRMA, CONF_ZONA_1, CONF_ZONA_2}modo;	// Definicion de tipo modo

/*********** Variables globales ***********/
modo current_mode;	// Modo actual en que se encuentra el sistema
char display_RAM[32];	// RAM de display. Cada posicion se corresponde con un recuadro de la LCD

int main()
{
	
	/* Configurando el timer */
	XTmrCtr_SetLoadReg(XPAR_XPS_TIMER_0_BASEADDR, 0, TMR_BASE);
	XTmrCtr_LoadTimerCounterReg(XPAR_XPS_TIMER_0_BASEADDR, 0);
	XTmrCtr_SetControlStatusReg(XPAR_XPS_TIMER_0_BASEADDR, 0, XTC_CSR_ENABLE_INT_MASK | XTC_CSR_AUTO_RELOAD_MASK | XTC_CSR_DOWN_COUNT_MASK);

	while(1);
}

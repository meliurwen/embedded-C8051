#include <c8051f020.h>

#define DISP_ADDRESS 0x7C
#define TERM_ADDRESS 0x90
#define ACCEL_ADDRESS 0x98

#define SMB_START 0x08
#define SMB_RESTART 0x10
#define SMB_ADDR_NACK 0x20
#define SMB_ADDR_NACK_R 0x48
#define SMB_ADDR_ACK 0x18
#define SMB_ADDR_ACK_R 0x40
#define SMB_DATA_ACK 0x28
#define SMB_DATA_NACK 0x30
#define SMB_DATA_R_ACK 0x50
#define SMB_DATA_R_NACK 0x58

//Timer 0
#define TL0V 0xE5	//Low byte
#define TH0V 0xBE	//High byte

#define LUNGH_BUFFER 8	//Lunghezza buffer dell'accelerometro

sbit LedDisplay = P0^6;
sbit Button = P3^7;

/////i2c VAR /////
unsigned char _i2c_address;
char* _i2c_data;
char* _i2c_data_read;
unsigned char _i2c_data_length = 0;
unsigned char _i2c_data_read_length = 0;
/////////////////

/////INIZIO PWM
bit statoDisplay = 0;
bit button_flag = 0;
bit mode = 0;	//Modalità aumento o diminuzione brightness
unsigned char contatore_timer0 = 0;
unsigned char lum = 0;
/////FINE PWM


//Contatori all'interno dell'i2c
unsigned char contatore = 0;
unsigned char contatore2 = 0;
unsigned char contatore3 = 0;
unsigned char contatore4 = 0;

//Robe da scrivere sul display
unsigned char display_riga1[] = "@T 00.0ßC X= 00.0";
unsigned char display_riga2[] = "@ Y= 00.0 Z= 00.0";

//Variabili ed array temporanei
int temperaturaTemp = 0;
int assi_conv[] = {0, 0, 0};	//X,Y, e Z convertiti

//Lock
bit _i2c_lock = 0;
bit _noSto = 0; //Serve per l'accelerometro che nella fase di lettura dell'acelerazione c'è bisogno di sun secondo START senza lo STO

//Comandi per i vari dispositivi
char init_display[] = { 0x38, 0x39, 0x14, 0x74, 0x54, 0x6F, 0x0C, 0x01 };
char init_accel[] = { 0x07, 0x01 };
char cursor_display[] = { 0x80, 0x00 };
char init_termometro[] = { 0x08, 0x40, 0x50, 0x50 };
char clear_display[] = {0x80, 0x01};
char assi[] = {0x00};

//Dati estratti in lettura dall'SMB0DAT
unsigned char indice_buffer = 0;
char assi_buffer[LUNGH_BUFFER][3];

//Dovrà contenere i dati grezzi estratti dal termometro
typedef union int_chars{
	int temp;
	unsigned char raw[2]; 
}int_chars;

//Inizializzo le variabili della union per il termometro
int_chars therm_data;
int_chars generic_data;


//Qualche #define spastico sulla gestione degli eventi per rendere la lettura del codice un po' più igienica :)
#define EV_ENABLE(ev_enum) (ev_flags |= (1 << ev_enum))
#define EV_DISABLE(ev_enum) (ev_flags &= ~(1 << ev_enum))
#define IF_EV(ev_num) if(ev_flags & (1 << ev_num))
unsigned int ev_flags = 0;

//Registro che gestisce i flag degli eventi
typedef enum Event {
	ev_termometro_read,
	ev_termometro_calcola,
	ev_accel_init,
	ev_read_assi,
	ev_store_assi,
	ev_calc_media,
	ev_display_riga1,
	ev_display_riga2,
	ev_display_clear,
	ev_display_cursor_move,
	ev_nop	//Flag dummy che serve quando dalla funzione è rischiesto un argomento Event ma non si vuole fare alcuna operazione
} Event;

//evento che deve essere abilitato quando l'i2c ha finito il lavoro assegnatogli
Event _i2c_callback;


//Lookup table per l'accelerometro
code int lookup_table[32] = {
	0,
	27,
	54,
	81,
	108,
	135,
	163,
	192,
	220,
	250,
	280,
	310,
	342,
	375,
	410,
	447,
	486,
	528,
	575,
	630,
	696,
	799,
	900,
	900,
	900,
	900,
	900,
	900,
	900,
	900,
	900,
	900	
};



//Funzione universale per lanciare i comandi all'i2c
void i2c_command(int address, char* datas, int datas_length, char* datas_read, Event callback){
	
	_i2c_lock = 1;
	if((address == ACCEL_ADDRESS) && (datas[0] == 0x00))	//Alza un flag quando nella fase di lettura l'accelerometro deve fare il secondo START
		_noSto = 1;
	_i2c_callback = callback; //Chiama una funzione che deve mandatoriamente essere eseguita subito dopo di questa
	_i2c_address = address;
	_i2c_data = datas;
	_i2c_data_length = datas_length-1;
	_i2c_data_read = datas_read;
	if(address == ACCEL_ADDRESS)		//I casi di lettura sono solo 2 (accelerometro o termometro)
		_i2c_data_read_length = 3;	//Accelerometro: i dati sono X, Y e Z
	else
		_i2c_data_read_length = 2;	//Termometro: valore della temperatura spezzato in due
	
	contatore = 0;
	contatore2 = 0;
	
	STO = 0;
	STA = 1;
}


//Funzioni display
void display_write(char* display_riga2, int length, Event callback){
	i2c_command(DISP_ADDRESS & 0xFE, display_riga2, length, generic_data.raw, callback);
}

void display_clear(Event callback){
	i2c_command(DISP_ADDRESS & 0xFE, &clear_display[0], sizeof clear_display, generic_data.raw, callback);
}

void display_init(Event callback){
	i2c_command(DISP_ADDRESS & 0xFE, init_display, sizeof init_display, generic_data.raw, callback);
}

void display_cursor(char disp_ram_addr, Event callback){
	cursor_display[1] = disp_ram_addr;
	i2c_command(DISP_ADDRESS & 0xFE, cursor_display, sizeof cursor_display, generic_data.raw, callback);
}


//Funzioni Termometro
void termometro_read(Event callback){
	i2c_command(TERM_ADDRESS | 0x01, init_termometro, sizeof init_termometro, therm_data.raw, callback);
}

void termometro_calcola(){
	therm_data.temp = therm_data.temp >> 3; //Shift a destra di 3 bit
	//Se negativo sistema la rappresentazione a complemento a 2
	if(therm_data.raw[0] & 0x10)
		therm_data.raw[0] |= 0xF0;
	temperaturaTemp = therm_data.temp;
}


//Funzioni Accelerometro
void accel_init(){
	i2c_command(ACCEL_ADDRESS & 0xFE, init_accel, sizeof init_accel, generic_data.raw, ev_nop);
}

void read_assi(Event callback){
	i2c_command(ACCEL_ADDRESS & 0xFE, assi, sizeof assi, assi_buffer[indice_buffer], callback);
}

int orientation(char indice){
	if (indice & 0x20){
		indice = ~(indice|0xC0) + 1;
		return -lookup_table[indice];
	}
	indice = indice & 0x1f;
	return lookup_table[indice];
}

void store_assi(){
	if(indice_buffer == (LUNGH_BUFFER - 1))
		indice_buffer = 0;
	else
		indice_buffer = indice_buffer + 1;
}



//Converte e calcola la media delle inclinazioni degli assi dell'accelerometro.
//Converte il termometro e poi prepara gli array di char che verranno poi usati per stampare a schermo.
void calc_media(Event callback){

	//INIZIO ACCELEROMETRO
	assi_conv[0] = 0;
	assi_conv[1] = 0;
	assi_conv[2] = 0;

	for(contatore3 = 0; contatore3 < LUNGH_BUFFER; contatore3 = contatore3 + 1){
		assi_conv[0] = assi_conv[0] + orientation(assi_buffer[contatore3][0]);
		assi_conv[1] = assi_conv[1] + orientation(assi_buffer[contatore3][1]);
		assi_conv[2] = assi_conv[2] + orientation(assi_buffer[contatore3][2]);
	}
	assi_conv[0] = assi_conv[0]/LUNGH_BUFFER;
	assi_conv[1] = assi_conv[1]/LUNGH_BUFFER;
	assi_conv[2] = assi_conv[2]/LUNGH_BUFFER;
	
	if(assi_conv[0] >= 0 )
		display_riga1[12] = '+';
	else{
		display_riga1[12] = '-';
		assi_conv[0] = -assi_conv[0];
	}
	display_riga1[13] = (assi_conv[0]/100) + '0'; //sommo il char '0' così converto il numero al corrispondente numero in codifica ASCII
	display_riga1[14] = ((assi_conv[0]%100)/10) + '0';
	display_riga1[16] = (assi_conv[0]%10) + '0';
	
	if(assi_conv[1] >= 0 )
		display_riga2[4] = '+';
	else{
		display_riga2[4] = '-';
		assi_conv[1] = -assi_conv[1];
	}
	display_riga2[5] = (assi_conv[1]/100) + '0';
	display_riga2[6] = ((assi_conv[1]%100)/10) + '0';
	display_riga2[8] = (assi_conv[1]%10) + '0';

	if(assi_conv[2] >= 0 )
		display_riga2[12] = '+';
	else{
		display_riga2[12] = '-';
		assi_conv[2] = -assi_conv[2];
	}
	display_riga2[13] = (assi_conv[2]/100) + '0';
	display_riga2[14] = ((assi_conv[2]%100)/10) + '0';
	display_riga2[16] = (assi_conv[2]%10) + '0';
	//FINE ACCELEROMETRO
	
	//INIZIO TERMOMETRO
	if(temperaturaTemp >= 0 )
		display_riga1[2] = '+';
	else{
		display_riga1[2] = '-';
		temperaturaTemp = -temperaturaTemp;
	}
	//display_riga1[2] = ((temperaturaTemp / 16) / 100) + '0'; //Anche se in un certo senso potrebbe arrivarci, nella realtà non ci arriverà mai
	display_riga1[3] = (((temperaturaTemp / 16) % 100) / 10) + '0';
	display_riga1[4] = ((temperaturaTemp / 16) % 10) + '0';
	display_riga1[6] = (((temperaturaTemp % 16)*10)/16) + '0';
	//FINE TERMOMETRO
	
	EV_ENABLE(callback);
}



void init (void){
		LedDisplay = 0;
		EA = 0;			//Abilita tutti gli Interupts
		WDTCN = 0x0DE;		//Disabilita tutti Watchdogs
		WDTCN = 0x0AD;		//
		OSCICN &= 0x014;	//Disable missing clock detector and set osc at 2 MHz as the clock source
		XBR0 = 0x005;		//Imposta ed abilita il crossbar
		XBR1 = 0x000;		//
		XBR2 = 0x040;		//
		P0MDOUT |= 0x040;	//Set P0.6 to push-pull
		EIE1 |= 0x02;		//Abilita l'interrupt SMBUS
		ET0 = 1;		//Abilita interrupt timer 0
		ET1 = 1;		//Abilita interrupt timer 1
		EA = 1;			//Abilita tutti gli interrupts
		TMOD |= 0x11;		//Entrambi i timer in modalità 16 bit
		TL0 = TL0V;
		TH0 = TH0V;
		TR0 = 1; 		//Abilita timer 0
		TR1 = 1;		//Abilita timer 1
		SMB0CN = 0x44;		//Abilita l'SMB 
		SMB0CR = -80;
		SCON0 |= 0x10;		//Abilita la uart0 a ricevere
}


void SMBUS_ISR () interrupt 7{
	switch (SMB0STA){
		case SMB_RESTART:
		case SMB_START:
			SMB0DAT = _i2c_address;
			STA=0;
			break;
		case SMB_ADDR_ACK_R:
		case SMB_ADDR_ACK:
			SMB0DAT = _i2c_data[0];
			break;
		case SMB_ADDR_NACK:
			//Cerca di riprendere la comunicazione
			STO = 1;
			STA = 0;
			STO = 0;
			STA = 1;
			break;
		case SMB_DATA_ACK:
			if(contatore < _i2c_data_length){
				contatore++;
				SMB0DAT = _i2c_data[contatore];
			}
			else{
					if(_noSto){
						_noSto = 0;
						_i2c_address = ACCEL_ADDRESS | 0x01; //Indirizzo dell'accelerometro in modalità lettura
						STA = 1;
					}
					else{
					STA=0;
					STO=1;
					EV_ENABLE(_i2c_callback);
					_i2c_lock = 0;
					}
				}
			break;
			case SMB_DATA_R_ACK:
				if(contatore2 < _i2c_data_read_length){
					_i2c_data_read[contatore2] = SMB0DAT;
					contatore2++;
					if(contatore2 == 3) //controlla se è l'ultimo dato da leggere, se sì allora il prossimo è un NACK
						AA = 0;
				}
			else{
					STA=0;
					STO=1;
					EV_ENABLE(_i2c_callback);
					_i2c_lock = 0;
				}
			break;
			case SMB_DATA_R_NACK:
					STA=0;
					STO=1;
					AA = 1;
					EV_ENABLE(_i2c_callback);
					_i2c_lock = 0;
			break;
	}
	SI = 0;
}


//Il timer cicla ogni 100ms
void timer0() interrupt 1{
	TL0 = TL0V;
	TH0 = TH0V;

	//Ogni 100ms
	EV_ENABLE(ev_read_assi);
	
	//Ogni 300ms
	if(contatore_timer0 % 3 == 0){
		EV_ENABLE(ev_calc_media);
	}
	
	//Ogni 1000ms
	if(contatore_timer0 % 10 == 0){
		EV_ENABLE(ev_termometro_read);
	}

	contatore_timer0++;
	if(contatore_timer0 > 29)
		contatore_timer0 = 0;
	
	//PWM
	if(button_flag){
		//Per impedire che dopo un po' che tengo il pulsante premuto si interrompa la config mode
		if(contatore4 == 255)
			contatore4 = 10;
		else
			contatore4 += 1;
		
		//Dopo un secondo entra in config mode
		if(contatore4 > 9){
			if(lum == 0)
				mode = 1;
			else
				if(lum == 240)
					mode = 0;
			if(mode)
				lum+=8;
			else
				lum-=8;
			lum = lum %248;
		}
	}
	else{
		if((contatore4 < 10) & (contatore4 > 0))
			statoDisplay = ~statoDisplay;
		contatore4 = 0;
	}
}

//Timer del PWM
void timer1() interrupt 3 {
	if (statoDisplay)
		LedDisplay = 0;
	else {
		if(LedDisplay)
			TL1 = lum;
		else
			TL1 = 248 - lum;
		TH1 = 255;
		LedDisplay = ~LedDisplay;
	}
}



void scheduler(){
	if(!_i2c_lock){
		IF_EV(ev_accel_init){
			EV_DISABLE(ev_accel_init);
			accel_init();
		}else IF_EV(ev_read_assi){
			EV_DISABLE(ev_read_assi);
			read_assi(ev_store_assi);
		}else IF_EV(ev_termometro_read){
			EV_DISABLE(ev_termometro_read);
			termometro_read(ev_termometro_calcola);
		}else IF_EV(ev_display_riga1) {
			EV_DISABLE(ev_display_riga1);
			display_write(display_riga1, sizeof(display_riga1)-1, ev_display_cursor_move);
		}else IF_EV(ev_display_riga2) {
			EV_DISABLE(ev_display_riga2);
			display_write(display_riga2, sizeof(display_riga2)-1, ev_display_cursor_move);
		}else IF_EV(ev_display_clear){
			EV_DISABLE(ev_display_clear);
			display_clear(ev_display_riga1);
		}else IF_EV(ev_display_cursor_move){
			EV_DISABLE(ev_display_cursor_move);
			display_cursor(0xC0, ev_display_riga2);
		}
	}
	
	IF_EV(ev_store_assi){
		EV_DISABLE(ev_store_assi);
		store_assi();
	}
	
	IF_EV(ev_termometro_calcola){
		EV_DISABLE(ev_termometro_calcola);
		termometro_calcola();
	}
	
	IF_EV(ev_calc_media){
		EV_DISABLE(ev_calc_media);
		calc_media(ev_display_clear);
	}
}



void main(void){
	init();
	display_init(ev_accel_init);
	while(1){
		scheduler();
		if(!Button)
			button_flag = 1;
		else
			button_flag = 0;
	}
}

/// @dir bandgap
/// Try reading the bandgap reference voltage to measure current VCC voltage.
/// @see http://jeelabs.org/2012/05/12/improved-vcc-measurement/
// 2012-04-22 <jc@wippler.nl> http://opensource.org/licenses/mit-license.php

#include <JeeLib.h>
//#include <avr/sleep.h>

#define DEBUG	 1	 // set to 1 to display each loop() run and PIR trigger

#define NODE_ID				 3				 // NodeId of this JeeNode
#define GROUP_ID				5				 // GroupId of this JeeNode

//Message max 8 bytes
struct payload {
	 unsigned int pompAanTijd;		//Byte 0..1
	 unsigned int pompUitTijd;		//Byte 2..3
	 bool pompAanRelais;					//Byte 4
	 char waterDruk;							//Byte 5
	 char waterDrukAan;					 //Byte 6
	 char waterDrukUit;					 //Byte 7
	 bool pompLed;								//Byte 8
	 bool storingsLed;						//Byte 9
	 bool pompBlokkeren;					//Byte 10
	 unsigned char waterdrukMask; //Byte 11
	 char waarschuwingsMelding;	 //Byte 12
	 char alarmMelding;					 //Byte 13
	 bool waterDrukUitlezingOK;	 //Byte 14
} payload;

volatile char waterDruk = 0;
volatile float sumWaterDruk = 0;
volatile char metingTeller = 0;
volatile char gemiddeldeWaterdruk = 0;
volatile char oldGemiddeldeWaterdruk = 0;
volatile char waterDrukStabiel = 0;
volatile bool waterDrukUitlezingOK = false;
volatile char waterDrukAan = 10;
volatile char waterDrukUit = 50;
volatile unsigned int pompAanTijd = 0;
volatile unsigned int vorigePompAanTijd = 0;
volatile unsigned int pompUitTijd = 0;
volatile unsigned int vorigePompUitTijd = 0;

volatile bool pompAanRelais = false;
volatile bool oldPompAanRelais = false;
volatile bool pompBlokkeren = false;

volatile char secondCounter = 10;
volatile bool timerTick = true;

volatile bool sendMsg = true; //Zend bericht bij opstarten/herstarten
volatile bool debugMsg = false;
volatile char rxCommand = 0;
volatile unsigned char waterdrukMask = 0;
volatile char alarmMelding = 0;
volatile char waarschuwingsMelding = 0;

//Pins JeeNode port 1
#define PORT_IRQ	 3 //Arduino pin volgorde
#define PORT1_DIO	4 //Arduino pin volgorde
#define PORT1_AIO 14 //Arduino pin volgorde

//Pins JeeNode port 2
#define PORT2_DIO	5 //Arduino pin volgorde
#define PORT2_AIO 15 //Arduino pin volgorde

//Pins JeeNode port 3
#define PORT3_DIO	6 //Arduino pin volgorde
#define PORT3_AIO 16 //Arduino pin volgorde

//Pins JeeNode port 4
#define PORT4_DIO	7 //Arduino pin volgorde
#define PORT4_AIO 17 //Arduino pin volgorde

// Pomp alarmen en waarschuwingen
#define ALARM_POMP_TE_LANG_ACTIEF						 1
#define ALARM_ORG_POMP_REGELING_ZET_POMP_UIT	2
#define ALARM_ORG_REGELING_IS_UITGESCHAKELD	 3
#define ALARM_DROOGLOOP_BEVEILIGING					 4

#define WAARSCHUWING_WATERDRUK_TE_LAAG				1

//Pin change interrupt routine for PortD0..7, only PD4 is used
//ISR(PCINT2_vect) { if (waterDruk >= waterDrukUit) setPompAanRelais(false); }

//External interrupt 1: PORT_IRQ (Led: 2 bar)
//void pompAanIRQ () { setPompAanRelais(true); }

char getWaterDruk (void) {
	 waterdrukMask = 0;
	 char tempWaterDruk = 0;

	 if (!digitalRead(PORT1_AIO)) { tempWaterDruk = 10; waterdrukMask |= 0b1;		 } //1 Bar=0x00.0001
	 if (!digitalRead(PORT_IRQ) ) { tempWaterDruk = 20; waterdrukMask |= 0b10;		} //2 Bar=0x00.0011
	 if (!digitalRead(PORT3_DIO)) { tempWaterDruk = 30; waterdrukMask |= 0b100;	 } //3 Bar=0x00.0111
	 if (!digitalRead(PORT2_DIO)) { tempWaterDruk = 40; waterdrukMask |= 0b1000;	} //4 Bar=0x00.1111
	 if (!digitalRead(PORT1_DIO)) { tempWaterDruk = 50; waterdrukMask |= 0b10000; } //5 Bar=0x01.1111
	 if (!digitalRead(PORT4_DIO)) { tempWaterDruk = 60; waterdrukMask |= 0b100000;} //6 Bar=0x11.1111

	 //Controleer aan de hand van de gegeven druk of de rest van de ingangen hetzelfde aangeven
	 switch(tempWaterDruk) {
			case 0: //0.0 bar
				 if (waterdrukMask == 0b0) tempWaterDruk = 0;	//Uitlezing OK
				 else											tempWaterDruk = -1; //Uitlezing ERROR
				 break;
			case 10: //1.0 bar
				 if (waterdrukMask == 0b1) tempWaterDruk = 10;	//Uitlezing OK
				 else											tempWaterDruk = -1; //Uitlezing ERROR
				 break;
			case 20: //2.0 bar
				 if (waterdrukMask == 0b11) tempWaterDruk = 20;	//Uitlezing OK
				 else											 tempWaterDruk = -1; //Uitlezing ERROR
				 break;
			case 30: //3.0 bar
				 if (waterdrukMask == 0b111) tempWaterDruk = 30;	//Uitlezing OK
				 else												tempWaterDruk = -1; //Uitlezing ERROR
				 break;
			case 40: //4.0 bar
				 if (waterdrukMask == 0b1111) tempWaterDruk = 40;	//Uitlezing OK
				 else												 tempWaterDruk = -1; //Uitlezing ERROR
				 break;
			case 50: //5.0 bar
				 if (waterdrukMask == 0b11111) tempWaterDruk = 50;	//Uitlezing OK
				 else													tempWaterDruk = -1; //Uitlezing ERROR
				 break;
			case 60: //6.0 bar
				 if (waterdrukMask == 0b111111) tempWaterDruk = 60;	//Uitlezing OK
				 else													 tempWaterDruk = -1; //Uitlezing ERROR
				 break;
			default: //Voor het geval dat iets anders mis is
				 tempWaterDruk = -1; //Uitlezing ERROR
				 break;
	 }

	 //if (!digitalRead(PORT1_AIO)) tempWaterDruk = 1; //1 Bar
	 //if (!digitalRead(PORT_IRQ) ) tempWaterDruk = 2; //2 Bar
	 //if (!digitalRead(PORT3_DIO)) tempWaterDruk = 3; //3 Bar
	 //if (!digitalRead(PORT2_DIO)) tempWaterDruk = 4; //4 Bar
	 //if (!digitalRead(PORT1_DIO)) tempWaterDruk = 5; //5 Bar
	 //if (!digitalRead(PORT4_DIO)) tempWaterDruk = 6; //6 Bar
	 return tempWaterDruk;
}

bool getStoringsLed (void) {
	 return (digitalRead(PORT3_AIO) == 0);
}

bool getPompAanLed (void) {
	 return (digitalRead(PORT4_AIO) == 0);
}

void setPompAanRelais (bool pompAan) {
	 if (!pompBlokkeren) {
			pompAanRelais = pompAan;
			digitalWrite(PORT2_AIO, pompAan);

			if (pompAan) {
#if DEBUG
				 Serial.println("Pomp Relais is aan!");
#endif
				 vorigePompAanTijd = pompAanTijd;
				 pompAanTijd = 0;
			} else {
#if DEBUG
				 Serial.println("Pomp Relais is uit!");
#endif
				 vorigePompUitTijd = pompUitTijd;
				 pompUitTijd = 0;
			}
			sendMsg = true;
	 } else {
		 pompAanRelais = false;
		 digitalWrite(PORT2_AIO, false);
#if DEBUG
				 Serial.println("Pomp is door een alarm geblokkeerd!");
#endif
	 }
}

bool getPompAanRelais (void) {
	 return pompAanRelais;
}

#if DEBUG
void showStatus (void) {
	 Serial.print("Waterdruk is nu: ");
	 //Serial.print(gemiddeldeWaterdruk / 10, DEC);
	 Serial.print(waterDruk / 10, DEC);
	 Serial.print(",");
	 //Serial.print(gemiddeldeWaterdruk % 10, DEC);
	 Serial.print(waterDruk % 10, DEC);
	 Serial.print(" bar");
	 if (waterDrukUitlezingOK) Serial.println(" uitlezing OK");
	 else											Serial.println(" uitlezing niet OK");
	 if (getPompAanRelais()) Serial.print("WaterpompRelais is: AAN");
	 else										Serial.print("WaterpompRelais is: UIT");
	 if (pompBlokkeren)			Serial.println(" (en geblokkeerd!)");
	 else										Serial.println(" (en niet geblokkeerd!)");
	 // if (getStoringsLed())		Serial.println("Storingsled is: AAN");
	 // else										Serial.println("Storingsled is: UIT");
	 // if (getPompAanLed())		Serial.println("Pompled is: AAN");
	 // else										Serial.println("Pompled is: UIT");
	 Serial.println("---");
}
#endif

void setup() {
#if DEBUG
	 Serial.begin(57600);
	 Serial.println("\n[Waterpomp regeling]");
#endif

	 rf12_initialize(NODE_ID, RF12_868MHZ, GROUP_ID);

	 pinMode(PORT1_AIO, INPUT); //1Bar
	 pinMode(PORT_IRQ,	INPUT); //2Bar
	 pinMode(PORT1_DIO, INPUT); //3Bar
	 pinMode(PORT2_DIO, INPUT); //4Bar
	 pinMode(PORT3_DIO, INPUT); //5Bar
	 pinMode(PORT4_DIO, INPUT); //6Bar

	 pinMode(PORT2_AIO, OUTPUT); //PompAanRelais
	 pinMode(PORT3_AIO, INPUT);	//StoringsLed
	 pinMode(PORT4_AIO, INPUT);	//PompAanLed

	 //Zet de interrupt routine aan voor de PORT1..4_DIO inputs
	 //bitSet(PCICR,	PCIE2);				//Enable pin change interrupt for Port D (D0..D7)
	 //bitSet(PCMSK2, PORT1_DIO | PORT2_DIO | PORT3_DIO | PORT4_DIO);	 //Choose which pins has to interrupt: D4 -> Port1..4_DIO is PD4..PD7

	 //Init the PORT_IRQ routine on this input
	 //pinMode(PORT_IRQ, INPUT);
	 //attachInterrupt(1, pompAanIRQ, RISING);
	 //bitSet(EICRA, ISC11);
	 //bitClear(EICRA, ISC10);
}

void loop() {
	//
	// Timers bijwerken
	//
	 timerTick = false;
	 if (secondCounter >= 0) {
		 secondCounter = secondCounter - 1;
		 // Serial.print(secondCounter, DEC);
		 if (secondCounter == 0) {
			 secondCounter = 40; //[100ms]
			 timerTick = true;
			 // Serial.print("Tick");
		 }
	 }
	 if (pompAanRelais) {
			if (pompAanTijd < 0xFFFF) pompAanTijd++; //[100ms]
	 } else {
			if (pompUitTijd < 0xFFFF) pompUitTijd++; //[100ms]
	 }

	 //
	 // Waterdruk regelen
	 //
	 waterDruk = getWaterDruk();

	 if (waterDruk != -1) {
			sumWaterDruk += waterDruk;
			metingTeller++;
			gemiddeldeWaterdruk = int(sumWaterDruk / metingTeller);
			gemiddeldeWaterdruk = ((gemiddeldeWaterdruk + 5) / 5) * 5;

			// Serial.print(" Waterdruk: "); Serial.println(waterDruk, DEC);
			// Serial.print(" gemiddeldeWaterdruk: "); Serial.println(gemiddeldeWaterdruk, DEC);

			if (waterDrukStabiel != waterDruk) {
				 waterDrukStabiel = waterDruk;
				 waterDrukUitlezingOK = false;
			} else {
				 waterDrukUitlezingOK = true;
			}
	 } else {
			waterDrukStabiel = -1;
			waterDrukUitlezingOK = false;
	 }

	 //Regel de druk als er geen waterDruk ERROR is (uitlezingsfout)
	 if (waterDrukUitlezingOK && !pompBlokkeren) {
			// Serial.print("waterDrukUitlezingOK!");
			//Druk regelen
			if (waterDruk <= waterDrukAan) pompAanRelais = true;
			if (waterDruk >= waterDrukUit) pompAanRelais = false;

			if (oldPompAanRelais != pompAanRelais) {
				 oldPompAanRelais = pompAanRelais;
				 setPompAanRelais(pompAanRelais);
			}
	 }

	 if (timerTick) {
		 sumWaterDruk = 0;
		 metingTeller = 0;
		 if (oldGemiddeldeWaterdruk != gemiddeldeWaterdruk) {
			 oldGemiddeldeWaterdruk = gemiddeldeWaterdruk;
			 sendMsg = true;
		 }
	 }


	 //
	 // Beveiligingen
	 //
	 if (!pompBlokkeren) {
			//2-Waterpomp te lang actief (>20 min)
			if (getPompAanRelais() && (pompAanTijd > 12000)) {
#if DEBUG
				 Serial.println("Alarm: Waterpomp te lang actief (>20 min)");
#endif
				 setPompAanRelais(false);
				 pompBlokkeren = true;
				 alarmMelding = 2; //2=Waterpomp te lang actief (met pompAanTijd)
				 sendMsg = true;
			}

			if (getPompAanRelais() && (pompAanTijd > 300) && !getPompAanLed()) {
#if DEBUG
				 Serial.println("Alarm: Originele regeling zet de pomp na verloop uit, dan is er iets mis");
			//3-Originele regeling zet de pomp na verloop uit, dan is er iets mis
#endif
				 setPompAanRelais(false);
				 pompBlokkeren = true;
				 alarmMelding = ALARM_ORG_POMP_REGELING_ZET_POMP_UIT; //3=Originele regeling zet de pomp na verloop uit
				 sendMsg = true;
			}

			//5-Pomp blokkeren als originele waterpomp regeling niet aan staat, alle leds zijn dan uit,
			// er wordt dus schijnbaar 0 bar gemeten en de het pompAanRelais blijft constant aan
			if ((getWaterDruk() == 0) and !getStoringsLed() and !getPompAanLed()) {
#if DEBUG
				 Serial.println("Alarm: Pomp blokkeren als originele waterpomp regeling niet aan staat");
#endif
				 setPompAanRelais(false);
				 pompBlokkeren = true;
				 alarmMelding = ALARM_ORG_REGELING_IS_UITGESCHAKELD; //5=Pomp blokkeren als originele waterpomp regeling niet aan staat
				 sendMsg = true;
			}

			//6-Waterpomp droogloop beveiliging, wel testen of er 1 van de leds actief is
			if ((alarmMelding != ALARM_ORG_REGELING_IS_UITGESCHAKELD) && (waterDruk == 0) && (pompAanTijd > 200)) {
#if DEBUG
				 Serial.println("Alarm: Waterpomp droogloop beveiliging (> 20 sec en < 1bar)");
#endif
				 setPompAanRelais(false);
				 pompBlokkeren = true;
				 alarmMelding = ALARM_DROOGLOOP_BEVEILIGING; //6=Waterpomp droogloop beveiliging
				 sendMsg = true;
			}
	 } else {
			if (alarmMelding == ALARM_ORG_REGELING_IS_UITGESCHAKELD) { //5=Test of originele waterpomp regeling weer aan gaat, bij aanschakelen gaan alle leds even aan
				 if ((getWaterDruk() != 0) or getStoringsLed() or getPompAanLed()) {
#if DEBUG
						Serial.println("Alarm reset: Originele pomp regeling is weer aan gezet");
#endif
				 pompBlokkeren = false;
				 alarmMelding = 0; //5=Pomp blokkeren als originele waterpomp regeling niet aan staat
				 sendMsg = true;
				 }

			}
	 }

	 if (waarschuwingsMelding == 0) {
			//2-Druk is te laag geworden (> 5 sec), maar alleen testen als de originele waterpomp regeling aan
			//	staat, dus als de drukuitlezingsleds actief zijn
			if ((alarmMelding != ALARM_ORG_REGELING_IS_UITGESCHAKELD) && (waterDruk == 0) && (pompAanTijd > 50)) {
#if DEBUG
				 Serial.println("Waarschuwing: Waterdruk is te laag (< 1bar)");
#endif
				 waarschuwingsMelding = WAARSCHUWING_WATERDRUK_TE_LAAG; //2=Waterdruk te laag (< 1bar)
				 sendMsg = true;
			}
	 } else {
			//Automatische reset van waarschuwing als waterdruk te laag was
			if (waarschuwingsMelding == WAARSCHUWING_WATERDRUK_TE_LAAG) {
				 if ((alarmMelding != ALARM_ORG_REGELING_IS_UITGESCHAKELD) && (waterDruk > 0)) {
						waarschuwingsMelding = 0;
						sendMsg = true;
				 }
			}
	 }

	 //
	 // Commando bericht ontvangen?
	 //
	 if (rf12_recvDone() && rf12_crc == 0) {
			if ((rf12_hdr & 0x1F) == NODE_ID) {
#if DEBUG
				 Serial.print(" -> OK ");
				 Serial.print((int)rf12_hdr & 0x1F, DEC);
				 for (byte i = 0; i < rf12_len; ++i) {
						Serial.print(" ");
						Serial.print((int)rf12_data[i], DEC);
				 }
				 Serial.println();
#endif
				 //Ontvangen bericht uitlezen
				 rxCommand = rf12_data[0]; //Zet debug messages aan: 0=Uit 1=Start/Stop 2=Start/Stop+Status leds
				 switch(rxCommand) {
						case 0: // Reset debugMsg
#if DEBUG
							 Serial.println("DebugMsg: UIT");
#endif
							 sendMsg = true;
							 debugMsg = false;
							 break;
						case 1: // Activeer debugMsg's
#if DEBUG
							 Serial.println("DebugMsg: AAN (pomp aan/uit status msg's)");
#endif
							 debugMsg = true;
							 sendMsg = true;
							 break;
						case 2: // Reset pompBlokkeren/waarschuwing
#if DEBUG
							 Serial.println("Reset alarm/pompBlokkeren/waarschuwing");
#endif
							 pompBlokkeren = false;
							 pompAanRelais = false; oldPompAanRelais = false;
							 alarmMelding = 0;
							 waarschuwingsMelding = 0;
							 pompAanTijd = 0; vorigePompAanTijd = 0;
							 pompUitTijd = 0; vorigePompUitTijd = 0;
							 sendMsg = true;
							 break;
//						case 3: // Reset waarschuwing
//#if DEBUG
//							 Serial.println("Reset waarschuwing");
//#endif
//							 waarschuwingsMelding = 0;
//							 sendMsg = true;
//							 break;
						case 4: // Pomp waterdruk schakelpunten wijzigen
							 if ((rf12_data[1] > 0) && (rf12_data[1] <= 60)) waterDrukAan = rf12_data[1];
							 if ((rf12_data[2] > 0) && (rf12_data[2] <= 60)) waterDrukUit = rf12_data[2];
#if DEBUG
							 Serial.print("Pomp waterdruk schakelpunten wijzigen, waterDrukAan: ");
							 Serial.print(waterDrukAan, DEC);
							 Serial.print(" bar, waterDrukUit: ");
							 Serial.print(waterDrukUit, DEC);
							 Serial.println(" bar");
#endif
							 sendMsg = true;
							 break;
						case 9: // Blokkeer de pomp handmatig en zet op alarm
#if DEBUG
							 Serial.println("Blokkeer pomp handmatig: AAN");
#endif
							 setPompAanRelais(false);
							 pompAanRelais = false;
							 pompBlokkeren = true;
							 alarmMelding = ALARM_DROOGLOOP_BEVEILIGING; //6=Pomp handmatig (remote) geblokkeerd
							 sendMsg = true;
							 break;
						case 10: // Vraag waterpomp regeling informatie op
										 //door een bericht te verzenden (onafhankelijk van debugMsg)
#if DEBUG
							 Serial.println("Opvragen info");
#endif
							 sendMsg = true;
							 break;
				 }
			}
	 }

	 //
	 // Bericht verzenden?
	 //
	 if (sendMsg && rf12_canSend()) {
			sendMsg = false;

#if DEBUG
			showStatus();
#endif
			if (pompAanRelais) {
				 if (pompAanTijd == 0) payload.pompAanTijd = vorigePompAanTijd; //In geval dat pomp net AAN geschakeld is, pompTijdAan is anders 0
				 else									payload.pompAanTijd = pompAanTijd;
				 payload.pompUitTijd = pompUitTijd;
			} else {
				 payload.pompAanTijd = pompAanTijd;
				 if (pompUitTijd == 0) payload.pompUitTijd = vorigePompUitTijd; //In geval dat pomp net UIT geschakeld is, pompTijdUit is anders 0
				 else									payload.pompUitTijd = pompUitTijd;
			}
			payload.pompAanRelais = pompAanRelais;
			payload.waterDruk = gemiddeldeWaterdruk;
			payload.waterDrukAan = waterDrukAan;
			payload.waterDrukUit = waterDrukUit;
			payload.pompLed = getPompAanLed();
			payload.storingsLed = getStoringsLed();
			payload.pompBlokkeren = pompBlokkeren;
			payload.waarschuwingsMelding = waarschuwingsMelding;
			payload.waterdrukMask = waterdrukMask;
			payload.alarmMelding = alarmMelding;
			payload.waterDrukUitlezingOK = waterDrukUitlezingOK;
			rf12_sendStart(0, &payload, sizeof payload);
	 }
	 delay(100); //100ms
}

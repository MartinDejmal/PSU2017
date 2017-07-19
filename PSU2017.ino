#include <U8glib.h>
#include <Wire.h>
#include <INA219.h>

// Debug režim - vypisuje hlášky na sériový port
#define debug 1
#define BlankScreenTimeout 990000 // prodleva v ms pred usnutim displeje
#define N_Decimals 3
// -----------------------------------------------------
// (c) 2017 Martin Dejmal
// Sketch pro Arduino sloužící na monitoring a ovládání 
// modulového  laboratorního  zdroje  řízeného  modulem 
// WD2002SJ.
//
//
// -----------------------------------------------------

INA219 INA;

// 2.42" OLED SPI
U8GLIB_SSD1306_128X64 u8g(13, 11, 10, 9, 8);  // SW SPI Com: SCK = 13, MOSI = 11, CS = 10, A0 = 9, RESET = 8

#define NUM_SAMPLES 40  // pocet vzorku pri mereni napeti
#define volt_ref 4.096 // referencni napeti AD prevodniku
#define divider_ratio 6.039 // delici pomer delice na mereni napeti

#define AD5242_1M_Addr 0x2C
#define RDAC1 0x00
#define RDAC2 0x80
#define VoltStep 0.09
bool sv = true;
byte CurrRes = 0;

//rotary encoder
#define EncBtn 3
#define EncPinA 2
#define EncPinB 4

volatile int EncPos = 0;
int EncPosPrev = 0;
volatile byte BtnState = 0;

// konfigurační režim - "SET" mode (rotační enkodér nastavuje hodnotu)
// pokud je 0, přepíná se mezi veličinami, které se budou po stisku tlačítka nastavovat.
boolean ConfigMode = true;

// promenne pouzivane pri mereni napeti
int sum = 0;                          // suma všech přečtených vzorků 
unsigned char sample_count = 0;       // počet vzorků při měření a průměrování napětí

// Proměnné pro aktuální stav obou kanálů zdroje
// Kanál 1 - promenne napětí
double Ch1VoltageAct = 0.0;           // aktuální napětí - kanál 1
double Ch1CurrentAct = 0.0;           // aktuální proud - kanál 1
double Ch1PowerAct = 0.0;             // vypočtený výkon - kanál 1

// Kanál 2 - pevne napětí
double Ch2VoltageAct = 0.0;           // aktuální napětí - kanál 2
double Ch2CurrentAct = 0.0;           // aktuální proud - kanál 2
double Ch2PowerAct = 0.0; // vypočtený výkon - kanál 2

/*
byte ADVoltValue = 0x0;
byte ADCurrValue = 0x0;
*/

// vystupni datova veta
String dataWord = "";
// pamatovacek na hodnotu millis
long curMillis = 0;                   // pro export na seriovku
long lastMillis = 0;                  // pro blank screen timeout

// parametry nastaveni zdroje 
// konstanty
// kanal 1
#define Ch1VoltageMin 0.8
#define Ch1VoltageMax 5.1
#define Ch1CurrentMin 0.1
#define Ch1CurrentMax 3.2

//kanal 2
#define Ch2VoltageMin 11.5
#define Ch2VoltageMax 12.5
#define Ch2CurrentMin 0.1
#define Ch2CurrentMax 3.2

// aby nepreteklo mackani tlacitka
#define BtnStateMin 0
#define BtnStateMax 1

// limity hodnot na AD5242
#define ResistanceMin 0x00
#define ResistanceMax 0xFF


// vychozi parametry nastaveni hodnot na jednotlivych kanalech
// kanal 1
double Ch1VoltageSet = 3.3;
double Ch1CurrentSet = 0.5;

// kanal 2 - stejne nema regulaci...
double Ch2VoltageSet = 12;
double Ch2CurrentSet = 0.5;

// a jedem ... setup
void setup(void) {
  Serial.begin(115200);             // at to poradne lita
  INA.begin();                      // INA219 meri proud
  Wire.begin();                     // I2C Bus Master
  
  //analogova reference 4.096V
  analogReference(EXTERNAL);
  pinMode(A0, INPUT);                // mereni napeti - delic

  // rotary encoder
  pinMode(EncPinA, INPUT); 
  digitalWrite(EncPinA, HIGH);       // turn on pull-up resistor
  pinMode(EncPinB, INPUT); 
  digitalWrite(EncPinB, HIGH);       // turn on pull-up resistor
  pinMode(EncBtn, INPUT);
  digitalWrite(EncBtn, HIGH);

  // Start I2C transmission
  Wire.beginTransmission(AD5242_1M_Addr);
  Wire.write(RDAC1);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(300);
  Wire.beginTransmission(AD5242_1M_Addr);
  Wire.write(RDAC2);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(300);
 
  
  // IRQ !
  attachInterrupt(0, HandleEncoder, CHANGE);  // Preruseni pro rotacni enkoder - zmena hodnoty (libovolna)
  attachInterrupt(1, HandleBtn, FALLING);  // Preruseni pro tlacitko enkoderu - sestupna hrana
}

void loop(void) { 

  if (sv == true) CurrRes = FineTuneVoltage();

  // Měření aktuálních hodnot
  // kanál 1
  Ch1CurrentAct = (INA.shuntCurrent()); 
  Ch1VoltageAct = MeasureVoltage();
  Ch1PowerAct   = Ch1CurrentAct*Ch1VoltageAct;

  // kanál 2
  Ch2VoltageAct = (INA.shuntVoltage())*divider_ratio;


  dataWord = "";
  curMillis = millis();
  
  Serial.print("<");
  Serial.print(Ch1VoltageAct,6);
  Serial.print(";");
  Serial.print(Ch1CurrentAct,6);
  Serial.print(";");
  Serial.print(Ch2VoltageAct,6);
  Serial.print(";");
  Serial.print(Ch2CurrentAct,6);
  Serial.print(";");
  Serial.print(curMillis);
//  dataWord += ";";
//  dataWord += (Ch1VoltageAct ^ Ch1CurrentAct ^ Ch2VoltageAct ^ Ch2CurrentAct ^ curMillis);
  Serial.print(">");
  
  if (debug == 0) {
    Serial.println(dataWord);
  }
  
  if (debug == 1) {
    Serial.print("Proud: ");
    Serial.print(Ch2CurrentAct);
    Serial.print(" Napeti: ");
    Serial.print(Ch2VoltageAct);
    Serial.print(" Encoder: ");
    Serial.print(EncPos);
    Serial.print(" Button: ");
    Serial.print(BtnState);
    Serial.print(" CfgMode: ");
    Serial.println(ConfigMode);
  }
  
if (millis()>lastMillis+BlankScreenTimeout)
  {
    u8g.firstPage();  
    do {
    } while( u8g.nextPage() );
  } 
  else
  { u8g.firstPage();  
    do {
      StatusPage2();
    } while( u8g.nextPage() );
  }
}


void HandleBtn() {
  ConfigMode = not ConfigMode;
  if (debug == 1) {
    Serial.print("ConfigMode Changed: ");
    Serial.println(ConfigMode);
  }
  sv = true;  
}

void HandleEncoder() {
  lastMillis = millis(); // zapamatovat si cas, kdy byl naposled pouzit enkoder
  
  EncPosPrev = EncPos;
  if (digitalRead(EncPinA) == HIGH) {   // found a low-to-high on channel A
    if (digitalRead(EncPinB) == LOW) {  // check channel B to see which way encoder is turning
      EncPos = EncPos - 1;         // CCW
    } 
    else {
      EncPos = EncPos + 1;         // CW
    }
  }
  else                                        // found a high-to-low on channel A
  { 
    if (digitalRead(EncPinB) == LOW) {     // check channel B to see which way encoder is turning  
      EncPos = EncPos + 1;          // CW
    } 
    else {
      EncPos = EncPos - 1;          // CCW
    }
  }

  if (ConfigMode == 0) {
    switch (BtnState) {
    case 0:  {  //napeti
      Ch1VoltageSet = Ch1VoltageSet+(0.1*(EncPos-EncPosPrev)); 
      }
      break;
    case 1: { //proud
      Ch1CurrentSet = Ch1CurrentSet+(0.1*(EncPos-EncPosPrev));
      }
      break;
    }
  }
  else {
    BtnState = BtnState + (EncPos-EncPosPrev);
  }
  
  if (debug == 1) {
    Serial.print("UpdatingValue: curr=");
    Serial.print(EncPos);
    Serial.print(" prev=");
    Serial.print(EncPosPrev);
    Serial.print(" diff=");
    Serial.println(EncPos-EncPosPrev);
  }
  HandleLimits();
}


void HandleLimits() {
  if (Ch2VoltageSet>Ch2VoltageMax) {
      Ch2VoltageSet = Ch2VoltageMax;
      if (debug == 1) Serial.println("Ch2VoltageMax reached!");
  }
  if (Ch2VoltageSet<Ch2VoltageMin) {
      Ch2VoltageSet = Ch2VoltageMin;
      if (debug == 1) Serial.println("Ch2VoltageMin reached!");
  }
  if (Ch2CurrentSet>Ch2CurrentMax) {
      Ch2CurrentSet = Ch2CurrentMax;
      if (debug == 1) Serial.println("Ch2CurrentMax reached!");
  }
  if (Ch2CurrentSet<Ch2CurrentMin) {
      Ch2CurrentSet = Ch2CurrentMin;
      if (debug == 1) Serial.println("Ch2CurrentMin reached!");
  }
  if (Ch1VoltageSet>Ch1VoltageMax) {
      Ch1VoltageSet = Ch1VoltageMax;
      if (debug == 1) Serial.println("Ch1VoltageMax reached!");
  }
  if (Ch1VoltageSet<Ch1VoltageMin) {
      Ch1VoltageSet = Ch1VoltageMin;
      if (debug == 1) Serial.println("Ch1VoltageMin reached!");
  }
  if (Ch1CurrentSet>Ch1CurrentMax) {
      Ch1CurrentSet = Ch1CurrentMax;
      if (debug == 1) Serial.println("Ch1CurrentMax reached!");
  }
  if (Ch1CurrentSet<Ch1CurrentMin) {
      Ch1CurrentSet = Ch1CurrentMin;
      if (debug == 1) Serial.println("Ch1CurrentMin reached!");
  }
  if (BtnState < BtnStateMin) {
      BtnState = BtnStateMin;
      if (debug == 1) Serial.println("BtnStateMin reached!");
  }
  if (BtnState > BtnStateMax) {
      BtnState = BtnStateMax;
      if (debug == 1) Serial.println("BtnStateMax reached!");
  }
  if (BtnState > BtnStateMax) {
      BtnState = BtnStateMax;
      if (debug == 1) Serial.println("BtnStateMax reached!");
  }

  
}


void StatusPage2(void) {
  u8g.drawLine(4,1,61,1); // horni vodorovna leva
  u8g.drawLine(67,1,124,1); // horni vodorovna prava
  u8g.drawLine(3,63,61,63); // spodni vodorovna leva
  u8g.drawLine(67,63,124,63); // spodni vodorovna prava
  u8g.drawLine(0,5,0,60); // leva svisla
  u8g.drawLine(127,5,127,60); // prava svisla
  u8g.drawLine(0,4,3,1); // levy horni roh
  u8g.drawLine(64,4,67,1); // levy horni prostredni roh
  u8g.drawLine(0,60,3,63); // levy dolni roh
  u8g.drawLine(64,60,67,63); // levy dolni prostredni roh
  u8g.drawLine(124,63,127,60); // pravy dolni roh
  u8g.drawLine(61,63,64,60); // pravy dolni prostredni roh
  u8g.drawLine(124,1,128,5); // pravy horni roh
  u8g.drawLine(61,1,64,4); // pravy horni prostredni roh
  u8g.drawLine(1,15,126,15); // horni vodorovna oddelovaci
  u8g.drawLine(64,4,64,60); // stredni svisla oddelovaci
  u8g.drawLine(1,27,126,27); // dalsi vodorovna oddelovaci pod Voltage/Ch2CurrentSet
  
//  u8g.setFont(u8g_font_micro);
//  u8g.setPrintPos(85, 8); 
//  u8g.print("VARIABLE");
//  u8g.setPrintPos(85, 14); 
//  u8g.print("VOLTAGE");

  u8g.setFont(u8g_font_6x13);
  u8g.setPrintPos(5, 13); 
  u8g.print("CHANNEL 1");

  u8g.setPrintPos(70, 13); 
  u8g.print("CHANNEL 2");

  // Aktuální hodnoty
  // Kanál 1
  // Napětí
  u8g.setFont(u8g_font_profont15);
  if (Ch1VoltageAct<10 && Ch1VoltageAct>=0) u8g.setPrintPos(15, 39);
    else u8g.setPrintPos(8, 39);
  u8g.print(Ch1VoltageAct,N_Decimals);
  u8g.setPrintPos(54, 39); 
  u8g.print("V");

  // Proud
  if (Ch1CurrentAct<10 && Ch1CurrentAct>=0) u8g.setPrintPos(15, 50);
    else u8g.setPrintPos(8, 50);
  u8g.print(Ch1CurrentAct,N_Decimals);
  u8g.setPrintPos(54, 50); 
  u8g.print("A");

  // Výkon
  if (Ch1PowerAct<10 && Ch1PowerAct>=0) u8g.setPrintPos(15, 61);
    else u8g.setPrintPos(8, 61);
  u8g.print(Ch1PowerAct,N_Decimals);
  u8g.setPrintPos(54, 61); 
  u8g.print("W");
  
  // Kanál 2
  // Napětí
  if (Ch2VoltageAct<10 && Ch2VoltageAct>=0) u8g.setPrintPos(78, 39);
    else u8g.setPrintPos(71, 39);
  u8g.print(Ch2VoltageAct,N_Decimals);
  u8g.setPrintPos(117, 39); 
  u8g.print("V");

  // Proud
  if (Ch2CurrentAct<10 && Ch2CurrentAct>=0) u8g.setPrintPos(78, 50);
    else u8g.setPrintPos(71, 50);
  u8g.print(Ch2CurrentAct,N_Decimals);
  u8g.setPrintPos(117, 50); 
  u8g.print("A");

  // Výkon
  if (Ch2PowerAct<10 && Ch2PowerAct>=0) u8g.setPrintPos(78, 61);
    else u8g.setPrintPos(71, 61); 
  u8g.print(Ch2PowerAct,N_Decimals);
  u8g.setPrintPos(117, 61); 
  u8g.print("W");
  
  // Nastavené hodnoty
  // Napětí
  u8g.setFont(u8g_font_profont11);
  if (Ch1VoltageSet<10 && Ch1VoltageSet>0) u8g.setPrintPos(10, 24); 
    else u8g.setPrintPos(4, 24); 
  u8g.print(Ch1VoltageSet,1);
  if (BtnState == 0) u8g.drawLine(3,25,33,25);
  u8g.setPrintPos(28, 24); 
  u8g.print("V");
  // Indikace mezní hodnoty
  if (Ch1VoltageSet == Ch1VoltageMin) u8g.drawPixel(3,24);
  if (Ch1VoltageSet == Ch1VoltageMax) u8g.drawPixel(33,24);
  
  // Proud - jednociferná celá část, není třeba ošetřit posun
  u8g.setPrintPos(38, 24); 
  u8g.print(Ch1CurrentSet,1);
  if (BtnState == 1) u8g.drawLine(37,25,62,25);
  u8g.setPrintPos(57, 24); 
  u8g.print("A");
  // Indikace mezní hodnoty
  if (Ch1CurrentSet == Ch1CurrentMin) u8g.drawPixel(37,24);
  if (Ch1CurrentSet == Ch1CurrentMax) u8g.drawPixel(62,24);

  // Indikace nastavené hodnoty na grafu (na lince pod vybranou hodnotou)
  u8g.setColorIndex(0);
  u8g.drawPixel(3+(30*((Ch1VoltageSet-Ch1VoltageMin)/(Ch1VoltageMax-Ch1VoltageMin))),25);
  u8g.drawPixel(37+(25*((Ch1CurrentSet-Ch1CurrentMin)/(Ch1CurrentMax-Ch1CurrentMin))),25);
  u8g.setColorIndex(1);

  // Indikace režimu volby
  u8g.setPrintPos(66, 25); 
  if (ConfigMode == 0) u8g.print("SET");
   else u8g.print("SEL");

  u8g.setPrintPos(110, 25);
  u8g.print(CurrRes);
   
   
}

float MeasureVoltage(){
  //mereni napeti - Reference
  sample_count = 0;
  sum = 0;
  while (sample_count < NUM_SAMPLES) {
        sum += analogRead(A1);
        sample_count++;
        delay(5);
    }
  return(((float)sum / (float)NUM_SAMPLES * volt_ref) / 1024.0 * divider_ratio * 1.06);
}

void SetResistance(byte addr, bool rdac, byte res){
  if (debug == 1) Serial.print("Setting resistance at address ");
  if (debug == 1) Serial.print(addr);
  if (debug == 1) Serial.print(" on RDAC ");
  if (debug == 1) Serial.print(rdac);
  if (debug == 1) Serial.print(" with step value ");
  if (debug == 1) Serial.println(res);

  if (rdac == 1) {
    Wire.beginTransmission(addr);
    Wire.write(0x80);      // Command byte - 0x80 nastavuje hodnotu na RDAC2, 0x00 na RDAC1 
    Wire.write(res);                    // Data byte - 0-255, hodnota rezistoru
    Wire.endTransmission();
  }
  else {  
    Wire.beginTransmission(addr);
    Wire.write(0x00);      // Command byte - 0x80 nastavuje hodnotu na RDAC2, 0x00 na RDAC1 
    Wire.write(res);                    // Data byte - 0-255, hodnota rezistoru
    Wire.endTransmission();
  }
  delay(150);
  if (debug == 1) Serial.println("Setting resistance finished.");
}

void SetVoltage(){
  if (debug == 1) Serial.print("Setting voltage with step: ");
  //byte Step = (Ch1VoltageSet-Ch1VoltageMin)/VoltStep;
  byte Step = Ch1VoltageSet/0.16;
  if (debug == 1) Serial.println(Step);
  SetResistance(AD5242_1M_Addr, 0, Step);
  SetResistance(AD5242_1M_Addr, 1, Step);
  sv = false;
  
}

byte FineTuneVoltage(){
  byte Step = (Ch1VoltageSet-Ch1VoltageMin)/VoltStep;
  
  SetVoltage();
  while (Ch1VoltageAct <= Ch1VoltageSet) {
        SetResistance(AD5242_1M_Addr, 0, Step);
        SetResistance(AD5242_1M_Addr, 1, Step);        
        Step++;
        Ch1VoltageAct = MeasureVoltage();
    }
      
  while (Ch1VoltageAct >= Ch1VoltageSet) {
        SetResistance(AD5242_1M_Addr, 0, Step);
        SetResistance(AD5242_1M_Addr, 1, Step);        
        Step--;
        Ch1VoltageAct = MeasureVoltage();
    }
  return Step;
}


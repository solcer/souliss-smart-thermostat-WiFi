/**************************************************************************
   Souliss - Web Configuration

    This example demonstrate a complete web configuration of ESP8266 based
	nodes, the node starts as access point and allow though a web interface
	the configuration of IP and Souliss parameters.

	This example is only supported on ESP8266.
***************************************************************************/
#define HOST_NAME_INSKETCH
#define HOST_NAME "Souliss-Termostato-Piano-Terra"


#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <DHT.h>



// Configure the Souliss framework
#include "bconf/MCU_ESP8266.h"              // Load the code directly on the ESP8266
#include "conf/RuntimeGateway.h"            // This node is a Peer and can became a Gateway at runtime
#include "conf/DynamicAddressing.h"         // Use dynamically assigned addresses
#include "conf/WEBCONFinterface.h"          // Enable the WebConfig interface

#include "Souliss.h"

#include "encoder.h"
#include "constants.h"
#include "display.h"
#include "display2.h"
#include "language.h"
#include "ntp.h"
#include <Time.h>
#include <MenuSystem.h>
#include "menu.h"
#include "crono.h"
#include "read_save.h"

//*************************************************************************
//*************************************************************************

DHT dht(DHTPIN, DHTTYPE);
float temperature = 0;
float humidity = 0;
float setpoint = 0;
float encoderValue_prec = 0;

//DISPLAY
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <SPI.h>
#include <Arduino.h>
#include "Ucglib.h"


int backLEDvalue = 0;
int backLEDvalueHIGH = BRIGHT_MAX;
int backLEDvalueLOW = BRIGHT_MIN_DEFAULT;
bool FADE = 1;


// Menu
MenuSystem* myMenu;

// Use hardware SPI
Ucglib_ILI9341_18x240x320_HWSPI ucg(/*cd=*/ 2 , /*cs=*/ 15);

// Setup the libraries for Over The Air Update
OTA_Setup();

void setup()
{
  SERIAL_OUT.begin(115200);

  //DISPLAY INIT
  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  ucg.begin(UCG_FONT_MODE_SOLID);
  ucg.setColor(0, 0, 0);
  ucg.setRotate90();

  //SPI Frequency
  SPI.setFrequency(100000000);
  
  //BACK LED
  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  digitalWrite(BACKLED, HIGH);
  pinMode(BACKLED, OUTPUT);                     // Background Display LED

  display_print_splash_screen(ucg);
  Initialize();

  // Read the IP configuration from the EEPROM, if not available start
  // the node as access point
  if (!ReadIPConfiguration())
  {
    // Start the node as access point with a configuration WebServer
    SetAccessPoint();
    startWebServer();
    SERIAL_OUT.println("display_print_splash_waiting_need_configuration");
    display_print_splash_waiting_need_configuration(ucg);
    // We have nothing more than the WebServer for the configuration
    // to run, once configured the node will quit this.
    while (1)
    {
      yield();
      runWebServer();
    }

  }

  if (IsRuntimeGateway())
  {
    SERIAL_OUT.println("display_print_splash_waiting_connection_gateway");
    display_print_splash_waiting_connection_gateway(ucg);
    // Connect to the WiFi network and get an address from DHCP
    SetAsGateway(myvNet_dhcp);       // Set this node as gateway for SoulissApp
    SetAddressingServer();
  }
  else
  {
    SERIAL_OUT.println("display_print_splash_waiting_connection_peer");
    display_print_splash_waiting_connection_peer(ucg);
    // This board request an address to the gateway at runtime, no need
    // to configure any parameter here.
    //SetDynamicAddressing();
    //GetAddress();
    
    SERIAL_OUT.println("Address received");
  }

  //*************************************************************************
  //*************************************************************************
  Set_T52(SLOT_TEMPERATURE);
  Set_T53(SLOT_HUMIDITY);
  Set_T19(SLOT_BRIGHT_DISPLAY);
  Set_T11(SLOT_AWAY);

  //set default mode
  Set_Thermostat(SLOT_THERMOSTAT);
  set_ThermostatMode(SLOT_THERMOSTAT);
  set_DisplayMinBright(SLOT_BRIGHT_DISPLAY, BRIGHT_MIN_DEFAULT);

  // Define output pins
  pinMode(RELE, OUTPUT);    // Heater
  dht.begin();

  //ENCODER
  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  pinMode (ENCODER_PIN_A, INPUT_PULLUP);
  pinMode (ENCODER_PIN_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), encoderFunction, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), encoderFunction, CHANGE);
  // SWITCH ENCODER
  digitalWrite(BACKLED, HIGH);
  pinMode(ENCODER_SWITCH, INPUT);


  // EEPROM 
  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  Store_Init();
  ReadAllSettingsFromEEPROM();
  ReadCronoMatrix();

  //NTP
  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  initNTP();
  delay(1000);
  //*************************************************************************
  //*************************************************************************

  //MENU
  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  initMenu();
  myMenu = getMenu();

  // Init the OTA
  OTA_Init();

  // Init HomeScreen
  initScreen();


 
}

void loop()
{
  EXECUTEFAST() {
    UPDATEFAST();

    FAST_50ms() {
      //set point attuale
      setpoint = Souliss_SinglePrecisionFloating(memory_map + MaCaco_OUT_s + SLOT_THERMOSTAT + 3);
      //Stampa il setpoint solo se il valore dell'encoder è diverso da quello impostato nel T31
      if (!getEnabled()) {
        if (arrotonda(getEncoderValue()) != arrotonda(encoderValue_prec)) {
          FADE = 1;
          //TICK TIMER
          timerDisplay_setpoint_Tick();
          //SETPOINT PAGE ////////////////////////////////////////////////////////////////

          if (getLayout1()) {
            SERIAL_OUT.println("display_setpointPage - layout 1");
            display_layout1_background(ucg, arrotonda(getEncoderValue()) - arrotonda(setpoint));
            display_layout1_setpointPage(ucg, getEncoderValue(), Souliss_SinglePrecisionFloating(memory_map + MaCaco_OUT_s + SLOT_THERMOSTAT + 1), humidity, getSystemState() );
          }
          else if (getLayout2()) {
            SERIAL_OUT.println("display_setpointPage - layout 2");
            display_layout2_Setpoint(ucg, getEncoderValue());
            
          }
        }

        encoderValue_prec = getEncoderValue();
      } else {
        //Bright high if menu enabled
        FADE = 1;
        //Menu Command Section
        if (getEncoderValue() != encoderValue_prec)
        {
          if (getEncoderValue() > encoderValue_prec) {
            //Menu DOWN
            myMenu->next();
          } else {
            //Menu UP
            myMenu->prev();
          }
          printMenu(ucg);
          encoderValue_prec = getEncoderValue();
        }
      }

      Logic_T19(SLOT_BRIGHT_DISPLAY);
      Logic_T11(SLOT_AWAY);
    }

    FAST_110ms() {
      if (!getEnabled()) {
        if (timerDisplay_setpoint()) {
          //timeout scaduto
          display_layout1_background_black(ucg);
          setEncoderValue(setpoint);
        } else {
          //timer non scaduto. Memorizzo
          setpoint = getEncoderValue();
          //memorizza il setpoint nel T31
          setSetpoint(setpoint);
          
          // Trig the next change of the state
          data_changed = Souliss_TRIGGED;
        }
      }

      //SWITCH ENCODER
      if (!digitalRead(ENCODER_SWITCH)) {
        if (!getEnabled()) {
          //IF MENU NOT ENABLED
          setEnabled(true);
          //il flag viene impostato a true, così quando si esce dal menu la homescreen viene aggiornata ed il flag riportato a false
          setFlag_initScreen(true);
          ucg.clearScreen();
        } else {
          //IF MENU ENABLED
          myMenu->select(true);
          yield();
          /// CRONO 
          if (getProgCrono()) { 
          byte menu;
            ucg.clearScreen();
            drawCrono(ucg); 
            menu=1;
                  while(menu==1){
                    setDay(ucg);
                    drawBoxes(ucg);
                    setBoxes(ucg);
                    //delay(2000);
                    if(exitmainmenu())
                    {menu=0; }
                  }
          }
        }
        ucg.clearScreen();
        on_item_ProgCrono_deselected();
        printMenu(ucg);
      }

      //FADE
      if (FADE == 0) {
        //Raggiunge il livello di luminosità minima, che può essere variata anche da SoulissApp
        if ( backLEDvalue != backLEDvalueLOW) {
          if ( backLEDvalue > backLEDvalueLOW) {
            backLEDvalue -= BRIGHT_STEP_FADE_OUT;
          } else {
            backLEDvalue += BRIGHT_STEP_FADE_OUT;
          }
          bright(backLEDvalue);
        }
      } else  if (FADE == 1 && backLEDvalue < backLEDvalueHIGH) {
        backLEDvalue +=  BRIGHT_STEP_FADE_IN;
        bright(backLEDvalue);
      }

    //CRONO
     
      
    }

    FAST_210ms() {   // We process the logic and relevant input and output
      //*************************************************************************
      //*************************************************************************
      Logic_Thermostat(SLOT_THERMOSTAT);
      // Start the heater and the fans
      nDigOut(RELE, Souliss_T3n_HeatingOn, SLOT_THERMOSTAT);    // Heater

      //*************************************************************************
      //*************************************************************************
    }

    FAST_510ms() {
      // Compare the acquired input with the stored one, send the new value to the
      // user interface if the difference is greater than the deadband
      Logic_T52(SLOT_TEMPERATURE);
      Logic_T53(SLOT_HUMIDITY);
      
    }


    FAST_710ms() {
      //HOMESCREEN ////////////////////////////////////////////////////////////////
      ///update homescreen only if menu exit
      if (!getEnabled() && getFlag_initScreen()) {
        SERIAL_OUT.println("Init Screen");
        initScreen();
        setFlag_initScreen(false);
        
        //EXIT MENU - Actions
        //write min bright on T19
        memory_map[MaCaco_OUT_s + SLOT_BRIGHT_DISPLAY + 1] = getDisplayBright();
        SERIAL_OUT.print("Set Display Bright: "); SERIAL_OUT.println(memory_map[MaCaco_OUT_s + SLOT_BRIGHT_DISPLAY + 1]);

        //write system ON/OFF
        if (getSystem()) {
          //ON
          SERIAL_OUT.println("Set system ON ");
          set_ThermostatMode(SLOT_THERMOSTAT);        // Set System On
        } else {
          //OFF
          SERIAL_OUT.println("Set system OFF ");
          memory_map[MaCaco_OUT_s + SLOT_THERMOSTAT] &= ~ (Souliss_T3n_SystemOn | Souliss_T3n_FanOn1 | Souliss_T3n_FanOn2 | Souliss_T3n_FanOn3 | Souliss_T3n_CoolingOn | Souliss_T3n_HeatingOn);
        }

        memory_map[MaCaco_IN_s + SLOT_THERMOSTAT] = Souliss_T3n_RstCmd;          // Reset
        // Trig the next change of the state
        data_changed = Souliss_TRIGGED;
      }
    }

    FAST_910ms() {
      if (timerDisplay_setpoint()) {
        //if timeout read value of T19
        backLEDvalueLOW =  memory_map[MaCaco_OUT_s + SLOT_BRIGHT_DISPLAY + 1];
        FADE = 0;
        //HOMESCREEN ////////////////////////////////////////////////////////////////
        if (!getEnabled()) {
          if (getLayout1()) {
            display_layout1_HomeScreen(ucg, temperature, humidity, setpoint, getSystemState());
          } else if (getLayout2()) {
            //
          }
        }
      }
    }

    // Run communication as Gateway or Peer
    if (IsRuntimeGateway())
      FAST_GatewayComms();
    else
      FAST_PeerComms();
  }

  EXECUTESLOW() {
    UPDATESLOW();

    SLOW_50s() {
      //*************************************************************************
      //*************************************************************************
      if (!getEnabled()) {
        if (getLayout1()) {
          getTemp();
          if (getCrono()) {
            Serial.println("CRONO: aggiornamento");
            setSetpoint(checkNTPcrono(ucg));   
            setEncoderValue(checkNTPcrono(ucg));             
            Serial.print("CRONO: setpoint: ");Serial.println(setpoint);         
          }
        } else if (getLayout2()) {
          display_layout2_print_circle_white(ucg);
          getTemp();
          display_layout2_print_circle_black(ucg);
          display_layout2_HomeScreen(ucg, temperature, humidity, setpoint);
          display_layout2_print_datetime(ucg);
          if (getCrono()) {
            Serial.println("CRONO: aggiornamento");
            setSetpoint(checkNTPcrono(ucg));
            setEncoderValue(checkNTPcrono(ucg));
            Serial.print("CRONO: setpoint: ");Serial.println(setpoint);           
          }else{         
            ucg.setColor(0, 0, 0);       // black
            ucg.drawDisc(156, 50, 5, UCG_DRAW_ALL);
            ucg.drawDisc(165, 62, 6, UCG_DRAW_ALL);
            ucg.drawDisc(173, 77, 7, UCG_DRAW_ALL);
            ucg.drawDisc(179, 95, 8, UCG_DRAW_ALL);
            }
          yield();
          display_layout2_print_circle_green(ucg);
        }
      }
    }

    SLOW_70s() {
      if (!getEnabled()) {
        if (getLayout1()) {
          //
        } else if (getLayout2()) {
          calcoloAndamento(ucg, temperature);
          display_layout2_print_datetime(ucg);
          display_layout2_print_circle_green(ucg);
        }
      }
    }
    
    SLOW_15m() {
      //NTP
      /////////////////////////////////////////////////////////////////////////////////////////////////////////
      yield();
      initNTP();
      yield();
    }

    // If running as Peer
    if (!IsRuntimeGateway())
      SLOW_PeerJoin();
  }
  // Look for a new sketch to update over the air
  OTA_Process();
}

void set_ThermostatMode(U8 slot) {
  memory_map[MaCaco_OUT_s + slot] |= Souliss_T3n_HeatingMode | Souliss_T3n_SystemOn;
  // Trig the next change of the state
  data_changed = Souliss_TRIGGED;
}

void set_DisplayMinBright(U8 slot, U8 val) {
  memory_map[MaCaco_OUT_s + slot + 1] = val;
  // Trig the next change of the state
  data_changed = Souliss_TRIGGED;
}

void getTemp() {
  // Read temperature value from DHT sensor and convert from single-precision to half-precision
  temperature = dht.readTemperature();
  //Import temperature into T31 Thermostat
  ImportAnalog(SLOT_THERMOSTAT + 1, &temperature);
  ImportAnalog(SLOT_TEMPERATURE, &temperature);

  // Read humidity value from DHT sensor and convert from single-precision to half-precision
  humidity = dht.readHumidity();
  ImportAnalog(SLOT_HUMIDITY, &humidity);

  SERIAL_OUT.print("aquisizione Temperature: "); SERIAL_OUT.println(temperature);
  SERIAL_OUT.print("aquisizione Humidity: "); SERIAL_OUT.println(humidity);
}

boolean getSystemState(){
    SERIAL_OUT.print("System State: "); SERIAL_OUT.println(memory_map[MaCaco_OUT_s + SLOT_THERMOSTAT] & Souliss_T3n_SystemOn);
    return memory_map[MaCaco_OUT_s + SLOT_THERMOSTAT] & Souliss_T3n_SystemOn;
}

void bright(int lum) {
  int val = ((float)lum / 100) * 1023;
  if (val > 1023) val = 1023;
  if (val < 0) val = 0;
  analogWrite(BACKLED, val);
}


void initScreen() {
  ucg.clearScreen();
  if (getLayout1()) {
    SERIAL_OUT.println("HomeScreen Layout 1");
    display_layout1_HomeScreen(ucg, temperature, humidity, setpoint, getSystemState());
    getTemp();
  }
  else if (getLayout2()) {
    SERIAL_OUT.println("HomeScreen Layout 2");
    getTemp();
    display_layout2_HomeScreen(ucg, temperature, humidity, setpoint);
    display_layout2_print_circle_white(ucg);
    display_layout2_print_datetime(ucg);
    display_layout2_print_circle_black(ucg);
    yield();
    display_layout2_print_circle_green(ucg);
  }
}

void encoderFunction() {
  encoder();
}

void setSetpoint(float setpoint){
  //SERIAL_OUT.print("Away: ");SERIAL_OUT.println(memory_map[MaCaco_OUT_s + SLOT_AWAY]);
  if(memory_map[MaCaco_OUT_s + SLOT_AWAY]) {
    //is Away 
    
  } else {
    //is not Away
  }
  Souliss_HalfPrecisionFloating((memory_map + MaCaco_OUT_s + SLOT_THERMOSTAT + 3), &setpoint);
}


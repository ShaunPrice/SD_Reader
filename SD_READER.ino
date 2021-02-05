/*
    Name:       SD_READER.ino
    Created:	12/08/2018 9:55:08 PM
    Author:     SHAUNSPC\Shaun Price


	SD Card VSPI PINS
	========================================
	SCLK = 18, MISO = 19, MOSI = 23, SS = 5

	FPGA SPI ESP32 HSPI PINS
	========================================
	SCLK = 14, MISO = 12, MOSI = 13, SS = 15

	FPGA SPI Teensy Pins
	========================================
	SCLK = 13, MISO = 12, MOSI = 11, SS = 10

	========================================
	TODO
	========================================
	1. Add FPGA handshake and reset
	2. Play any fseq files on SD
	3. WiFi and Bluetooth upload to SDHC
	4. Check FSEQ header. channelsPerStep doesn't always match 

	*/

#include <sdios.h>
#include <SdFatConfig.h>
#include <SdFat.h>
#include <FSEQLib.h>
#include <SPI.h>

#define DEBUG


#ifdef DEBUG
#define DEBUG_PRINTLN(x)  DEBUG_PRINT(x); Serial.println (x);
#define DEBUG_PRINT(x); Serial.print (x);
#else
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINT(x)
#endif

#define CardDetectedTrue  1
#define CardDetectedFalse 0

#ifdef ESP32
#include <sd_diskio.h>
#include <sd_defines.h>
#include <SD.h>

SPIClass spiFPGA(HSPI);
#define FPGA_HANDSHAKE 2
#define FPGA_MISO 12
#define FPGA_MOSI 13
#define FPGA_SCK  14
#define FPGA_CS   15
const int cardDetectedPin = GPIO_NUM_17; // May require 10k pull-up
#else
const uint8_t chipSelect = BUILTIN_SDCARD; // Teensy 3.5 & 3.6 & 4.1 on-board
#define SD_FAT_TYPE 3
#define FPGA_CS   10
#endif

const size_t UNIVERSES = 16 ;
const size_t PIXELS = 150;
const size_t LEDS_PER_UNIVERSE = PIXELS * 3;
const size_t STEP_BUFFER_LENGTH = (UNIVERSES * LEDS_PER_UNIVERSE) + ((4 - (UNIVERSES * LEDS_PER_UNIVERSE) % 4) % 4);
size_t bytesRead = 0;
SdFs sd;
FsFile dataFile;
HeaderData rawHeader;
FSEQLib header;
const  char* filename = "show.dat";

// SPI
char stepBuffer[STEP_BUFFER_LENGTH];
uint8_t universeBuffer[LEDS_PER_UNIVERSE + 5];
char * receiveFPGABuffer;

uint32_t currentStep = 0;

bool cardInitialised = false;
bool cardDetected = false;

void setup()
{
#ifdef DEBUG
	Serial.begin(115200);
#endif
	pinMode(FPGA_CS, OUTPUT);

#ifdef ESP32	
	pinMode(SS, OUTPUT); // SD Card VSPI SS

	pinMode(cardDetectedPin,INPUT_PULLUP); // SD Card Detected CD 

	// FPGA PIN Mode
	pinMode(FPGA_MOSI, OUTPUT);
	pinMode(FPGA_MISO, INPUT);
	pinMode(FPGA_SCK, OUTPUT);

	spiFPGA.begin(FPGA_SCK, FPGA_MISO, FPGA_MOSI, FPGA_CS);
#else
	SPI.begin();
#endif
	digitalWrite(FPGA_CS, HIGH);
}

void loop()
{
	unsigned long startMicros = micros();	
	
	DEBUG_PRINTLN("Checking Initialization state of SD card...");
#ifdef ESP32
	cardDetected = (digitalRead(cardDetectedPin) == 1) ? true : false;

	// Initialise SPI Master

	// See if the card is present and can be initialized:
	if (cardDetected && !cardInitialised)
	{
		DEBUG_PRINT("Initializing SD card...");
		if (SD.begin())
		{
#else
	if (!cardInitialised)
	{		
		DEBUG_PRINTLN("Initializing SD card...");
		if (sd.begin(chipSelect))
		{
#endif				
			DEBUG_PRINTLN("SD card initialized.");
			DEBUG_PRINTLN("Checking for Xlights FSEQ file.");
			// Find the FSEQ file
			sd.ls();
			if (sd.exists(filename))
			{
				DEBUG_PRINTLN("FSEQ File Exists. Reading file.");
				dataFile = sd.open(filename);
				Serial.print("File size: ");
				dataFile.printFileSize(&Serial);
				DEBUG_PRINTLN(" bytes");

				if (dataFile.size() > 0)
				{
					dataFile.readBytes(rawHeader.rawData, 28);
					header = FSEQLib(rawHeader);

					DEBUG_PRINTLN("======================");
					DEBUG_PRINTLN("== Xlights FSEQ Header");
					DEBUG_PRINTLN("======================");
					DEBUG_PRINTLN("Magic: " + header.magic());
					DEBUG_PRINTLN("Data Offset: " + String(header.dataOffset()));
					DEBUG_PRINTLN("Version: " + header.majorVersion());
					DEBUG_PRINTLN("Header Length: " + String(header.headerLength()));
					DEBUG_PRINTLN("Channels per Step: " + String(header.channelsPerStep()));
					DEBUG_PRINTLN("Step Length: " + String(header.sequenseSteps()));
					DEBUG_PRINTLN("======================");

					// Set the initialised state if the magic is correct
					if (header.magic() == "PSEQ")
					{
						cardInitialised = true;
						DEBUG_PRINTLN("done!");
					}
					else
					{
						DEBUG_PRINTLN("Invalid Magic in header.");
					}
				}
				else
				{
					cardInitialised = false;
					dataFile.close();
				}
			}
			else
			{
				DEBUG_PRINTLN("File does not exist.");
			}
		}
		else
		{
			DEBUG_PRINTLN("Card failed, or not present");
			delay(500);
		}
	}
#ifdef ESP32
	else if (!cardDetected && cardInitialised)
	{
		DEBUG_PRINT("Card removed"); 
		cardInitialised = false;
		dataFile.close();
		SD.end();

	}
	else if (cardDetected && cardInitialised)
#else
	else
#endif
	{
		DEBUG_PRINTLN("Card initialized. Reading data.");

		// Read the channels for the next step
		if (dataFile.readBytes(stepBuffer, STEP_BUFFER_LENGTH) != STEP_BUFFER_LENGTH)
		{
			DEBUG_PRINTLN("Buffer Failed to load. Closing SD Card");
			cardInitialised = false;
			dataFile.close();
#ifdef ESP32
			SD.end();
#endif		
		}

		// Output data
		for (uint8_t current_universe = 0; current_universe < UNIVERSES; current_universe++)
		{
			// Copy the led values into the universe buffer
			memcpy(&universeBuffer[5],&stepBuffer[current_universe * LEDS_PER_UNIVERSE], LEDS_PER_UNIVERSE);

			// Set the header to the universe number (starting with 0)
			universeBuffer[0] = current_universe;

			// Set the header pixel count (256 max)
			universeBuffer[1] = PIXELS;

			// Set the header colour ordering
			universeBuffer[2] = 1; // Red
			universeBuffer[3] = 2; // Green
			universeBuffer[4] = 0; // Blue

			SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

			// Lower the CS line
			digitalWrite(FPGA_CS, LOW);

			// Send the data
#ifdef ESP32
			spiFPGA.writeBytes(&universeBuffer[0], LEDS_PER_UNIVERSE + 1);
#else
			SPI.transfer(&universeBuffer[0], LEDS_PER_UNIVERSE + 5);
#endif		
			// Raise the CS line
			digitalWrite(FPGA_CS, HIGH);

			SPI.endTransaction();

			DEBUG_PRINTLN("Universe "+String(current_universe)+" send complete.");
		}
		
		currentStep++;

		// Reset to first step if we have gone past the last step
		if (currentStep == header.sequenseSteps())
		{
			// Restart at first step
			currentStep = 0;
			// Restart file after header
			dataFile.seek(header.headerLength());
		}
	}
	// Delay to make sure we send x20 per second.
	// The delay is constrained between 1ms and 50ms
	delayMicroseconds(constrain(50000 - (micros() - (uint32_t)startMicros),1,50000));
}

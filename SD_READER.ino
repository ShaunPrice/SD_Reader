/*
    Name:       SD_READER.ino
    Created:	12/08/2018 9:55:08 PM
    Author:     SHAUNSPC\Shaun Price


	SD Card VSPI PINS
	========================================
	SCLK = 18, MISO = 19, MOSI = 23, SS = 5

	FPGA Card HSPI PINS
	========================================
	SCLK = 14, MISO = 12, MOSI = 13, SS = 15

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

#define CardDetectedTrue  1
#define CardDetectedFalse 0

#ifdef ESP32
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
#endif

#define FPGA_CS   15

const size_t UNIVERSES = 16 ;
const size_t PIXELS = 150;
const size_t LEDS_PER_UNIVERSE = PIXELS * 3;
const size_t BUFFER_LENGTH = (UNIVERSES * LEDS_PER_UNIVERSE) + ((4 - (UNIVERSES * LEDS_PER_UNIVERSE) % 4) % 4);
size_t bytesRead = 0;
SdFs sd;
FsFile dataFile;
HeaderData rawHeader;
FSEQLib header;
const  char* filename = "Rainbow.fseq";

// SPI
char stepBuffer[BUFFER_LENGTH];
uint8_t universeBuffer[LEDS_PER_UNIVERSE + 1];
char * receiveFPGABuffer;

uint32_t currentStep = 0;

bool cardInitialised = false;
bool cardDetected = false;

void setup()
{
	Serial.begin(115200);

#ifdef ESP32	
	pinMode(SS, OUTPUT); // SD Card VSPI SS

	pinMode(cardDetectedPin,INPUT_PULLUP); // SD Card Detected CD 

	// FPGA PIN Mode
	pinMode(FPGA_MOSI, OUTPUT);
	pinMode(FPGA_MISO, INPUT);
	pinMode(FPGA_SCK, OUTPUT);
	pinMode(FPGA_CS, OUTPUT);

	spiFPGA.begin(FPGA_SCK, FPGA_MISO, FPGA_MOSI, FPGA_CS);
#else
	SPI.begin();
#endif
	digitalWrite(FPGA_CS, 1);
}

void loop()
{
	unsigned long startMillis = millis();	
	
	Serial.println("Checking Initialization state of SD card...");
#ifdef ESP32
	cardDetected = (digitalRead(cardDetectedPin) == 1) ? true : false;

	// Initialise SPI Master

	// See if the card is present and can be initialized:
	if (cardDetected && !cardInitialised)
	{
		Serial.println("Initializing SD card...");
		if (SD.begin())
		{
#else
	if (!cardInitialised)
	{		
		Serial.println("Initializing SD card...");
		if (sd.begin(chipSelect))
		{
#endif				
			Serial.println("SD card initialized.");
			Serial.println("Checking for Xlights FSEQ file.");
			// Find the FSEQ file
			sd.ls();
			if (sd.exists(filename))
			{
				Serial.println("FSEQ File Exists. Reading file.");
				dataFile = sd.open(filename);
				Serial.print("File size: ");
				dataFile.printFileSize(&Serial);
				Serial.println(" bytes");

				if (dataFile.size() > 0)
				{
					dataFile.readBytes(rawHeader.rawData, 28);
					header = FSEQLib(rawHeader);

					Serial.println("======================");
					Serial.println("== Xlights FSEQ Header");
					Serial.println("======================");
					Serial.println("Magic: " + header.magic());
					Serial.println("Data Offset: " + String(header.dataOffset()));
					Serial.println("Version: " + header.majorVersion());
					Serial.println("Header Length: " + String(header.headerLength()));
					Serial.println("Channels per Step: " + String(header.channelsPerStep()));
					Serial.println("Step Length: " + String(header.stepLength()));
					Serial.println("Universes: " + String((header.universes() == 0) ? "N/A" : String(header.universes())));
					Serial.println("Size of Universe: " + String((header.sizeofUniverses() == 0) ? "N/A" : String(header.sizeofUniverses())));
					Serial.println("Gamma: " + String(header.gamma()));
					Serial.println("Light Type: " + header.lightType());
					Serial.println("======================");

					// Set the initialised state if the magic is correct
					if (header.magic() == "PSEQ")
					{
						cardInitialised = true;
						Serial.println("done!");
					}
					else
					{
						Serial.println("Invalid Magic in header.");
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
				Serial.println("File does not exist.");
			}
		}
		else
		{
			Serial.println("Card failed, or not present");
			delay(500);
		}
	}
#ifdef ESP32
	else if (!cardDetected && cardInitialised)
	{
		Serial.println("Card removed"); 
		cardInitialised = false;
		dataFile.close();
		SD.end();

	}
	else if (cardDetected && cardInitialised)
#else
	else
#endif
	{
		Serial.println("Card initialized. Reading data.");

		// Read the channels for the next step
		if (dataFile.readBytes(stepBuffer, BUFFER_LENGTH) != BUFFER_LENGTH)
		{
			Serial.println("Buffer Failed to load. Closing SD Card");
			cardInitialised = false;
			dataFile.close();
#ifdef ESP32
			SD.end();
#endif		
		}

		// Output data
		for (uint8_t current_universe = 0; current_universe < UNIVERSES; current_universe++)
		{
			// Set the one byte header to the universe number (starting with 0)
			universeBuffer[0] = current_universe;
			
			// Copy the led values into the universe buffer
			memcpy(&universeBuffer[1],&stepBuffer[current_universe * LEDS_PER_UNIVERSE], LEDS_PER_UNIVERSE);
			
			// Lower the CS line
			digitalWrite(FPGA_CS, 0);

			// Send the data
#ifdef ESP32
			spiFPGA.writeBytes(&universeBuffer[0], LEDS_PER_UNIVERSE + 1);
#else
			SPI.transfer(&universeBuffer[0], LEDS_PER_UNIVERSE + 1);
#endif

			// Raise the CS line
			digitalWrite(FPGA_CS, 1);
#ifndef ESP32
			SPI.endTransaction();
#endif
		}
		
		currentStep++;

		// Reset to first step if we have gone past the last step
		if (currentStep == header.stepLength())
		{
			// Restart at first step
			currentStep = 0;
			// Restart file after header
			dataFile.seek(header.headerLength());
		}
	}
	// Delay to make sure we send x20 per second.
	// The delay is constrained between 1ms and 50ms
	delay(constrain(50 - (millis() - (uint32_t)startMillis),1,50));
}

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
#include <SD.h>
#include <SPI.h>

#define CardDetectedTrue  1
#define CardDetectedFalse 0

SPIClass spiFPGA(HSPI);
#define FPGA_HANDSHAKE 2
#define FPGA_MISO 12
#define FPGA_MOSI 13
#define FPGA_SCK  14
#define FPGA_CS   15

const int cardDetectedPin = GPIO_NUM_17; // May require 10k pull-up
const int UNIVERSES = 10;
const int PIXELS = 144;
const int LEDS_PER_UNIVERSE = PIXELS * 3;
const int BUFFER_LENGTH = (UNIVERSES * LEDS_PER_UNIVERSE) + ((4 - (UNIVERSES * LEDS_PER_UNIVERSE) % 4) % 4);
size_t bytesRead = 0;
File dataFile;

// SPI
char stepBuffer[BUFFER_LENGTH];
uint8_t universeBuffer[LEDS_PER_UNIVERSE + 1];
char * receiveFPGABuffer;

int32_t currentStep = 0;

#pragma pack(push, 1)
typedef struct headerData_t
{
	char	  magic[4];	// Should be "PSEQ"
	uint16_t  dataOffset;
	uint8_t   minorVersion;
	uint8_t   majorVersion;
	uint16_t  headerLength;
	uint16_t  lchannelsPerStep;
	uint16_t  hchannelsPerStep;
	uint16_t  lstepLength;
	uint16_t  hstepLength;
	uint16_t  fifty; // Always 50
	uint16_t  universes;
	uint16_t  sizeofUniverse;
	uint8_t   gamma;
	uint8_t   lightType;
};
#pragma pop() 

typedef union HeaderData
{
	headerData_t headerData;
	char rawData[28];	    // Xlights FSEQ Header
};

class XlightsHeader
{
private:
	headerData_t _header;
public:
	XlightsHeader() {};

	XlightsHeader(HeaderData header)
	{
		_header = header.headerData;
	}

	void xlightsHeader(HeaderData header)
	{
		_header = header.headerData;
	}

	String magic() { return String(_header.magic); }
	uint16_t dataOffset() { return _header.dataOffset; }
	String versionString() { return String(_header.majorVersion) + "." + String(_header.minorVersion); }
	uint8_t majorVersion() { return _header.majorVersion; }
	uint8_t minorVersion() { return _header.minorVersion; }
	uint16_t headerLength() { return (int)_header.headerLength; }
	uint32_t channelsPerStep() { return (uint32_t)_header.lchannelsPerStep + (uint32_t)(_header.hchannelsPerStep * 65536); }
	uint32_t stepLength() { return (uint32_t)_header.lstepLength + (uint32_t)(_header.hstepLength * 65536); }
	uint16_t universes() { return _header.universes; }
	uint16_t sizeofUniverses() { return _header.sizeofUniverse; }
	uint8_t gamma() { return _header.gamma; }
	String lightType() { return (_header.lightType == 0) ? "RGB" : "UNKNOWN"; }
};

HeaderData rawHeader;
XlightsHeader header;

bool cardInitialised = false;
bool cardDetected = false;

void setup()
{
	Serial.begin(115200);

	pinMode(SS, OUTPUT); // SD Card VSPI SS
	pinMode(cardDetectedPin,INPUT_PULLUP); // SD Card Detected CD 

	// FPGA PIN Mode
	pinMode(FPGA_MOSI, OUTPUT);
	pinMode(FPGA_MISO, INPUT);
	pinMode(FPGA_SCK, OUTPUT);
	pinMode(FPGA_CS, OUTPUT);

	spiFPGA.begin(FPGA_SCK, FPGA_MISO, FPGA_MOSI, FPGA_CS);

	digitalWrite(FPGA_CS, 1);
}

void loop()
{
	unsigned long startMillis = millis();
	cardDetected = (digitalRead(cardDetectedPin) == 1) ? true : false;
	// Initialise SPI Master

	// See if the card is present and can be initialized:
	if (cardDetected && !cardInitialised)
	{
		Serial.print("Initializing SD card...");
		
		if (SD.begin())
		{
			Serial.println("card initialized.");

			// open the file. note that only one file can be open at a time,
			// so you have to close this one before opening another.
			dataFile = SD.open("/xlight00.dat");
			Serial.println("File size: " + String(dataFile.size()));
			if (dataFile.size() > 0)
			{
				cardInitialised = true;

				dataFile.readBytes(rawHeader.rawData, 28);

				header = XlightsHeader(rawHeader);

				Serial.println("======================");
				Serial.println("== Xlights FSEQ Header");
				Serial.println("======================");
				Serial.println("Magic: " + header.magic());
				Serial.println("Data Offset: " + String(header.dataOffset()));
				Serial.println("Version: " + header.versionString());
				Serial.println("Header Length: " + String(header.headerLength()));
				Serial.println("Channels per Step: " + String(header.channelsPerStep()));
				Serial.println("Step Length L: " + String(header.stepLength()));
				Serial.println("Universes: " + String((header.universes() == 0) ? "N/A" : String(header.universes())));
				Serial.println("Size of Universe: " + String((header.sizeofUniverses() == 0) ? "N/A" : String(header.sizeofUniverses())));
				Serial.println("Gamma: " + String(header.gamma()));
				Serial.println("Light Type: " + (header.lightType() == 0) ? "RGB" : "Unknown");
				Serial.println("======================");

				Serial.println("done!");
			}
			else
			{
				cardInitialised = false;
				dataFile.close();
			}
		}
		else
		{
			Serial.println("Card failed, or not present");
			delay(500);
		}
	}
	else if (!cardDetected && cardInitialised)
	{
		Serial.println("Card removed"); 
		cardInitialised = false;
		dataFile.close();
		SD.end();
	}
	else if (cardDetected && cardInitialised)
	{ 
		// Read the channels for the next step
		if (dataFile.readBytes(stepBuffer, BUFFER_LENGTH) != BUFFER_LENGTH)
		{
			Serial.println("Buffer Failed to load. Closing SD Card");
			cardInitialised = false;
			dataFile.close();
			SD.end();
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
			spiFPGA.writeBytes(&universeBuffer[0], LEDS_PER_UNIVERSE + 1);
			
			// Raise the CS line
			digitalWrite(FPGA_CS, 1);
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
	delay(constrain(50 - (millis() - startMillis),1,50));
}

#include <Arduino.h>
#include <functional>
#include <WiFi.h>
#include <WiFiUdp.h>

#define UDP_RX_PACKET_MAX_SIZE 1500

// You only need to modify folloing settings to get it to work woth Alexa and your network
#define WIFI_SSID   "YourSSID"
#define WIFI_PASS   "YourPassword"
#define DEVICE_NAME "YourDeviceName"
#define OUTPUT_PIN  12

static String    deviceName = DEVICE_NAME;
static const int outputPin  = OUTPUT_PIN;

static WiFiUDP    UDP;
static WiFiServer server(80);
static char       packetBuffer[UDP_RX_PACKET_MAX_SIZE]; //buffer to hold incoming packet,
static String     persistentUuid;
static String     ipAddressAsString;
static bool       switchState;

static void prepareId(void)
{
	uint64_t macAddress = ESP.getEfuseMac() & 0xffffffff;
	uint32_t chipId     = (uint32_t)macAddress;
	Serial.printf("ChipId 0x%08x\n", chipId);

	char tempBuffer[64];
	sprintf_P(tempBuffer, PSTR("38323636-4558-4dda-9188-cda0e6%02x%02x%02x"), (uint16_t)((chipId >> 16) & 0xff),(uint16_t)((chipId >> 8)& 0xff), (uint16_t)chipId & 0xff);

	persistentUuid    = "Socket-1_0-" + String(tempBuffer);
	ipAddressAsString = WiFi.localIP().toString();
}

static void wifiSetup(void)
{
	int nConnection = 0;

	WiFi.mode(  WIFI_STA);
	WiFi.begin( WIFI_SSID, WIFI_PASS);

	while(1)
	{
		if(--nConnection < 0)
		{    
			Serial.printf("\n[WIFI] Trying to connect to %s ", WIFI_SSID);
			nConnection = 64;
		}
		if(WiFi.status() == WL_CONNECTED) break;

		Serial.print(".");
		delay(500);
	}
	Serial.printf("\n[WIFI] Connected, IP address: %s\n", WiFi.localIP().toString().c_str());
}

static void configureUdpForMulticast(void)
{
	Serial.println("Configuring Udp for Multicast ...");

	IPAddress ipMulti(239, 255, 255, 250);
	UDP.beginMulticast(ipMulti, 1900);
}

static void sendHttpPacket(WiFiClient client, String type, String body)
{
	String response = "HTTP/1.1 200 OK\r\nContent-type:{1}\r\n\r\n" + body;
	response.replace("{1}", type);

	Serial.printf("HTTP Sending :\n%s", response.c_str());
	client.print(response);
}

void sendRelayState(WiFiClient client, String responseType)
{
	String response = 
    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>\r\n"
    "<u:{1}BinaryStateResponse xmlns:u=\"urn:Belkin:service:basicevent:1\">\r\n"
    "<BinaryState>{2}</BinaryState>\r\n"
    "</u:{1}BinaryStateResponse>\r\n"
    "</s:Body> </s:Envelope>\r\n";

	response.replace("{1}", responseType);
	response.replace("{2}", switchState ? "1" : "0");
	sendHttpPacket(client, "text/xml", response);
}

static bool stringContains(String stringToSearch, const char*  stringToSearchFor)
{
	return stringToSearch.indexOf(stringToSearchFor) > 0; 
}

static void updateOutputState(int requiredState)
{
	Serial.printf("#### Got Turn %s request ####", requiredState == 1 ? "ON" : "OFF");
	switchState = requiredState;
	digitalWrite(outputPin, requiredState == 1 ? HIGH : LOW);
}

static void processSetBinaryStateRequest(WiFiClient client, String httpPacket)
{
	if(     stringContains(httpPacket, "<BinaryState>1</BinaryState>")) updateOutputState(1);
	else if(stringContains(httpPacket, "<BinaryState>0</BinaryState>")) updateOutputState(0);

	sendRelayState(client, "Set");
}

static void  processGetBinaryStateRequest(WiFiClient client)
{
	Serial.println("#### Got binary state request ####");
	sendRelayState(client, "Get");
}

static void processSetupRequest(WiFiClient client)
{
	Serial.println(" ########## Responding to setup.xml. ########\n");

	String setup_xml = "<?xml version=\"1.0\"?>"
    "<root>"
    "<device>"
    "<deviceType>urn:Belkin:device:controllee:1</deviceType>"
    "<friendlyName>" + deviceName + "</friendlyName>"
    "<manufacturer>Belkin International Inc.</manufacturer>"
    "<modelName>Emulated Socket</modelName>"
    "<modelNumber>3.1415</modelNumber>"
    "<UDN>uuid:" + persistentUuid + "</UDN>"
    "<serialNumber>221517K0101769</serialNumber>"
    "<binaryState>0</binaryState>"
    "<serviceList>"
    "<service>"
    "<serviceType>urn:Belkin:service:basicevent:1</serviceType>"
    "<serviceId>urn:Belkin:serviceId:basicevent1</serviceId>"
    "<controlURL>/upnp/control/basicevent1</controlURL>"
    "<eventSubURL>/upnp/event/basicevent1</eventSubURL>"
    "<SCPDURL>/eventservice.xml</SCPDURL>"
    "</service>"
    "</serviceList>" 
    "</device>"
    "</root>\r\n"
    "\r\n";
  sendHttpPacket(client, "text/xml", setup_xml);
}

static void processHttpRequest(WiFiClient client, String httpPacket)
{
	Serial.println("#### Proccessing http request ####\n" + httpPacket + "\n#### End of http request ####");

	if(     stringContains(httpPacket, "SetBinaryState xmlns:u=\"urn:Belkin:service:basicevent:1\"")) processSetBinaryStateRequest(client, httpPacket);
	else if(stringContains(httpPacket, "urn:Belkin:service:basicevent:1#GetBinaryState"))             processGetBinaryStateRequest(client);
	else if(stringContains(httpPacket, "/setup.xml"))                                                 processSetupRequest(client);
}

static void handleHttpRequests(void)
{
	WiFiClient client = server.available();   // Listen for incoming clients

	if(!client)                                // If a new client connects,
	{
		return;
	}
	Serial.println("##### New Client ####");

	const unsigned long timeoutTime  = 500;// Define timeout time in milliseconds (example: 2000ms = 2s)
	unsigned       long currentTime  = millis();
	unsigned       long startTime    = currentTime;
	String              httpPacket   = "";                // make a String to hold incoming data from the client

	enum {rsReceivingData, rsReceivedFirstCr, rsReceivedFirstNl, rsReceivedSecondCr }receiveState = rsReceivingData;

	while(client.connected()) // loop while the client's connected
	{
		bool timeout = currentTime - startTime > timeoutTime;
		if(timeout)
		{
			processHttpRequest(client, httpPacket);
			break;
		}
      
		currentTime = millis();
		if (client.available())
		{                                     // if there's bytes to read from the client,
			char c = client.read();             // read a byte, then
  		if(c == '\r')                       // if the byte is a CR character
			{
				if(receiveState == rsReceivingData)
				{
					receiveState = rsReceivedFirstCr;                                    
				}
				else if(receiveState == rsReceivedFirstNl)
				{
					receiveState = rsReceivedSecondCr; 
				}
				else
				{
					receiveState = rsReceivingData;
				}
			}
			else if(c == '\n')                       // if the byte is a newline character
			{
				if(receiveState == rsReceivedFirstCr)
				{
					receiveState = rsReceivedFirstNl;                                    
				}
				else if(receiveState == rsReceivedSecondCr)
				{
					processHttpRequest(client, httpPacket);
					httpPacket = "";
					receiveState = rsReceivingData;
				}
				else
				{
					receiveState = rsReceivingData;
				}
			}
			httpPacket += c;      // add it to the end of the currentLine
		}
	}
// Close the connection
	client.stop();
	Serial.println("#### Client disconnected ####");
}

static void sendUdpPacket(const char *pData)
{
	UDP.beginPacket(UDP.remoteIP(), UDP.remotePort());
	UDP.write((const uint8_t *)pData, strlen(pData));
	UDP.endPacket();
}

static void respondToSearch(void)
{
	String response = 
    "HTTP/1.1 200 OK\r\n"
    "CACHE-CONTROL: max-age=86400\r\n"
    "DATE: Fri, 15 Apr 2016 04:56:29 GMT\r\n"
    "EXT:\r\n"
    "LOCATION: http://" + ipAddressAsString + ":80/setup.xml\r\n"
    "OPT: \"http://schemas.upnp.org/upnp/1/0/\"; ns=01\r\n"
    "01-NLS: b9200ebb-736d-4b93-bf03-835149d13983\r\n"
    "SERVER: Unspecified, UPnP/1.0, Unspecified\r\n"
    "ST: urn:Belkin:device:**\r\n"
    "USN: uuid:" + persistentUuid + "::urn:Belkin:device:**\r\n"
    "X-User-Agent: redsonic\r\n\r\n";

	Serial.print(response);
	sendUdpPacket(response.c_str());
}

static void printUdpDebug(String request)
{
#ifdef DEBUG_UDP
	Serial.printf("\nUDP : Received packet from %s, port %s\nRequest: %s\n", UDP.remoteIP().toString(), UDP.remotePort().toString(), request.c_str());
#endif
}

static void handleUdpRequests(void)
{
// if there’s no data available, bail out
	if( UDP.parsePacket() <= 0) return;

	int len = UDP.read(packetBuffer, UDP_RX_PACKET_MAX_SIZE - 1);

// if the packet is empty, bail out
	if (len <= 0) return;
  
	packetBuffer[len] = 0;
	String request = packetBuffer;
	printUdpDebug(request);
    
	if(stringContains(request, "M-SEARCH"))
	{
		if(stringContains(request, "urn:Belkin:device:**") || stringContains(request, "ssdp:all") || stringContains(request, "upnp:rootdevice"))
		{
			respondToSearch();
		}
	}
}

static void showStillAlive(void)
{
	static int loopCount   = 0;
	static int columnCount = 0;
	if(++loopCount > 100000)
	{
		Serial.print("*");
		loopCount = 0;
		if(++columnCount > 64)
		{
			long rssi = WiFi.RSSI();
			Serial.printf("\nRSSI: %ld ", rssi);
			columnCount = 0;
		}
	}
}

void setup()
{
	Serial.begin(115200);

	pinMode(outputPin, OUTPUT);
	wifiSetup();
	prepareId();
	configureUdpForMulticast();
	server.begin();
}

void loop()
{
	showStillAlive();
	handleHttpRequests();
	handleUdpRequests();
}

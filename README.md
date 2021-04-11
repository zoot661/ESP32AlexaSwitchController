# ESP32AlexaSwitchController
 
This project is largely based on the [repo](https://github.com/kakopappa/arduino-esp8266-alexa-wemo-switch), much of the hard work was done by kakopappa.

It has been ported to work on an ESP32 rather than an ESP226, requiring modification of the http message handling.

It has been simplified to handle the protocol that my Alexa uses to discover and control 'Belkin' compatible switches.

The simplifed discovery protocol is
  Alexa transmits a 'M_SEARCH' uPNP discovery message using a mulicast packet on IP address 239.255.255.250 port 1900
  ESP responds via UDP with a uPnP response that identifies the URL of the xml file that describes the device setup (in this case setup.xml)
  Alexa reads the xml file (simply by issuing an http://URL, using the URL in the above response)
  ESP responds by 'listing' the xml file using the TCP client (see the function processSetupRequest()

The simplifed control protocol is
  Alexa sends a TCP message containing one of the strings decoded in the function processHttpRequest() (the packet should really be parsed more precisely, but in reality just checking for these strings is sufficient to handle the protocol
  ESP resonds with a TCP message that updates Alexa withthe output status
  If Alexa is happy with the response, she bleeps

The function handleHttpRequests() should really parse the incomng data to determine the end of a message.

Some messages are terminated with CR/LF/CR/LF, which is detected.
Other messages are in XML format and require matching the opening and closing tags. However, this isn't done and instead uses a timeout on the received data to indicate that the full message has been received.

Once the sketch has beed installed on the target ESP32 it is operated with the following voice commands
Alexa, discover devices
Alexa, turn on <device name>
Alexa, turn off <device name>
 
You can test the output of setup.xml by typing its URL into your web browser
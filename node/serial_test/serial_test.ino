/* libraries */

/* constants and enums */
const int BUFFER_SIZE = 4096; // size of the serial buffer, in bytes

enum MsgType {            // the type of serial message received from the datalogger
  TIMESTAMP,
  NUM_ENTRIES,
  TABLE_ENTRY,
  TABLE_FIELDS
};

/* functions */

/*
  retrieve the latest timestamp from the datalogger and convert 
  it back into a 4 byte uint32_t
*/
uint32_t getLatestTimestampFromDL()
{
  byte tsBuffer[4];                            // create a buffer for the timestamp returned by the request
  Serial1.write(TIMESTAMP);                    // send a timestamp request to the datalogger

  for (int response = 0; response > 0; )
  {
    response = Serial1.readBytes(tsBuffer, 4);  // read buffer for datalogger response
  }

  return 0;
}

/* variables */
char serialBuffer[BUFFER_SIZE]; // the buffer to store the serial data into
int bytesRead;                  // the number of bytes read from the serial line
char ts = '0';
uint32_t last_timestamp;        // the last timestamp. uses Unix time, i.e. secs since Jan 1, 1970.
                                // this should last until roughly 2150 AD

/* main program */
void setup() {
  Serial1.begin(9600);    // establish Serial connection with TX/RX <-> datalogger
  Serial.begin(9600);     // establish Serial connection with USB <-> computer

  last_timestamp = 0;     // set last_timestamp to 0 for now. later I will retrieve this value from FLASH
}

void loop() {
  //getLatestTimestampFromDL();
  memset(serialBuffer, 0, sizeof(serialBuffer));

  Serial1.write((ts));

  // sample code to just read whatever comes over the serial line
  bytesRead = Serial1.readBytes(serialBuffer, BUFFER_SIZE);
  // String str = Serial1.readString();
  // str.trim();
  // if (!(str.equals("")))
  // {
  //   Serial.println(str);
  // }
  if (bytesRead > 0)
  {
    // for (int i = 0; serialBuffer[i]; i++)
    // {
    //   Serial.print(serialBuffer[i]);
    //   Serial.print(", ");
    // }
    Serial.println(serialBuffer);
  }
}

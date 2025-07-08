
/* message formats
TIMESTAMP (sent by MCU):                  %TS[MM/DD/YYYY hh:mm:dd]%
NUM_ROWS (sent by DL):                    %NR[START_ROW],[STOP_ROW]%
TABLE_ENTRY (sent by DL):                 %TE[REC_NUM],[TIMESTAMP],[DATA],[DATA],...%
TABLE_METADATA (req by MCU, resp by DL):  %TM%                                                                                                    (from MCU)
                                          %TM[TABLE_NAME];[NUM_COLS];[FIELD1],[FIELD2],[FIELD3],[FIELD4]...;[FIELD1_UNITS],[FIELD2_UNITS],...%    (from DL)
ACK (sent by MCU):                        %ACK[START_ROW],[STOP_ROW]%
*/

/* libraries */

/* constants */
#define BUFFER_SIZE 4096 // size of the serial buffer, in bytes
#define TIMESTAMP_SIZE 20 // size of a time string of format 'MM/DD/YYYY hh:mm:ss'
#define CR_TABLE_NAME_SIZE 21 // maximum size of a CRBasic Table Name (including null term)
#define CR_FIELD_NAME_SIZE 40 // maximum size of a CRBasic Field Name (including null term)

#define TIMESTAMP "TS" // message codes to prepend a message received/transmitted over serial
#define NUM_ROWS "NR"
#define TABLE_ENTRY "TE"
#define TABLE_METADATA "TM"
#define ACK "OK"
#define FRAME "%"

/* enums and structs */

/* functions */
// compare two time strings formatted as MM/DD/YYYY hh:mm:ss
// returns -1 if t1 is less than t2, 0 if they are equal, and 1 if t1 is greater than t2
int compareTime(const char* t1, const char* t2) {
  int retVal = 0;
  uint8_t M1, D1, Y1, h1, m1, s1;
  uint8_t M2, D2, Y2, h2, m2, s2; 
  int result = sscanf(t1, "%d/%d/%d %d:%d:%d", &M1, &D1, &Y1, &h1, &m1, &s1);
  Y1 = Y1 - 1970;
  result = sscanf(t2, "%d/%d/%d %d:%d:%d", &M2, &D2, &Y2, &h2, &m2, &s2);
  Y2 = Y2 - 1970;

  if (Y1 < Y2) { // year comp
    return -1;
  } else if (Y1 > Y2) {
    return 1;
  } else {
    if (M1 < M2) { // month comp
      return -1;
    } else if (M1 > M2) {
      return 1;
    } else {
      if (D1 < D2) { // day comp
        return -1;
      } else if (D1 > D2) {
        return 1;
      } else {
        if (h1 < h2) { // hour comp
          return -1;
        } else if (h1 > h2) {
          return 1;
        } else {
          if (m1 < m2) { // minute comp
            return -1;
          } else if (m1 > m2) {
            return 1;
          } else {
            if (s1 < s2) { // second comp
              return -1;
            } else if (s1 > s2) {
              return 1;
            } else {
              return 0;
}}}}}}}

// returns the length of the message, excluding the % frames, returns -1 if not a proper message
int validateMsg(char* buf) {
  if (buf[0] == '%') {
    for (int i = 1; buf[i]; i++) {
      if (buf[i] == '%') {
        return i-1;
      }
    }
    return -1;
  }
  return -1;
}

// // process table metadata into the pointers received as arguments and confirm they
// void processTableMDMsg(char* buf, char* tblName, int* nCols, char** fldNames, char** fldUnits) {
//   //%TM[TABLE_NAME];[NUM_COLS];[FIELD1],[FIELD2],[FIELD3],[FIELD4]...;[FIELD1_UNITS],[FIELD2_UNITS],...%
//   char* recvTableName
//   int recvNCols;
//   char** recvFldNames;
//   char** recvFldUnits;

//   // parse out table name and number of columns
//   char* svptr;
//   recvTableName = strtok_r(&buf[3], ";", &svptr);
//   recvNCols = 0;

  

//   Serial.println(recvTableName);
//   return;
// }

// send the last timestamp stored in FLASH
// going to change the arguments to handle FLASH business down the road
void sendLastTimeStamp(char* ts) {
  char writeBuf[100];
  snprintf(writeBuf, 100, "%%%s%s%%", TIMESTAMP, ts);
  Serial1.write(writeBuf);
  Serial.println(writeBuf);
  return;
}

// processes the number of rows and sends an ACK to begin
// receiving the table entries
void processNumRowsMsg(char* buf) {
  // gather the the start and stop rows of the message
  char* endptr;
  int startRow = (int)strtol(&buf[3], &endptr, 10);
  int stopRow = (int)strtol((endptr+1), NULL, 10);
  
  // ACK message with requested rows  
  char writeBuf[100];
  snprintf(writeBuf, 100, "%%%s%d,%d%%", ACK, startRow, stopRow);
  Serial1.write(writeBuf);
  Serial.println(writeBuf);
  return;
}

/* variables */
char readBuf[BUFFER_SIZE]; // the buffer to store the serial data into
//char writeBuf[100];
char last_timestamp[] = "01/01/1970 01:01:01";     // later I will retrieve this value from FLASH, the last timestamp. uses Unix time, i.e. secs since Jan 1, 1970.
                                // this should last until roughly 2150 AD
int bytesRead;                  // the number of bytes read from the serial line

int numRows;
int startRow;
int stopRow;

// table metadata
char* tableName;
int numCols;
char** fieldNames;

// boolean to track if table metadata has been processed yet
bool tableMDValidated;

/* main program */
void setup() {
  // read table metadata from FLASH and store it in the program
  // numCols = Flash.getNumCols
  // fieldNames = Flash.getfieldNames
  // tableName = Flash.getTableName

  // establish Serial connection with TX/RX <-> datalogger
  Serial1.begin(9600);
  // establish Serial connection with USB <-> computer
  Serial.begin(9600);

  // the table metadata received by the datalogger has not been validated yet.
  tableMDValidated = false;

  delay(500);
}

void loop() {
  // if there is no table metadata then what the hell are we doing?
  // so first priority is reading metadata in before anything else.
  // it doesn't matter if we already have metadata, we need to validate
  // it from the DL
  if (!tableMDValidated) {
    char writeBuf[100];
    snprintf(writeBuf, 100, "%%%s%%", TABLE_METADATA);
    Serial1.write(writeBuf);
    Serial.println(writeBuf);

    delay(500); // possible unneeded
  }

  // read bytes from serial buffer
  bytesRead = Serial1.readBytes(readBuf, BUFFER_SIZE);  

  int msgLen = validateMsg(readBuf);
  char header[] = {readBuf[1], readBuf[2], '\0'};

  if (bytesRead > 0)
  {
    // gather the serial input from the datalogger
    Serial.println(readBuf);
    // handle serial input

    //parseLine(readBuf, &startRow, &stopRow);
  }

  memset(readBuf, 0, sizeof(readBuf)); // reset the buffer  
  delay(5000);
}

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

// message codes to prepend a message received/transmitted over serial
#define TIMESTAMP "TS"
#define NUM_ROWS "NR"
#define TABLE_ENTRY "TE"
#define TABLE_METADATA "TM"
#define ACK "OK"
#define FRAME "%"

// message types to return by validateMsg function
#define TIMESTAMP_N 0 
#define NUM_ROWS_N 1
#define TABLE_ENTRY_N 2
#define TABLE_METADATA_N 3
#define INVALID_MSG -1

/* enums and structs */

/* functions */
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

// -----
// returns the length of the message, excluding the % frames, returns -1 if not a proper message
int validateMsg(char* buf) {
  // make sure the message is fenced by '%' characters
  if (buf[0] == '%') {
    for (int i = 1; buf[i]; i++) {
      // if the message is properly bounded, check what the message type is
      if (buf[i] == '%') {
        char header[] = {buf[1], buf[2], '\0'};
        if (strcmp(TIMESTAMP_N, header) == 0) {
          return TIMESTAMP_N;
        } 
        else if (strcmp(NUM_ROWS, header) == 0) {
          return NUM_ROWS_N;
        }
        else if (strcmp(TABLE_ENTRY, header) == 0) {
          return TABLE_ENTRY_N;
        }
        else if (strcmp(TABLE_METADATA, header) == 0) {
          return TABLE_METADATA_N;
        }
      }
    }
    return INVALID_MSG;
  }
  return INVALID_MSG;
}

// // parse out a table metadata message into its respective variables
// int parseTableMetadata(char* buf, char** namePtr, int* colsPtr, char** fieldsPtr, char** unitsPtr) {
//   // strtok the hell outta this bitch
//   // %TM[TABLE_NAME];[NUM_COLS];[FIELD1],[FIELD2],[FIELD3],[FIELD4]...;[FIELD1_UNITS],[FIELD2_UNITS],...%    (from DL)


//   // if the table metadata parses correctly, return 1 
//   return 1;
// }

/* variables */

// serial reading/writing variables
int bytesRead;                  // number of bytes read from the serial line
char writeBuf[BUFFER_SIZE];     // serial write buffer
char readBuf[BUFFER_SIZE];      // serial read buffer
int recvMsgType;                // integer representing type of message received by DL`

// FLASH-stored data
char* tableName;                // local name of the table 
int numCols;                    // number of columns the table holds
char** fieldNames;              // names of each field, as an array of strings
char** fieldUnits;              // units of each field, as an array of strings

bool tableMDValidated; // boolean to track if table metadata has been processed yet

/* main program */
void setup() {
  // read table metadata from FLASH and store it in the program
  // numCols = 5;
  // fieldNames = Flash.getTableName;
  // tableName = Flash.getTableName;

  // establish serial communication
  Serial1.begin(9600);          // TX/RX pins <-> datalogger
  Serial.begin(9600);           // USB <-> computer

  // the table metadata received by the datalogger has not been validated yet
  tableMDValidated = false;
}

void loop() {
  // reset the serial write buffer
  memset(writeBuf, 0, sizeof(writeBuf)); // reset the buffer
  recvMsgType = -1;
  // read bytes from serial buffer
  bytesRead = Serial1.readBytes(readBuf, BUFFER_SIZE);  

  // send a request for the table metadata to the DL until a response
  // has been returned
  if (!tableMDValidated) {
    // serial-write request for table metadata to datalogger
    snprintf(writeBuf, 100, "%%%s%%", TABLE_METADATA);
    Serial1.write(writeBuf);
    Serial.println(writeBuf);

    // read and validate serial input from the DL
    bytesRead = Serial1.readBytes(readBuf, BUFFER_SIZE);
    recvMsgType = validateMsg(readBuf);

    if (recvMsgType == TABLE_METADATA_N) {
      // store the temp received metadata and compare to the FLASH metadata
      char* name;
      int cols;
      char* fields;
      char* units;

      char* rest = readBuf;
      char* token;

      // get the name of the table
      name = strtok_r(&(readBuf[3]), ";", &rest);

      // get the number of columns
      char* colsStr = strtok_r(rest, ";", &rest);
      cols = (int)strtol(colsStr, &colsStr, 10);

      // malloc a flat array for each field name
      fields = (char*)malloc(cols*CR_FIELD_NAME_SIZE*sizeof(char*));
      if (fields == NULL){
        Serial.println("fields malloc error");
      }

      // malloc a flat array for each field name
      units = (char*)malloc(cols*CR_FIELD_NAME_SIZE*sizeof(char*));
      if (units == NULL){
        Serial.println("units malloc error");
      }

      // parse out all of the fields and store them in the array
      char* fieldsStr = strtok_r(rest, ";", &rest);
      char* field;
      char* fieldsPtr = fieldsStr;
      snprintf(fields, CR_FIELD_NAME_SIZE, "%s", "RecNum");
      snprintf((fields+CR_FIELD_NAME_SIZE), CR_FIELD_NAME_SIZE, "%s", "TimeStamp");
      for (int i = 2; (i < cols) && (field = strtok_r(fieldsPtr, ",", &fieldsPtr)); i++) {
        char* curPtr = fields + i*CR_FIELD_NAME_SIZE;
        snprintf(curPtr, CR_FIELD_NAME_SIZE, "%s", field);
      }

      // parse out all of the units and store them in respective array
      char* unitsStr = strtok_r(rest, "%", &rest);
      char* unit;
      char* unitsPtr = unitsStr;
      snprintf(units, CR_FIELD_NAME_SIZE, "%s", "None");
      snprintf((units+CR_FIELD_NAME_SIZE), CR_FIELD_NAME_SIZE, "%s", "None");
      for (int i = 2; (i < cols) && (unit = strtok_r(unitsPtr, ",", &unitsPtr)); i++) {
        char* curPtr = units + i*CR_FIELD_NAME_SIZE;
        snprintf(curPtr, CR_FIELD_NAME_SIZE, "%s", unit);
      }

      // compare these new records with what's stored in FLASH
      

      // free up the array resources
      free(fields);
      free(units);

      // once metadata has been handled, set tableMDValidated to true
      tableMDValidated = true;
    }
  }
  else {
    recvMsgType = validateMsg(readBuf);

    if (recvMsgType != INVALID_MSG) {
      if (recvMsgType == TIMESTAMP_N) {
        // handle timestamps (not needed)
      } 
      else if (recvMsgType == NUM_ROWS_N) {
        // once the number of rows is received, it should set numRowsRemaining to that value
        // and return an acknowledgement 
      }
      else if (recvMsgType == TABLE_ENTRY_N) {
        // each entry should be equal to recNum + 1, from recNum_0 to recNum+numRows.
        // if it is correct, send an acknowledgement for the recNum+1, if not, then repeat
        // previous recNum acknowledgement
      }
      else if (recvMsgType == TABLE_METADATA_N) {
        // handle case where table metadata changes while MCU is awake
      }
    }
  }

  // send timestamp to DL
  // sendLastTimeStamp(last_timestamp);

  if (bytesRead > 0)
  {
    // gather the serial input from the datalogger
    //Serial.println(readBuf);
    // handle serial input

    //parseLine(readBuf, &startRow, &stopRow);
  }

  memset(readBuf, 0, sizeof(readBuf)); // reset the buffer  
  delay(5000);
}

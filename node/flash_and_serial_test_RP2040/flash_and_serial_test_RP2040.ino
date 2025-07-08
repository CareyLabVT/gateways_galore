/* message formats
TIMESTAMP (sent by MCU):                  %TS[MM/DD/YYYY hh:mm:dd]%
NUM_ROWS (sent by DL):                    %NR[START_ROW],[STOP_ROW]%
TABLE_ENTRY (sent by DL):                 %TE[REC_NUM],[TIMESTAMP],[DATA],[DATA],...%
TABLE_METADATA (req by MCU, resp by DL):  %TM%                                                                                                    (from MCU)
                                          %TM[TABLE_NAME];[NUM_COLS];[FIELD1],[FIELD2],[FIELD3],[FIELD4]...;[FIELD1_UNITS],[FIELD2_UNITS],...%    (from DL)
ACK (sent by MCU):                        %ACK[START_ROW],[STOP_ROW]%
*/

/* JSON format

*/

/* libraries */
// FLASH support
#include "SdFat_Adafruit_Fork.h"
#include <SPI.h>
#include <Adafruit_SPIFlash.h>
//#include "flash_config.h"             // for flashTransport definition

// JSON support for metadata
//#include <ArduinoJson.h>

/* constants */
#define BUFFER_SIZE 4096 // size of the serial buffer, in bytes
#define TIMESTAMP_SIZE 20 // size of a time string of format 'MM/DD/YYYY hh:mm:ss'

// FLASH related constants
#define DATA_DIR "/data"
#define METADATA_FNAME "/metadata.json"

// CR-basic related constants
#define CR_TABLE_NAME_SIZE 21 // maximum size of a CRBasic Table Name (including null term)
#define CR_FIELD_NAME_SIZE 40 // maximum size of a CRBasic Field Name (including null term)

// message codes to prepend a message received/transmitted over serial
// #define TIMESTAMP "TS"
// #define NUM_ROWS "NR"
#define TABLE_ENTRY "TE"
#define TABLE_METADATA "TM"
// #define ACK "OK"
#define FRAME "%"

// message types to return by validateMsg function
// #define TIMESTAMP_N 0 
// #define NUM_ROWS_N 1
#define TABLE_ENTRY_N 0
#define TABLE_METADATA_N 1
#define INVALID_MSG -1

/* enums and structs */
typedef struct Metadata {
  String tableName;                // local name of the table
  int numCols;                    // number of columns the table holds
  char* fieldNames;               // names of each field, as an array of strings
  char* fieldUnits;               // units of each field, as an array of strings
  int numRows;                    // number of rows in the table
} Metadata;

/* functions */
// returns the type of message received by the datalogger, 
int validateMsg(char* buf) {
  // make sure the message is fenced by '%' characters
  if (buf[0] == '%') {
    for (int i = 1; buf[i]; i++) {
      // if the message is properly bounded, check what the message type is
      if (buf[i] == '%') {
        char header[] = {buf[1], buf[2], '\0'};
        if (strcmp(TABLE_ENTRY, header) == 0) {
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

// write table metadata to FLASH
int writeMetadataToFLASH() {
  return 0;
}

int readMetadataFromFLASH() {
  return 0;
}

// parse table metadata and compare with the FLASH stored data
// returns 1 if the FLASH-metadata is the same as the received data
//         0 if the FLASH-metadata does not match
//         -1 upon error  
int processTableMetadata(char* buf, Metadata mem) {
  String name;
  int cols;
  char* fields;
  char* units;

  char* rest = buf;
  char* token;

  Serial.println(buf);

  // get the name of the table
  name = String(strtok_r(&(buf[3]), ";", &rest));

  // get the number of columns
  char* colsStr = strtok_r(rest, ";", &rest);
  cols = (int)strtol(colsStr, &colsStr, 10);

  // malloc a flat array for each field name
  fields = (char*)malloc(cols*CR_FIELD_NAME_SIZE*sizeof(char*));
  if (fields == NULL){
    Serial.println("fields malloc error");
    return -1;
  }

  // malloc a flat array for each field name
  units = (char*)malloc(cols*CR_FIELD_NAME_SIZE*sizeof(char*));
  if (units == NULL){
    Serial.println("units malloc error");
    return -1;
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

  // compare received metadata to the FLASH metadata
  // is the table name the same?
  if (name != mem.tableName) 
  {
    Serial.println("Received name " + String(name) + " is not the same as FLASH-stored" + String(mem.tableName));
    return 0;
  }

  // is the number of columns the same?
  if (cols != mem.numCols) {
    Serial.println("Received number of columns " + String(cols) + " is not the same as FLASH-stored " + String(mem.numCols));
    return 0;
  }

  // are the field names the same? loop through to find out
  for (int i=0; i < cols; i++) {
    char* recvPtr = fields + i*CR_FIELD_NAME_SIZE;
    char* flshPtr = mem.fieldNames + i*CR_FIELD_NAME_SIZE;
    if (strcmp(recvPtr, flshPtr) != 0) {
      Serial.println("Received field name " + String(i) + " is " + String(recvPtr) + " when " + String(flshPtr) + " is expected.");
      return 0;
    }
  }
  // are the field units the same?
  for (int i=0; i < cols; i++) {
    char* recvPtr = units + i*CR_FIELD_NAME_SIZE;
    char* flshPtr = mem.fieldUnits + i*CR_FIELD_NAME_SIZE;
    if (strcmp(recvPtr, flshPtr) != 0) {
      Serial.println("Received field unit " + String(i) + " is " + String(recvPtr) + " when " + String(flshPtr) + " is expected.");
      return 0;
    }
  }

  // free up the array resources
  free(fields);
  free(units);
  return 1;

  // for debugging: print those mfers!
  // for (int i = 0; i < cols; i++) {
  //   char* curPtr = fields + i*CR_FIELD_NAME_SIZE;
  //   Serial.println(curPtr);
  // }

  // for (int i = 0; i < cols; i++) {
  //   char* curPtr = units + i*CR_FIELD_NAME_SIZE;
  //   Serial.println(curPtr);
  // }
}

/* variables */
// serial reading/writing variables
int bytesRead;                  // number of bytes read from the serial line
char writeBuf[BUFFER_SIZE];     // serial write buffer
char readBuf[BUFFER_SIZE];      // serial read buffer
int recvMsgType;                // integer representing type of message received by DL

// FLASH-related variables
// Adafruit_SPIFlash flash(&flashTransport);
// FatVolume fatfs;                // file system object from SdFat

// FLASH-stored data
Metadata* md;
// char* tableName;                // local name of the table 
// int numCols;                    // number of columns the table holds
// char** fieldNames;              // names of each field, as an array of strings
// char** fieldUnits;              // units of each field, as an array of strings
// int numRows;                    // number of rows in the table

bool tableMDValidated;          // boolean to track if table metadata has been processed yet

/* main program */
void setup() {
  // the table metadata received by the datalogger has not been validated yet
  tableMDValidated = false;
  // read table metadata from FLASH and store it in the program
  // numCols = 5;
  // fieldNames = Flash.getTableName;
  // tableName = Flash.getTableName;

  // establish serial communication
  Serial1.setFIFOSize(4096);
  Serial1.begin(9600);          // TX/RX pins <-> datalogger
  // wait for serial port to open
  while (!Serial1) {
    delay(100);
  }
  Serial.begin();               // USB <-> computer

  // // initialize flash library and check chip ID
  // if (!flash.begin()) {
  //   Serial.println(F("Error, failed to initialize flash chip!"));
  //   while (1) {
  //     yield();
  //   }
  // }
  // Serial.print(F("Flash chip JEDEC ID: 0x"));
  // Serial.println(flash.getJEDECID(), HEX);

  // // mount the filesystem
  // if (!fatfs.begin(&flash)) {
  //   Serial.println(F("Error, failed to mount newly formatted filesystem!"));
  //   Serial.println(
  //       F("Was the flash chip formatted with the SdFat_format example?"));
  //   while (1) {
  //     yield();
  //   }
  // }
  // Serial.println(F("Mounted filesystem!"));

  // // check if the `/data` directory exists, if not then create it
  // if (!fatfs.exists(DATA_DIR)) {
  //   Serial.println(F(DATA_DIR + " directory not found, creating..."));

  //   fatfs.mkdir(DATA_DIR);

  //   if (!fatfs.exists(DATA_DIR)) {
  //     Serial.println(F("Error, failed to create " DATA_DIR "directory!"));
  //     while (1) {
  //       yield();
  //     }
  //   } else {
  //     Serial.println(F("Created " DATA_DIR " directory!"));
  //   }
  // }

  // // check if the metadata json exists
  // // format and create metadata file, then re-open it as a read-only file
  // File32 metadataFile = fatfs.open((DATA_DIR METADATA_FNAME), FILE_READ);
  // if (!metadataFile) {
  //   Serial.println(F("Error, failed to open " METADATA_FNAME " for reading. Creating it now..."));
  //   metadataFile = fatfs.open((DATA_DIR METADATA_FNAME), FILE_WRITE)
  //   if (!metadataFile) {
  //     Serial.println(F("Error, failed to create " METADATA_FNAME " for writing. I'll just die now."));
  //     while (1) {
  //       yield();
  //     }
  //   }
  //   // then write JSON metadata outline to the file
  //   // createNewMetadata();
  //   metadataFile.close();
  //   metadataFile = fatfs.open((DATA_DIR METADATA_FNAME), FILE_READ);
  // }
  // Serial.println(F("Opened file " METADATA_FNAME " for reading..."));


  // setup Metadata struct
  md = new Metadata;
  //metadata = (Metadata)malloc(sizeof(Metadata));
  md->tableName = "Table1";
  md->numCols = 5;
  md->numRows = 0;

  md->fieldNames = (char*)malloc(md->numCols * CR_FIELD_NAME_SIZE);
  snprintf(md->fieldNames, CR_FIELD_NAME_SIZE, "%s", "RecNum");
  snprintf(md->fieldNames+CR_FIELD_NAME_SIZE, CR_FIELD_NAME_SIZE, "%s", "TimeStamp");
  snprintf(md->fieldNames+2*CR_FIELD_NAME_SIZE, CR_FIELD_NAME_SIZE, "%s", "BattV");
  snprintf(md->fieldNames+3*CR_FIELD_NAME_SIZE, CR_FIELD_NAME_SIZE, "%s", "PTemp_C");
  snprintf(md->fieldNames+4*CR_FIELD_NAME_SIZE, CR_FIELD_NAME_SIZE, "%s", "Temp_C");

  md->fieldUnits = (char*)malloc(md->numCols * CR_FIELD_NAME_SIZE);
  snprintf(md->fieldUnits, CR_FIELD_NAME_SIZE, "%s", "None");
  snprintf(md->fieldUnits+CR_FIELD_NAME_SIZE, CR_FIELD_NAME_SIZE, "%s", "None");
  snprintf(md->fieldUnits+2*CR_FIELD_NAME_SIZE, CR_FIELD_NAME_SIZE, "%s", "Volts");
  snprintf(md->fieldUnits+3*CR_FIELD_NAME_SIZE, CR_FIELD_NAME_SIZE, "%s", "Deg C");
  snprintf(md->fieldUnits+4*CR_FIELD_NAME_SIZE, CR_FIELD_NAME_SIZE, "%s", "Temp C");

  // parse out JSON metadata and store it in program memory
  // tableName = readTableNameFLASH(metadataFile);
  // numCols = readNumColsFLASH(metadataFile);
  // fieldNames = readFieldNamesFLASH(metadataFile);
  // fieldUnits = readFieldUnitsFLASH(metadataFile);
  // numRows = readNumRowsFLASH(metadataFile);

  // open CSV file for writing
  
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
    Serial.println("not validated");
    // serial-write request for table metadata to datalogger
    snprintf(writeBuf, 100, "%%%s%%", TABLE_METADATA);
    Serial1.write(writeBuf);
    Serial.println(writeBuf);

    // read and validate serial input from the DL
    bytesRead = Serial1.readBytes(readBuf, BUFFER_SIZE);
    recvMsgType = validateMsg(readBuf);

    if (recvMsgType == TABLE_METADATA_N) {
      Serial.println("table metadata msg");

      // once metadata has been handled, set tableMDValidated to true
      int res = processTableMetadata(readBuf, *md);
      if (res != -1) {
        Serial.println(String(res));
        tableMDValidated = true;
      }
    }
    else {
      Serial.println("not table metadata msg");
    }
  }
  else {
    recvMsgType = validateMsg(readBuf);

    if (recvMsgType != INVALID_MSG) {
      if (recvMsgType == TABLE_ENTRY_N) {
        // each entry should be equal to numRows+1
        // upon reception, the message should be parsed as a CSV row and the first piece of info extracted
        // should be the row number. if that is equal to the numRows+1, then accept it as a valid entry, 
        // increment numRows and save in FLASH and send an ACK for the newly increm'd numRows.  
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

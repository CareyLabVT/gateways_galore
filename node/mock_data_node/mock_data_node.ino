/* message formats
TABLE_ENTRY (req/ack by MCU, resp by DL): %TE[REC_NUM]%                                 (from MCU)
                                          %TE[REC_NUM],[TIMESTAMP],[DATA],[DATA],...%   (from DL)
                                          %TE%                                          (from DL - done)
TABLE_METADATA (req by MCU, resp by DL):  %TM%                                                                                                    (from MCU)
                                          %TM[TABLE_NAME];[NUM_COLS];[FIELD1],[FIELD2],[FIELD3],[FIELD4]...;[FIELD1_UNITS],[FIELD2_UNITS],...%    (from DL)
*/

/* JSON format

*/

/* libraries */
// FLASH support
#include "SdFat_Adafruit_Fork.h"
#include <SPI.h>
#include <Adafruit_SPIFlash.h>
#include "flash_config.h"             // for flashTransport definition

// JSON support for metadata
#include <ArduinoJson.h>

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
  String tableName;               // local name of the table
  String csvFilename;             // name of the CSV file (which may be different than the table name
                                  //                       due to duplicate-named tables on the DL)
  int dupCount;                   // a counter for duplicate table names for naming the csv file
  int numCols;                    // number of columns the table holds
  char* fieldNames;               // names of each field, as an array of strings
  char* fieldUnits;               // units of each field, as an array of strings
  int numRows;                    // number of rows in the table
} Metadata;

/* functions */

int readMetadataJSON(Metadata* md) {
  JsonDocument doc;
  // locate the JSON file from '/data/metadata.json'
  File32
  deserializeJson(doc, )
  // and deserialize it!
  
  md->tableName = "Table1";
  md->numCols = 5;
  md->numRows = 0;
  md->dupCount = 0;
  md->csvFilename = "Table1.csv";

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
  return 1;
}

// int writeMetadataJSON(Metadata* md) {
//  return 0;
// }

int compareMetadata(Metadata* md1, Metadata* md2) {
  // compare received metadata to the FLASH metadata
  // is the table name the same?
  if (md1->tableName != md2->tableName) 
  {
    Serial.println("Received name " + String(md1->tableName) + " is not the same as FLASH-stored" + String(md2->tableName));
    return 0;
  }

  // is the number of columns the same?
  if (md1->numCols != md2->numCols) {
    Serial.println("Received number of columns " + String(md1->numCols) + " is not the same as FLASH-stored " + String(md2->numCols));
    return 0;
  }

  // are the field names the same? loop through to find out
  for (int i=0; i < md1->numCols; i++) {
    char* recvPtr = md1->fieldNames + i*CR_FIELD_NAME_SIZE;
    char* flshPtr = md2->fieldNames + i*CR_FIELD_NAME_SIZE;
    if (strcmp(recvPtr, flshPtr) != 0) {
      Serial.println("Received field name " + String(i) + " is " + String(recvPtr) + " when " + String(flshPtr) + " is expected.");
      return 0;
    }
  }

  // are the field units the same?
  for (int i=0; i < md1->numCols; i++) {
    char* recvPtr = md1->fieldUnits + i*CR_FIELD_NAME_SIZE;
    char* flshPtr = md2->fieldUnits + i*CR_FIELD_NAME_SIZE;
    if (strcmp(recvPtr, flshPtr) != 0) {
      Serial.println("Received field unit " + String(i) + " is " + String(recvPtr) + " when " + String(flshPtr) + " is expected.");
      return 0;
    }
  }
  return 1;
}

/* input processing */

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

// parse table metadata and compare with the FLASH stored data
// returns 1 if the FLASH-metadata is the same as the received data
//         0 if the FLASH-metadata does not match
//         -1 upon error
int processTableMetadataMsg(char* buf, Metadata mem) {
  Metadata* recvMd = new Metadata;

  char* rest = buf;
  char* token;

  Serial.println(buf);

  // get the name of the table
  recvMd->tableName = String(strtok_r(&(buf[3]), ";", &rest));

  // get the number of columns
  char* colsStr = strtok_r(rest, ";", &rest);
  recvMd->numCols = (int)strtol(colsStr, &colsStr, 10);

  // malloc a flat array for each field name
  recvMd->fieldNames = (char*)malloc(recvMd->numCols*CR_FIELD_NAME_SIZE*sizeof(char*));
  if (recvMd->fieldNames == NULL){
    Serial.println("fields malloc error");
    return -1;
  }

  // malloc a flat array for each field name
  recvMd->fieldUnits = (char*)malloc(recvMd->numCols*CR_FIELD_NAME_SIZE*sizeof(char*));
  if (recvMd->fieldUnits == NULL){
    Serial.println("units malloc error");
    return -1;
  }

  // parse out all of the fields and store them in the array
  char* fieldsStr = strtok_r(rest, ";", &rest);
  char* field;
  char* fieldsPtr = fieldsStr;
  snprintf(recvMd->fieldNames, CR_FIELD_NAME_SIZE, "%s", "RecNum");
  snprintf((recvMd->fieldNames+CR_FIELD_NAME_SIZE), CR_FIELD_NAME_SIZE, "%s", "TimeStamp");
  for (int i = 2; (i < recvMd->numCols) && (field = strtok_r(fieldsPtr, ",", &fieldsPtr)); i++) {
    char* curPtr = recvMd->fieldNames + i*CR_FIELD_NAME_SIZE;
    snprintf(curPtr, CR_FIELD_NAME_SIZE, "%s", field);
  }

  // parse out all of the units and store them in respective array
  char* unitsStr = strtok_r(rest, "%", &rest);
  char* unit;
  char* unitsPtr = unitsStr;
  snprintf(recvMd->fieldUnits, CR_FIELD_NAME_SIZE, "%s", "None");
  snprintf((recvMd->fieldUnits+CR_FIELD_NAME_SIZE), CR_FIELD_NAME_SIZE, "%s", "None");
  for (int i = 2; (i < recvMd->numCols) && (unit = strtok_r(unitsPtr, ",", &unitsPtr)); i++) {
    char* curPtr = recvMd->fieldUnits + i*CR_FIELD_NAME_SIZE;
    snprintf(curPtr, CR_FIELD_NAME_SIZE, "%s", unit);
  }

  // compare the two metadata structs
  int compareResult = compareMetadata(&mem, recvMd);

  // if the result of the comparison is 0 (they are different), 
  // then we must update the metadata and create a new table
  if (compareResult == 0) {
    // create new metadata file... and do what with the old one?
    // things to consider: do we need to use the MD at all for LoRa transfer? 
    //                    - compareResult needs to have more returns for different cases, or it needs to be brought out to this function again
    //md->dupCount++; 
    mem.csvFilename = String(mem.tableName) + String(mem.dupCount) + ".csv"; 
    //snprintf(mem->csvFilename, (CR_TABLE_NAME_SIZE + 9), "%s%d.csv", mem->tableName, mem->dupCount);
  }
  else {
    mem.csvFilename = String(mem.tableName) + ".csv"; 
    //snprintf(mem->csvFilename, (CR_TABLE_NAME_SIZE + 4), "%s.csv", mem->tableName);
  }

  // free up the array resources
  free(recvMd->fieldNames);
  free(recvMd->fieldUnits);
  delete recvMd;
  return compareResult;
}

// handle a table entry message
// return 0 if the record number is not what is expected, 
// 1 if it is expected and has been successfully processed 
// and stored into memory
// or -1 if an error occured during processing
int processTableEntryMsg(char* buf, Metadata* md) {
  // %TE[REC_NUM],[TIMESTAMP],[DATA],[DATA],...%   (from DL)
  char* rest = buf;
  char* token;

  // get the record number
  char* recNumStr = strtok_r(&(buf[3]), ",", &rest);
  int recNum = (int)strtol(recNumStr, &recNumStr, 10);

  // only process the data if the record number is correct                                                // --- FLASH STUFF
  if (recNum == md->numRows) {
    // write to the CSV file referenced in the metadata struct
    
    // open CSV file from md

    // write a new line to it

    // close that dirty bastard
    Serial.println("Wrote " + String(buf) + " to " + String(md->csvFilename));
    md->numRows++;
    return 1;                                                                                             // --- END
  } 
  else {
    Serial.println(String(md->numRows+1) + " was expected, but received " + String(recNum));
  }

  return 0;
}

/* variables */
// serial reading/writing variables
int bytesRead;                  // number of bytes read from the serial line
char writeBuf[BUFFER_SIZE];     // serial write buffer
char readBuf[BUFFER_SIZE];      // serial read buffer
int recvMsgType;                // integer representing type of message received by DL

// FLASH-related variables
Adafruit_SPIFlash flash(&flashTransport);
FatVolume fatfs;                // file system object from SdFat

// FLASH-stored data
Metadata* md;

// boolean to track if table metadata has been processed yet
bool tableMDValidated;

// boolean to track if the datalogger is done sending table entries
bool entriesDone;

/* main program */
void setup() {
  // the table metadata received by the datalogger has not been validated yet
  tableMDValidated = false;
  entriesDone = false;

  // establish serial communication
  Serial1.setFIFOSize(4096);
  Serial1.begin(9600);          // TX/RX pins <-> datalogger
  // wait for serial port to open
  while (!Serial1) {
    delay(100);
  }
  Serial.begin();               // USB <-> computer

  // initialize flash library and check chip ID
  if (!flash.begin()) {
    Serial.println(F("Error, failed to initialize flash chip!"));
    while (1) {
      yield();
    }
  }
  Serial.print(F("Flash chip JEDEC ID: 0x"));
  Serial.println(flash.getJEDECID(), HEX);

  // mount the filesystem
  if (!fatfs.begin(&flash)) {
    Serial.println(F("Error, failed to mount newly formatted filesystem!"));
    Serial.println(
        F("Was the flash chip formatted with the SdFat_format example?"));
    while (1) {
      yield();
    }
  }
  Serial.println(F("Mounted filesystem!"));

  // check if the `/data` directory exists, if not then create it
  if (!fatfs.exists(DATA_DIR)) {
    Serial.println(F(DATA_DIR + " directory not found, creating..."));

    fatfs.mkdir(DATA_DIR);

    if (!fatfs.exists(DATA_DIR)) {
      Serial.println(F("Error, failed to create " DATA_DIR "directory!"));
      while (1) {
        yield();
      }
    } else {
      Serial.println(F("Created " DATA_DIR " directory!"));
    }
  }

  // check if the metadata json exists
  // format and create metadata file, then re-open it as a read-only file
  File32 metadataFile = fatfs.open((DATA_DIR METADATA_FNAME), FILE_READ);
  if (!metadataFile) {
    Serial.println(F("Error, failed to open " METADATA_FNAME " for reading. Creating it now..."));
    metadataFile = fatfs.open((DATA_DIR METADATA_FNAME), FILE_WRITE)
    if (!metadataFile) {
      Serial.println(F("Error, failed to create " METADATA_FNAME " for writing. I'll just die now."));
      while (1) {
        yield();
      }
    }
    // then write JSON metadata outline to the file
    // createNewMetadata();
    metadataFile.close();
    metadataFile = fatfs.open((DATA_DIR METADATA_FNAME), FILE_READ);
  }
  Serial.println(F("Opened file " METADATA_FNAME " for reading..."));


  // setup Metadata struct and read the JSON into the struct
  md = new Metadata;
  readMetadataJSON(md);

  // open CSV file for writing
  
  // 
}

void loop() {
  // loop 1: validate metadata
  while (!tableMDValidated) {
    // reset the serial write buffer
    memset(writeBuf, 0, sizeof(writeBuf)); // reset the buffer
    memset(readBuf, 0, sizeof(readBuf));
    recvMsgType = -1;
    
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
      int res = processTableMetadataMsg(readBuf, *md);
      if (res != -1) {
        Serial.println(String(res));
        // send table entry request
        tableMDValidated = true;
      }
    }
  }
  while (!entriesDone) {
    memset(writeBuf, 0, sizeof(writeBuf)); // reset the buffer
    memset(readBuf, 0, sizeof(readBuf));
    recvMsgType = -1;
    
    // send a request for the table entry at numRows + 1
    snprintf(writeBuf, 100, "%%%s%d%%", TABLE_ENTRY, md->numRows);
    Serial1.write(writeBuf);
    Serial.println(writeBuf);

    // read bytes from serial buffer
    bytesRead = Serial1.readBytes(readBuf, BUFFER_SIZE);

    Serial.println(readBuf);
    recvMsgType = validateMsg(readBuf);

    if (recvMsgType == TABLE_ENTRY_N) {
      Serial.println("table entry");
      if (readBuf[3] != '%') {
        // parse out the table entry, this will increment the recNum of md if correctly parsed
        int entryRes = processTableEntryMsg(readBuf, md);
      }
      else {
        entriesDone = true;
      }
    }
    
  }
  //memset(readBuf, 0, sizeof(readBuf)); // reset the buffer  
  // sleep time between DL requests and LoRa requests 
  delay(5000);
}

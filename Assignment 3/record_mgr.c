#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "record_mgr.h"
#include "buffer_mgr.h"
#include "storage_mgr.h"

// This is custom data structure defined for making the use of Record Manager.
typedef struct RecordManager
{
	// Buffer Manager's PageHandle for using Buffer Manager to access Page files
	BM_PageHandle pageHandle;	// Buffer Manager PageHandle 
	// Buffer Manager's Buffer Pool for using Buffer Manager	
	BM_BufferPool bufferPool;
	// Record ID	
	RID recordID;
	// This variable defines the condition for scanning the records in the table
	Expr *condition;
	// This variable stores the total number of tuples in the table
	int tuplesCount;
	// This variable stores the location of first free page which has empty slots in table
	int freePage;
	// This variable stores the count of the number of records scanned
	int scanCount;
} RecordManager;

const int MAX_NUMBER_OF_PAGES = 100;
const int ATTRIBUTE_SIZE = 15; // Size of the name of the attribute

RecordManager *recordManager;

// ******** CUSTOM FUNCTIONS ******** //

// This function returns a free slot within a page
int findFreeSlot(char *data, int recordSize)
{
	int i, totalSlots = PAGE_SIZE / recordSize; 

	for (i = 0; i < totalSlots; i++)
		if (data[i * recordSize] != '+')
			return i;
	return -1;
}


// ******** TABLE AND RECORD MANAGER FUNCTIONS ******** //

// This function initializes the Record Manager
extern RC initialize_RecordManager(void *managementData)
{
    // Initializing Storage Manager
    initialize_StorageManager();
    return RC_OK;
}

// This function shuts down the Record Manager
extern RC shutdown_RecordManager()
{
    recordHandler = NULL;
    free(recordHandler);
    return RC_OK;
}

// This function creates a TABLE with table name "tableName" having schema specified by "tableSchema"
extern RC create_Table(char *tableName, Schema *tableSchema)
{
    // Allocating memory space to the record handler custom data structure
    recordHandler = (RecordHandler*) malloc(sizeof(RecordHandler));

    // Initializing the Buffer Pool using LRU page replacement policy
    initialize_BufferPool(&recordHandler->bufferPool, tableName, MAX_NUMBER_OF_PAGES, RS_LRU, NULL);

    char data[PAGE_SIZE];
    char *pagePointer = data;

    int result, k;

    // Setting number of tuples to 0
    *(int*)pagePointer = 0;

    // Incrementing pointer by sizeof(int) because 0 is an integer
    pagePointer = pagePointer + sizeof(int);

    // Setting first page to 1 since 0th page if for schema and other meta data
    *(int*)pagePointer = 1;

    // Incrementing pointer by sizeof(int) because 1 is an integer
    pagePointer = pagePointer + sizeof(int);

    // Setting the number of attributes
    *(int*)pagePointer = tableSchema->numAttr;

    // Incrementing pointer by sizeof(int) because number of attributes is an integer
    pagePointer = pagePointer + sizeof(int);

    // Setting the Key Size of the attributes
    *(int*)pagePointer = tableSchema->keySize;

    // Incrementing pointer by sizeof(int) because Key Size of attributes is an integer
    pagePointer = pagePointer + sizeof(int);

    for(k = 0; k < tableSchema->numAttr; k++)
    {
        // Setting attribute name
        strncpy(pagePointer, tableSchema->attrNames[k], ATTRIBUTE_SIZE);
        pagePointer = pagePointer + ATTRIBUTE_SIZE;

        // Setting data type of attribute
        *(int*)pagePointer = (int)tableSchema->dataTypes[k];

        // Incrementing pointer by sizeof(int) because we have data type using integer constants
        pagePointer = pagePointer + sizeof(int);

        // Setting length of datatype of the attribute
        *(int*)pagePointer = (int) tableSchema->typeLength[k];

        // Incrementing pointer by sizeof(int) because type length is an integer
        pagePointer = pagePointer + sizeof(int);
    }

    SM_FileHandle fileHandler;

    // Creating a page file page name as table name using storage manager
    if((result = create_PageFile(tableName)) != RC_OK)
        return result;

    // Opening the newly created page
    if((result = open_PageFile(tableName, &fileHandler)) != RC_OK)
        return result;

    // Writing the schema to first location of the page file
    if((result = write_Block(0, &fileHandler, data)) != RC_OK)
        return result;

    // Closing the file after writing
    if((result = close_PageFile(&fileHandler)) != RC_OK)
        return result;

    return RC_OK;
}

// This function opens the table with table name "tableName"
extern RC open_Table(RM_TableData *tableData, char *tableName)
{
    SM_PageHandle pagePointer;

    int attributeCount, k;

    // Setting table's meta data to our custom record handler meta data structure
    tableData->mgmtData = recordHandler;
    // Setting the table's name
    tableData->name = tableName;

    // Pinning a page i.e. putting a page in Buffer Pool using Buffer Manager
    pin_Page(&recordHandler->bufferPool, &recordHandler->pageHandle, 0);

    // Setting the initial pointer (0th location) if the record handler's page data
    pagePointer = (char*) recordHandler->pageHandle.data;

    // Retrieving total number of tuples from the page file
    recordHandler->tuplesCount = *(int*)pagePointer;
    pagePointer = pagePointer + sizeof(int);

    // Getting free page from the page file
    recordHandler->freePage = *(int*) pagePointer;
    pagePointer = pagePointer + sizeof(int);

    // Getting the number of attributes from the page file
    attributeCount = *(int*)pagePointer;
    pagePointer = pagePointer + sizeof(int);

    Schema *tableSchema;

    // Allocating memory space to 'tableSchema'
    tableSchema = (Schema*) malloc(sizeof(Schema));

    // Setting schema's parameters
    tableSchema->numAttr = attributeCount;
    tableSchema->attrNames = (char**) malloc(sizeof(char*) *attributeCount);
    tableSchema->dataTypes = (DataType*) malloc(sizeof(DataType) *attributeCount);
    tableSchema->typeLength = (int*) malloc(sizeof(int) *attributeCount);

    // Allocate memory space for storing attribute name for each attribute
    for(k = 0; k < attributeCount; k++)
        tableSchema->attrNames[k] = (char*) malloc(ATTRIBUTE_SIZE);

    for(k = 0; k < tableSchema->numAttr; k++)
    {
        // Setting attribute name
        strncpy(tableSchema->attrNames[k], pagePointer, ATTRIBUTE_SIZE);
        pagePointer = pagePointer + ATTRIBUTE_SIZE;

        // Setting data type of attribute
        tableSchema->dataTypes[k] = *(int*) pagePointer;
        pagePointer = pagePointer + sizeof(int);

        // Setting length of datatype (length of STRING) of the attribute
        tableSchema->typeLength[k] = *(int*)pagePointer;
        pagePointer = pagePointer + sizeof(int);
    }

    // Setting newly created schema to the table's schema
    tableData->schema = tableSchema;

    // Unpinning the page i.e. removing it from Buffer Pool using Buffer Manager
    unpin_Page(&recordHandler->bufferPool, &recordHandler->pageHandle);

    // Write the page back to disk using Buffer Manager
    force_Page(&recordHandler->bufferPool, &recordHandler->pageHandle);

    return RC_OK;
}

// This function closes the table referenced by "tableData"
extern RC close_Table(RM_TableData *tableData)
{
    // Storing the Table's meta data
    RecordHandler *recordHandler = tableData->mgmtData;

    // Shutting down Buffer Pool
    shutdown_BufferPool(&recordHandler->bufferPool);
    //tableData->mgmtData = NULL;
    return RC_OK;
}


// This function deletes the table having table name "name"
extern RC deleteTable (char *name)
{
	// Removing the page file from memory using storage manager
	destroyPageFile(name);
	return RC_OK;
}

// This function returns the number of tuples (records) in the table referenced by "rel"
extern int getNumTuples (RM_TableData *rel)
{
	// Accessing our data structure's tuplesCount and returning it
	RecordManager *recordManager = rel->mgmtData;
	return recordManager->tuplesCount;
}


// ******** RECORD FUNCTIONS ******** //

 // Function: insertRecord
// Inserts a new record into the table and updates the 'record' parameter with the Record ID of the newly inserted record.
extern RC insertRecord(RM_TableData *rel, Record *record) {
    RecordManager *recordManager = rel->mgmtData;
    RID *recordID = &record->id;
    char *data, *slotPointer;
    int recordSize = getRecordSize(rel->schema);
    int freePageNum = recordManager->freePage;
    BM_PageHandle *page = &recordManager->pageHandle;
    BM_BufferPool *bm = &recordManager->bufferPool;

    // Ensuring freePageNum is valid
    if (freePageNum < 1) {
        return RC_ERROR;
    }
    // Attempting to pin the page and validating it
    if (pinPage(bm, page, freePageNum) != RC_OK) {
        return RC_ERROR;
    }

    data = page->data;

    //Update the page number in the recordID
    recordID->page = freePageNum;

    //Find a free slot and insert record
    recordID->slot = findFreeSlot(data, recordSize);

    //If no free slot is available in current page, iterate over the pages to find free slot
    while (recordID->slot == -1) {
        unpinPage(bm, page);
        recordID->page++;
        pinPage(bm, page, recordID->page);
        data = page->data;
        recordID->slot = findFreeSlot(data, recordSize);
    }
    slotPointer = data;
    //Marking the page as dirty
    markDirty(bm, page);

    slotPointer += recordID->slot * recordSize;

    *slotPointer = '+';

    memcpy(++slotPointer, record->data + 1, recordSize - 1);
    unpinPage(bm, page);

    // Increment the count of tuples in record manager
    recordManager->tuplesCount++;

    // Pin the page again
    pinPage(bm, page, 0);

    return RC_OK;
}

//Function: deleteRecord
// Deletes a record with the specified Record ID from the table.

extern RC deleteRecord(RM_TableData *rel, RID id) {
    RecordManager *recordManager = rel->mgmtData;
    BM_PageHandle *page = &recordManager->pageHandle;
    BM_BufferPool *bm = &recordManager->bufferPool;
    char *data;
    int recordSize = getRecordSize(rel->schema);

     // Attempting to pin the page and validating it
    if (pinPage(bm, page, id.page) != RC_OK) {
        return RC_ERROR;
    }

    // Updating the page number of free page
    recordManager->freePage = id.page;

    data = page->data + (id.slot * recordSize);

    *data = '-';

    //Marking the page as dirty
    markDirty(bm, page);

    unpinPage(bm, page);

    return RC_OK;
}

//Function: updateRecord
// * Updates a record with the new data in the table.

extern RC updateRecord(RM_TableData *rel, Record *record) {
    RecordManager *recordManager = rel->mgmtData;
    BM_PageHandle *page = &recordManager->pageHandle;
    BM_BufferPool *bm = &recordManager->bufferPool;
    char *data;
    int recordSize = getRecordSize(rel->schema);

     //Attempting to pin the page and validating it
    if (pinPage(bm, page, record->id.page) != RC_OK) {
        return RC_ERROR;
    }

    data = page->data + (record->id.slot * recordSize);
    *data = '+';
    memcpy(++data, record->data + 1, recordSize - 1);

    //Marking the page as dirty
    markDirty(bm, page);

    unpinPage(bm, page);

    return RC_OK;
}


// This function retrieves a record having Record ID "id" in the table referenced by "rel".
// The result record is stored in the location referenced by "record"
extern RC getRecord (RM_TableData *rel, RID id, Record *record)
{
	// Retrieving our meta data stored in the table
	RecordManager *recordManager = rel->mgmtData;
	
	// Pinning the page which has the record we want to retreive
	pinPage(&recordManager->bufferPool, &recordManager->pageHandle, id.page);

	// Getting the size of the record
	int recordSize = getRecordSize(rel->schema);
	char *dataPointer = recordManager->pageHandle.data;
	dataPointer = dataPointer + (id.slot * recordSize);
	
	if(*dataPointer != '+')
	{
		// Return error if no matching record for Record ID 'id' is found in the table
		return RC_RM_NO_TUPLE_WITH_GIVEN_RID;
	}
	else
	{
		// Setting the Record ID
		record->id = id;

		// Setting the pointer to data field of 'record' so that we can copy the data of the record
		char *data = record->data;

		// Copy data using C's function memcpy(...)
		memcpy(++data, dataPointer + 1, recordSize - 1);
	}

	// Unpin the page after the record is retrieved since the page is no longer required to be in memory
	unpinPage(&recordManager->bufferPool, &recordManager->pageHandle);

	return RC_OK;
}


// ******** SCAN FUNCTIONS ******** //

// This function scans all the records using the condition
extern RC startScan (RM_TableData *rel, RM_ScanHandle *scan, Expr *cond)
{
	// Checking if scan condition (test expression) is present
	if (cond == NULL)
	{
		return RC_SCAN_CONDITION_NOT_FOUND;
	}

	// Open the table in memory
	openTable(rel, "ScanTable");

    	RecordManager *scanManager;
	RecordManager *tableManager;

	// Allocating some memory to the scanManager
    	scanManager = (RecordManager*) malloc(sizeof(RecordManager));
    	
	// Setting the scan's meta data to our meta data
    	scan->mgmtData = scanManager;
    	
	// 1 to start scan from the first page
    	scanManager->recordID.page = 1;
    	
	// 0 to start scan from the first slot	
	scanManager->recordID.slot = 0;
	
	// 0 because this just initializing the scan. No records have been scanned yet    	
	scanManager->scanCount = 0;

	// Setting the scan condition
    	scanManager->condition = cond;
    	
	// Setting the our meta data to the table's meta data
    	tableManager = rel->mgmtData;

	// Setting the tuple count
    	tableManager->tuplesCount = ATTRIBUTE_SIZE;

	// Setting the scan's table i.e. the table which has to be scanned using the specified condition
    	scan->rel= rel;

	return RC_OK;
}

// This function scans each record in the table and stores the result record (record satisfying the condition)
// in the location pointed by  'record'.
extern RC next (RM_ScanHandle *scan, Record *record)
{
	// Initiliazing scan data
	RecordManager *scanManager = scan->mgmtData;
	RecordManager *tableManager = scan->rel->mgmtData;
    	Schema *schema = scan->rel->schema;
	
	// Checking if scan condition (test expression) is present
	if (scanManager->condition == NULL)
	{
		return RC_SCAN_CONDITION_NOT_FOUND;
	}

	Value *result = (Value *) malloc(sizeof(Value));
   
	char *data;
   	
	// Getting record size of the schema
	int recordSize = getRecordSize(schema);

	// Calculating Total number of slots
	int totalSlots = PAGE_SIZE / recordSize;

	// Getting Scan Count
	int scanCount = scanManager->scanCount;

	// Getting tuples count of the table
	int tuplesCount = tableManager->tuplesCount;

	// Checking if the table contains tuples. If the tables doesn't have tuple, then return respective message code
	if (tuplesCount == 0)
		return RC_RM_NO_MORE_TUPLES;

	// Iterate through the tuples
	while(scanCount <= tuplesCount)
	{  
		// If all the tuples have been scanned, execute this block
		if (scanCount <= 0)
		{
			// printf("INSIDE If scanCount <= 0 \n");
			// Set PAGE and SLOT to first position
			scanManager->recordID.page = 1;
			scanManager->recordID.slot = 0;
		}
		else
		{
			// printf("INSIDE Else scanCount <= 0 \n");
			scanManager->recordID.slot++;

			// If all the slots have been scanned execute this block
			if(scanManager->recordID.slot >= totalSlots)
			{
				scanManager->recordID.slot = 0;
				scanManager->recordID.page++;
			}
		}

		// Pinning the page i.e. putting the page in buffer pool
		pinPage(&tableManager->bufferPool, &scanManager->pageHandle, scanManager->recordID.page);
			
		// Retrieving the data of the page			
		data = scanManager->pageHandle.data;

		// Calulate the data location from record's slot and record size
		data = data + (scanManager->recordID.slot * recordSize);
		
		// Set the record's slot and page to scan manager's slot and page
		record->id.page = scanManager->recordID.page;
		record->id.slot = scanManager->recordID.slot;

		// Intialize the record data's first location
		char *dataPointer = record->data;

		// '-' is used for Tombstone mechanism.
		*dataPointer = '-';
		
		memcpy(++dataPointer, data + 1, recordSize - 1);

		// Increment scan count because we have scanned one record
		scanManager->scanCount++;
		scanCount++;

		// Test the record for the specified condition (test expression)
		evalExpr(record, schema, scanManager->condition, &result); 

		// v.boolV is TRUE if the record satisfies the condition
		if(result->v.boolV == TRUE)
		{
			// Unpin the page i.e. remove it from the buffer pool.
			unpinPage(&tableManager->bufferPool, &scanManager->pageHandle);
			// Return SUCCESS			
			return RC_OK;
		}
	}
	
	// Unpin the page i.e. remove it from the buffer pool.
	unpinPage(&tableManager->bufferPool, &scanManager->pageHandle);
	
	// Reset the Scan Manager's values
	scanManager->recordID.page = 1;
	scanManager->recordID.slot = 0;
	scanManager->scanCount = 0;
	
	// None of the tuple satisfy the condition and there are no more tuples to scan
	return RC_RM_NO_MORE_TUPLES;
}

// This function closes the scan operation.
extern RC closeScan (RM_ScanHandle *scan)
{
	RecordManager *scanManager = scan->mgmtData;
	RecordManager *recordManager = scan->rel->mgmtData;

	// Check if scan was incomplete
	if(scanManager->scanCount > 0)
	{
		// Unpin the page i.e. remove it from the buffer pool.
		unpinPage(&recordManager->bufferPool, &scanManager->pageHandle);
		
		// Reset the Scan Manager's values
		scanManager->scanCount = 0;
		scanManager->recordID.page = 1;
		scanManager->recordID.slot = 0;
	}
	
	// De-allocate all the memory space allocated to the scans's meta data (our custom structure)
    	scan->mgmtData = NULL;
    	free(scan->mgmtData);  
	
	return RC_OK;
}


// ******** SCHEMA FUNCTIONS ******** //

// This function returns the record size of the schema referenced by "schema"
extern int getRecordSize (Schema *schema)
{
	int size = 0, i; // offset set to zero
	
	// Iterating through all the attributes in the schema
	for(i = 0; i < schema->numAttr; i++)
	{
		switch(schema->dataTypes[i])
		{
			// Switch depending on DATA TYPE of the ATTRIBUTE
			case DT_STRING:
				// If attribute is STRING then size = typeLength (Defined Length of STRING)
				size = size + schema->typeLength[i];
				break;
			case DT_INT:
				// If attribute is INTEGER, then add size of INT
				size = size + sizeof(int);
				break;
			case DT_FLOAT:
				// If attribite is FLOAT, then add size of FLOAT
				size = size + sizeof(float);
				break;
			case DT_BOOL:
				// If attribite is BOOLEAN, then add size of BOOLEAN
				size = size + sizeof(bool);
				break;
		}
	}
	return ++size;
}

// This function is responsible for generating a new schema.
extern Schema *createSchema (int numAttr, char **attrNames, DataType *dataTypes, int *typeLength, int keySize, int *keys)
{
	// Declare a pointer schema to a Schema structure and allocate memory for it using the malloc function, allocating memory space equal to the size of a Schema structure.
	Schema *schema = (Schema *) malloc(sizeof(Schema));
	// Set the numAttr field of the schema	
	schema->numAttr = numAttr;
	// Set the Attribute Names field of the schema
	schema->attrNames = attrNames;
	// Specify the data types for the attributes in the newly created schema.
	schema->dataTypes = dataTypes;
	// Define the length of data types, specifically for strings, in the newly created schema.
	schema->typeLength = typeLength;
	// Determine the size of the key in the newly created schema.
	schema->keySize = keySize;
	// Define the attributes that constitute the key in the newly created schema.
	schema->keyAttrs = keys;

	return schema; 
}

// The following function is responsible for removing a schema from memory and releasing all the memory space that the schema occupies.
extern RC freeSchema (Schema *schema)
{
	// It begins by deallocating the memory space occupied by the 'schema' variable.
	free(schema);
	// After deallocating the memory, it returns a success status code, indicating that the operation was completed successfully.
	return RC_OK;
}


// ******** DEALING WITH RECORDS AND ATTRIBUTE VALUES ******** //

// This function creates a new record in the schema referenced by "schema"
extern RC createRecord (Record **record, Schema *schema)
{
	// Allocate memory space for a new record based on the provided schema.
	Record *newRecord = (Record*) malloc(sizeof(Record));
	
	// Determine the size of the record based on the schema.
	int recordSize = getRecordSize(schema);

	// Allocate memory space for the data of the new record.
	newRecord->data= (char*) malloc(recordSize);

	// Initialize the page and slot position of the new record as -1, indicating that it's a new record and its position is unknown.
	newRecord->id.page = newRecord->id.slot = -1;

	// Get the starting position in memory where the record's data will be stored.
	char *dataPointer = newRecord->data;
	
	// Set the initial character of the record's data to '-' as part of a Tombstone mechanism to indicate that the record is empty.
	*dataPointer = '-';
	
	// Append '\0' (NULL character in C) to the record's data after the Tombstone character.
	*(++dataPointer) = '\0';

	// Assign the newly created record to the pointer passed as an argument.
	*record = newRecord;
    
	//Return an OK status to indicate that the record creation was successful.
	return RC_OK;
}

// This function sets the offset (in bytes) from initial position to the specified attribute of the record into the 'result' parameter passed through the function
RC attrOffset (Schema *schema, int attrNum, int *result)
{
	//Initialize a counter variable i to 0 and set the result parameter to 1.
	int i = 0;
    *result = 1;

    // iterate through each attribute in the schema until reaching the specified attribute number attrNum.
    while (i < attrNum)
    {
        // For each attribute, check its data type using if-else statements.
        if (schema->dataTypes[i] == DT_STRING)
        {
            // If the attribute is of type STRING, add its defined length (typeLength) to the result.
            *result = *result + schema->typeLength[i];
        }
        else if (schema->dataTypes[i] == DT_INT)
        {
            // If the attribute is of type INTEGER, add the size of an integer (sizeof(int)) to the result.
            *result = *result + sizeof(int);
        }
        else if (schema->dataTypes[i] == DT_FLOAT)
        {
            // If the attribute is of type FLOAT, add the size of a float (sizeof(float)) to the result.
            *result = *result + sizeof(float);
        }
        else if (schema->dataTypes[i] == DT_BOOL)
        {
            // If the attribute is of type BOOLEAN, add the size of a boolean (sizeof(bool)) to the result.
            *result = *result + sizeof(bool);
        }
        
        // Increment the counter i to move to the next attribute in the schema.
        i++;
    }
    //Once all attributes up to the specified one have been processed, return RC_OK.
    return RC_OK;
}

// This function deallocates the memory used by a record.
extern RC freeRecord (Record *record)
{
	// Freeing the memory space previously allocated to the record.
	free(record);
	// Returning the status code indicating successful execution.
	return RC_OK;
}

// This function retrieves an attribute from the given record in the specified schema
extern RC getAttr (Record *record, Schema *schema, int attrNum, Value **value)
{
    int offset = 0;

    // Calculate the offset value of attributes based on the attribute number
    attrOffset(schema, attrNum, &offset);

    // Allocate memory space for the Value data structure where the attribute values will be stored
    Value *attribute = (Value*) malloc(sizeof(Value));

    // Determine the starting position of the record's data in memory
    char *dataPointer = record->data;
    
    // Add the calculated offset to the starting position
    dataPointer = dataPointer + offset;

    // If attribute number is 1, set its data type in the schema to 1
    if (attrNum == 1) {
        schema->dataTypes[attrNum] = 1;
    } else {
        schema->dataTypes[attrNum] = schema->dataTypes[attrNum];
    }
    
    // Retrieve attribute's value depending on its data type
    if (schema->dataTypes[attrNum] == DT_STRING) {
        // If the attribute is of type STRING
        int length = schema->typeLength[attrNum];
        // Allocate space for a string of size 'length'
        attribute->v.stringV = (char *) malloc(length + 1);

        // Copy the string from the location pointed by dataPointer and append '\0' to denote the end of the string in C
        strncpy(attribute->v.stringV, dataPointer, length);
        attribute->v.stringV[length] = '\0';
        attribute->dt = DT_STRING;
    } else if (schema->dataTypes[attrNum] == DT_INT) {
        // If the attribute is of type INTEGER
        int value = 0;
        // Copy the integer value from dataPointer
        memcpy(&value, dataPointer, sizeof(int));
        attribute->v.intV = value;
        attribute->dt = DT_INT;
    } else if (schema->dataTypes[attrNum] == DT_FLOAT) {
        // If the attribute is of type FLOAT
        float value;
        // Copy the float value from dataPointer
        memcpy(&value, dataPointer, sizeof(float));
        attribute->v.floatV = value;
        attribute->dt = DT_FLOAT;
    } else if (schema->dataTypes[attrNum] == DT_BOOL) {
        // If the attribute is of type BOOLEAN
        bool value;
        // Copy the boolean value from dataPointer
        memcpy(&value,dataPointer, sizeof(bool));
        attribute->v.boolV = value;
        attribute->dt = DT_BOOL;
    } else {
        // If the serializer is not defined for the given data type
        printf("Serializer not defined for the given datatype. \n");
    }

    // Set the pointer to the attribute value
    *value = attribute;
    return RC_OK;
}


// This function sets the attribute value in the record in the specified schema
extern RC setAttr (Record *record, Schema *schema, int attrNum, Value *value)
{
    int offset = 0;

    // Calculate the offset value based on the attribute number
    attrOffset(schema, attrNum, &offset);

    // Get the starting position of the record's data in memory
    char *dataPointer = record->data;

    // Add the offset to the starting position
    dataPointer = dataPointer + offset;

    // Check the data type of the attribute and set its value accordingly
    if (schema->dataTypes[attrNum] == DT_STRING) {
        // For attributes of type STRING
        // Get the length of the string defined in the schema
        int length = schema->typeLength[attrNum];

        // Copy the attribute's value to the location pointed by the record's data
        strncpy(dataPointer, value->v.stringV, length);
        dataPointer = dataPointer + schema->typeLength[attrNum];
    } else if (schema->dataTypes[attrNum] == DT_INT) {
        // For attributes of type INTEGER
        // Set the attribute value
        *(int *) dataPointer = value->v.intV;
        dataPointer = dataPointer + sizeof(int);
    } else if (schema->dataTypes[attrNum] == DT_FLOAT) {
        // For attributes of type FLOAT
        // Set the attribute value
        *(float *) dataPointer = value->v.floatV;
        dataPointer = dataPointer + sizeof(float);
    } else if (schema->dataTypes[attrNum] == DT_BOOL) {
        // For attributes of type BOOL
        // Set the attribute value
        *(bool *) dataPointer = value->v.boolV;
        dataPointer = dataPointer + sizeof(bool);
    } else {
        // Handle cases where serializer is not defined for the given data type
        printf("Serializer not defined for the given data type.\n");
    }

    return RC_OK;
}

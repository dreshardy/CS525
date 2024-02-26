#include "buffer_mgr.h"
#include "dberror.h"
#include "storage_mgr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RC_QUEUE_IS_EMPTY 5;
#define RC_NO_FREE_BUFFER_ERROR 6;

SM_FileHandle *fh;
// bufferSize: Represents the maximum number of page frames that can be stored in the buffer pool.
int bufferSize = 0;
int rearIndex = 0;
int writeCount = 0;
int hit = 0;
int clockPointer = 0;
int lfuPointer = 0;
/** 
rearIndex: Stores the count of the number of pages read from the disk. It is also utilized by the FIFO function to calculate the frontIndex.

writeCount: Counts the number of I/O writes to the disk, indicating the number of pages written to the disk.

hit: Serves as a general counter incremented whenever a page frame is added to the buffer pool. It is employed by the LRU algorithm to determine the least recently added page in the buffer pool.

clockPointer: Used by the CLOCK algorithm to point to the last added page in the buffer pool.

lfuPointer: Utilized by the LFU algorithm to store the position of the least frequently used page frame. It facilitates faster operations from the second replacement onwards.
**/


RC pinPageLRU(BM_BufferPool * const bm, BM_PageHandle * const page,
		const PageNumber pageNum);
RC pinPageFIFO(BM_BufferPool *const bm, BM_PageHandle *const page,const PageNumber pageNum);






// Buffer Manager Interface Pool Handling
extern RC initBufferPool(BM_BufferPool *const bm, const char *const pageFileName, const int numPages, ReplacementStrategy strategy,void *stratData)
{
        //valid inputs check
    if (bm == NULL || pageFileName == NULL || numPages <= 0) {
        return RC_INVALID_PARAMETER;
    }

    bm->pageFile = malloc(strlen(pageFileName) + 1);
    if (bm->pageFile == NULL) {
        return RC_BUFFER_POOL_INIT_ERROR;
    }
    strcpy(bm->pageFile, pageFileName);

    // Set other buffer pool properties
    bm->numPages = numPages;
    bm->strategy = strategy;

    PageFrame *pageFrames = malloc(sizeof(PageFrame) * numPages);
    if (pageFrames == NULL) {
        free(bm->pageFile);
        return RC_BUFFER_POOL_INIT_ERROR;
    }

    // Initialize page frames
    for (int i = 0; i < numPages; i++) {
        pageFrames[i].data = NULL;
        pageFrames[i].pageNum = -1;
        pageFrames[i].dirtyBit = 0;
        pageFrames[i].fixCount = 0;
        pageFrames[i].hitNum = 0;
        pageFrames[i].refNum = 0;
    }

    // Set buffer pool properties
    bm->mgmtData = pageFrames;

    // Additional initialization based on strategy or other parameters can be added here

    return RC_OK;
}
extern RC shutdownBufferPool(BM_BufferPool *const bm)
{
    if (bm == NULL || bm->mgmtData == NULL) {
        return RC_BUFFER_POOL_NOT_INIT;
    }

    // Check if there are any pinned pages in the buffer
    for (int i = 0; i < bm->numPages; i++) {
        if (((PageFrame *)(bm->mgmtData))[i].fixCount > 0) {
            return RC_PINNED_PAGES_IN_BUFFER;
        }
    }

    // Flush all dirty pages to disk before shutdown
    forceFlushPool(bm);

    // Free resources/memory space used by the buffer pool
    free(((PageFrame *)(bm->mgmtData)));
    free(bm->pageFile);
    bm->mgmtData = NULL;
    bm->pageFile = NULL;
    bm->numPages = 0;

    return RC_OK;
}
extern RC forceFlushPool(BM_BufferPool *const bm)
{
    if (bm == NULL || bm->mgmtData == NULL) {
        return RC_BUFFER_POOL_NOT_INIT;
    }

    FILE *fp = fopen(bm->pageFile, "r+");  // Open the page file for both reading and writing

    if (fp == NULL) {
        return RC_FILE_NOT_FOUND;
    }

    PageFrame *pageFrames = (PageFrame *)(bm->mgmtData);

    for (int i = 0; i < bm->numPages; i++) {
        if (pageFrames[i].dirtyBit == 1 && pageFrames[i].fixCount == 0) {
            // Write the dirty page to disk only if not in use (fixCount == 0)
            fseek(fp, PAGE_SIZE * pageFrames[i].pageNum, SEEK_SET);
            fwrite(pageFrames[i].data, 1, PAGE_SIZE, fp);
            pageFrames[i].dirtyBit = 0;  // Reset dirty bit after writing to disk
        }
    }

    fclose(fp);

    return RC_OK;
}

// Buffer Manager Interface Access Pages

// This function marks page as dirty indicating that the data of the page has been modified the client
extern RC markDirty(BM_BufferPool *const bm, BM_PageHandle *const page)
{
    PageFrame *pageFrame = (PageFrame *)bm->mgmtData;
    
    int i;
    // Iterating through all the pages in the buffer
    for(i = 0; i < bufferSize; i++)
    {
        // If the current page the page to be marked dirty, then set dirtyBit = 1 (page has been modified) for that page
        if(pageFrame[i].pageNum == page->pageNum)
        {
            pageFrame[i].dirtyBit = 1;
            return RC_OK;        
        }            
    }        
    return RC_ERROR;
}

// This function unpins a page from the memory i.e. removes a page from the memory
extern RC unpinPage(BM_BufferPool *const bm, BM_PageHandle *const page)
{   
    PageFrame *pageFrame = (PageFrame *)bm->mgmtData;
    
    int i;
    // Iterating through all the pages in the buffer pool
    for(i = 0; i < bufferSize; i++)
    {
        // If the current page is the page to be unpinned, then decrease fixCount (which means client has completed work on that page) and exit loop
        if(pageFrame[i].pageNum == page->pageNum)
        {
            pageFrame[i].fixCount--;
            break;      
        }       
    }
    return RC_OK;
}

// This function writes the contents of the modified pages back to the page file on disk
extern RC forcePage(BM_BufferPool *const bm, BM_PageHandle *const page)
{
    PageFrame *pageFrame = (PageFrame *)bm->mgmtData;
    
    int i;
    // Iterating through all the pages in the buffer pool
    for(i = 0; i < bufferSize; i++)
    {
        // If the current page = page to be written to disk, then right the page to the disk using the storage manager functions
        if(pageFrame[i].pageNum == page->pageNum)
        {       
            SM_FileHandle fh;
            openPageFile(bm->pageFile, &fh);
            writeBlock(pageFrame[i].pageNum, &fh, pageFrame[i].data);
        
            // Mark page as undirty because the modified page has been written to disk
            pageFrame[i].dirtyBit = 0;
            
            // Increase the writeCount which records the number of writes done by the buffer manager.
            writeCount++;
        }
    }   
    return RC_OK;
}


extern RC pinPage(BM_BufferPool *const bm, BM_PageHandle *const page, const PageNumber pageNum)
{
    PageFrame *pageFrame = (PageFrame *)bm->mgmtData;
    
    int i;
    // Checking if the page is already in the buffer pool
    for(i = 0; i < bufferSize; i++)
    {
        if(pageFrame[i].pageNum == pageNum)
        {
            pageFrame[i].fixCount++;
            pageFrame[i].refNum++;
            hitCount++;
            return RC_OK;
        }
    }
    
    // If the page is not in the buffer pool, then replace a page using the clock replacement strategy
    int clockIndex = rearIndex;
    do
    {
        clockIndex = (clockIndex + 1) % bufferSize;
        if(pageFrame[clockIndex].fixCount == 0)
        {
            // If the page is dirty, then write it to disk before replacing it
            if(pageFrame[clockIndex].dirtyBit == 1)
            {
                SM_FileHandle fh;
                openPageFile(bm->pageFile, &fh);
                writeBlock(pageFrame[clockIndex].pageNum, &fh, pageFrame[clockIndex].data);
                writeCount++;
            }
            
            // Reading the new page from disk and initializing page frame's content
            pageFrame[clockIndex].data = (SM_PageHandle) malloc(PAGE_SIZE);
            SM_FileHandle fh;
            openPageFile(bm->pageFile, &fh);
            ensureCapacity(pageNum,&fh);
            readBlock(pageNum, &fh, pageFrame[clockIndex].data);
            pageFrame[clockIndex].pageNum = pageNum;
            pageFrame[clockIndex].fixCount++;
            rearIndex = clockIndex;
            pageFrame[clockIndex].hitNum = hitCount;
            pageFrame[clockIndex].refNum = 0;
            page->pageNum = pageNum;
            page->data = pageFrame[clockIndex].data;
            return RC_OK;
        }
    } while(clockIndex != rearIndex);
    
    // If all pages are fixed, then return an error
    return RC_ERROR;
}


extern PageNumber *getFrameContents (BM_BufferPool *const bm)
{
	
}


extern bool *getDirtyFlags (BM_BufferPool *const bm)
{
	
}


/**
This function takes three parameters: pageFrame, fixCounts, and bufferSize.
It iterates over bufferSize elements of the pageFrame array.
For each element, it checks if the fixCount is not equal to -1. If true, it assigns the fixCount to the corresponding index of fixCounts. Otherwise, it assigns 0.
This function effectively calculates the fix counts for each page frame and stores them in the fixCounts array.
**/
void calculateFixCounts(PageFrame *pageFrame, int *fixCounts, int bufferSize) {
    int i = 0;
    while(i < bufferSize) {
        fixCounts[i] = (pageFrame[i].fixCount != -1) ? pageFrame[i].fixCount : 0;
        i++;
    }
}

/**
This function takes a single parameter bm, which is a pointer to a BM_BufferPool structure representing a buffer pool handler.
It allocates memory for an integer array fixCounts of size bufferSize, where bufferSize is a global variable or defined elsewhere.
It retrieves the pageFrame array from the bm buffer pool handler.
It then calls the calculateFixCounts function to compute the fix counts and populate the fixCounts array.
Finally, it returns the fixCounts array containing the fix counts for each page frame.
**/
extern int *getFixCounts(BM_BufferPool *const bm) {
    int *fixCounts = malloc(sizeof(int) * bufferSize);
    PageFrame *pageFrame = (PageFrame *)bm->mgmtData;
    calculateFixCounts(pageFrame, fixCounts, bufferSize);
    return fixCounts;
}


/**
This function, getNumReadIO, takes a single parameter bm, which is a pointer to a BM_BufferPool structure representing a buffer pool handler.
It returns the number of pages that have been read from disk since the buffer pool has been initialized.
The implementation returns the value of rearIndex incremented by one. This is because rearIndex tracks the number of pages read from disk, and adding one accounts for starting the index from zero.
**/
extern int getNumReadIO (BM_BufferPool *const bm)
{
    int pageread = rearIndex + 1;
	return pageread;
}

/**
The getNumWriteIO function takes a single parameter bm, which is a pointer to a BM_BufferPool structure representing a buffer pool handler.
It returns the number of write operations that have been performed since the buffer pool has been initialized.
The implementation directly returns the value of writeCount, which tracks the number of write operations. 
**/
extern int getNumWriteIO (BM_BufferPool *const bm)
{
	return writeCount;
}

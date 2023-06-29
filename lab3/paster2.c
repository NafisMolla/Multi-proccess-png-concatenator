#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <sys/queue.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include "lib/crc.h"
#include "lib/zutil.h"
#include <fcntl.h>

#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */
void catpng(U8 *all_png, int *height, int *width);

int buffer_size = 0;
int number_of_producers = 0;
int number_of_consumers = 0;
int consumer_sleep_time = 0;
int image_number = 0;


typedef unsigned char U8;
typedef unsigned int  U32;

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

//inflated data goes in here
typedef struct recv_buf_flat_2 {
    char buf[10000];       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF_2;

//curl data goes in here
typedef struct recv_buf_flat {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

//////////////////////////////////PNG FUNCTION EDITS /////////////////////////////////////

//THIS PUSHES TO THE STACK
int png_push(RECV_BUF_2 *p, RECV_BUF_2 item, int *position, int buffer_size)
{
    if ( p == NULL ) {
        return -1;
    }
    //if we are not full
    if ( *position != buffer_size) {
        //copy the item into the buffer
        memcpy(&(p[*position]), &item, sizeof(RECV_BUF_2));
        //update the postion
        ++*position;
        return 0;
    } else {
        return -1;
    }
}

int png_pop(RECV_BUF_2 *p, RECV_BUF_2 *p_item, int *position, int buffer_size)
{
    if ( p == NULL ) {
        return -1;
    }
    //if the buffer is not empty
    if ( *position > 0 ) {
        //update the postion
        --*position;
        //get the element
        memcpy(p_item, &(p[*position]), sizeof(RECV_BUF_2));
        return 0;
    } else {
        return 1; 
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;
    
    if (realsize > strlen(ECE252_HEADER) &&
	strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {

    /* extract img sequence number */
	p->seq = atoi(p_recv + strlen(ECE252_HEADER));

    }
    return realsize;
}

size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
 
    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);   
        char *q = realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}


int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
    void *p = NULL;
    
    if (ptr == NULL) {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL) {
	return 2;
    }
    
    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1;              /* valid seq should be non-negative */
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr)
{
    if (ptr == NULL) {
	    return 1;
    }
    
    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

///////////////////////////////PNG FUNCITON EDITS///////////////////////////////////////

void producer(RECV_BUF_2 *stack_pointer,sem_t *mutex, sem_t *spaces, sem_t *items, int *images_got, sem_t *images_got_mutex, int image_num, int image_part, int *position, int buffer_size){    
    
    CURL *curl_handle;
    CURLcode res;
    char url[256];
    RECV_BUF recv_buf;
    RECV_BUF_2 recv_buf_2;

    while(1){

        sem_wait(images_got_mutex);
        //which image we have to get
        int local_img_count = *images_got;
        
        //if we got all the images 
        if(local_img_count > 49){
            //making sure all the other proccess don't get blocked off
            sem_post(images_got_mutex);
            break;
        }
        //updating the images_got
        ++*images_got;
        sem_post(images_got_mutex);
        
        
        sem_wait(spaces);
        
        //GETTING THE URL
        sprintf(url, "http://ece252-%d.uwaterloo.ca:2530/image?img=%d&part=%d", image_part % 3 + 1, image_num, local_img_count);
        recv_buf_init(&recv_buf, BUF_SIZE);
        curl_global_init(CURL_GLOBAL_DEFAULT);

        // init a curl session
        curl_handle = curl_easy_init();

        if (curl_handle == NULL) {
            fprintf(stderr, "curl_easy_init: returned NULL\n");
        }
        
        // specify URL to get
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);

        // register write call back function to process received data
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3);
        // user defined data structure passed to the call back function 
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);

        // register header call back function to process received header data 
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);
        // user defined data structure passed to the call back function 
        curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);

        // some servers requires a user-agent field 
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        //get it!
        res = curl_easy_perform(curl_handle);

        //INTIALIZING BUF
        recv_buf_2.size = recv_buf.size;
        recv_buf_2.max_size = recv_buf.max_size;
        recv_buf_2.seq = local_img_count;
        memcpy(recv_buf_2.buf, recv_buf.buf, recv_buf_2.size);

        if( res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        
        //MUTEX START
        //adding to the buffer
        sem_wait(mutex);
        png_push(stack_pointer, recv_buf_2, position, buffer_size);
        sem_post(mutex);
        //MUTEX END

        curl_easy_cleanup(curl_handle);
        curl_global_cleanup();
        recv_buf_cleanup(&recv_buf);
        
        
        sem_post(items);
   }
}

void consumer(RECV_BUF_2 *stack_pointer, sem_t *mutex, sem_t * image_number_mutex, U8 *consumer_main_buffer, int *number_of_processed_images, sem_t *spaces, sem_t *items, int sleep, int *position, int buffer_size, int *height, int *width){

    while(1){

        sem_wait(image_number_mutex);
        int local_img_count = *number_of_processed_images;
        
        if(local_img_count > 49){
            sem_post(image_number_mutex);
            break;
        }
        ++*number_of_processed_images;
        sem_post(image_number_mutex);
        sem_wait(items);
        usleep(sleep * 1000);

        sem_wait(mutex); //Change mutex to semaphore with starting value of 1

        //alocating shared memory for png objects 
        RECV_BUF_2 *popped_item = malloc(sizeof(RECV_BUF_2));
        
        int pop_return = png_pop(stack_pointer,popped_item, position, buffer_size);
        
        //height of the current image
        U64 *single_image_height  = malloc(sizeof(U64));
        int local_global_height = *height;

        //if pop works
        if(pop_return == 0){

            //Get image width
            memcpy(width, popped_item->buf + 16, 4);
            //Get image height of one single piece
            //need to read 4 bytes for the height,
            memcpy(single_image_height,popped_item->buf+20,4);
            //add the global height to the local image height
            *single_image_height = ntohl(*single_image_height) +  local_global_height;
            //copying local height to total height
            memcpy(height, single_image_height, sizeof(U64));

            U64 idat_len;
            memcpy(&idat_len, popped_item->buf + 33, 4);
            idat_len = ntohl(idat_len);

            U8 idat_buffer[idat_len];
            memcpy(&idat_buffer, popped_item->buf + 41, idat_len);

            U8 inflated_buf[1024*1024];
            U64 dest_len = 0;
            int ret = mem_inf(inflated_buf, &dest_len, idat_buffer, idat_len);
            if(ret != 0){
                printf("Inflate failed\n");
            }

            memcpy(consumer_main_buffer + (9606 * popped_item->seq), inflated_buf, dest_len);
            //free the local memory 
            free(popped_item);
            free(single_image_height);
        }
        sem_post(mutex);

        sem_post(spaces);
    }

}


int main( int argc, char** argv ) {
    //argument info
    buffer_size = atoi(argv[1]);
    number_of_producers = atoi(argv[2]);
    number_of_consumers = atoi(argv[3]);
    consumer_sleep_time = atoi(argv[4]);
    image_number = atoi(argv[5]);

    //Process info
    pid_t pid=0;
    //pid's for producers
    pid_t producer_ids[number_of_producers];
    //pid's for consumers
    pid_t consumer_ids[number_of_consumers];

    //timer info
    double times[2];
    struct timeval tv;

    //this is the result for the wait
    int fork_result;
//----------------------------------------------------------------------------------------------------------------------------------------------------
    //Create shared memory in the form of a stack that is used for communication between producer and consumer
    RECV_BUF_2 *stack_pointer = mmap(NULL, buffer_size*sizeof(RECV_BUF_2), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);

    int *array_position = mmap(NULL,sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);
    *array_position = 0;

    //creating a shared memory between the main and the consumer processes
    U8 *consumer_main_buffer = mmap(NULL, 9606 * 50 + 1000, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);

    //memory shared mutex (This is used for accessing the fixed buffer
    sem_t *mutex = mmap(NULL, sizeof(pthread_mutex_t),PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0 );
    sem_init(mutex,1,1);
   
    //Shared memory counter to keep track of how many images have been processed
    int *counter = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);   
    *counter = 0;
    //Mutex for keeping track of how many images have been processed
    sem_t *image_number_mutex = mmap(NULL, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0 );
    sem_init(image_number_mutex,1,1);

    //shared memoroy for which image the curl request will get
    int *img_part = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);   
    *img_part = 0;
    //mutex to lock/unlock variable for which image we are getting from server
    sem_t *get_this_image = mmap(NULL, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0 );
    sem_init(get_this_image,1,1);


    //Create shared memory semaphores
    //semaphore for spaces in shared memory
    sem_t *spaces = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);
    sem_init(spaces,1, buffer_size);
    //semaphore for items in shared memory
    sem_t *items = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0);
    sem_init(items, 1, 0);

    //Shared memory for width and height of IHDR
    int *height = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0); 
    *height = 0;
    int *width = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0); 
    *width = 0;

//----------------------------------------------------------------------------------------------------------------------------------------------------

    //if the timing module fails, we print the error
    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    //this is the intial time
    times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;

    //PRODUCER
    for (int i = 0; i < number_of_producers; i++) {
        pid = fork();
        if ( pid > 0 ) {        // parent proc 
            producer_ids[i] = pid;
        } else if ( pid == 0 ) { // producer proc 
            producer(stack_pointer, mutex, spaces, items, img_part, image_number_mutex,image_number, i, array_position, buffer_size);
            exit(0);
        } else {
            perror("fork");
            abort();
        } 
    }

    //Fill up consumer_id array
    //CONSUMER
    for (int i = 0; i < number_of_consumers; i++) {
        pid = fork();
        if ( pid > 0 ) {        // parent proc 
            consumer_ids[i] = pid;
        } else if ( pid == 0 ) { // consumer proc
            consumer(stack_pointer, mutex, image_number_mutex, consumer_main_buffer, counter, spaces, items, consumer_sleep_time, array_position, buffer_size, height, width);
            exit(0);
        } else {
            perror("fork");
            abort();
        }
    }

    if(pid > 0){
        for(int i = 0; i < number_of_consumers; ++i){
            waitpid(consumer_ids[i],&fork_result,0);
            // if(WIFEXITED(fork_result)){}
        }

        for(int i = 0; i < number_of_producers; ++i){
            waitpid(producer_ids[i],&fork_result,0);
            // if(WIFEXITED(fork_result)){}
        }
    }

    catpng(consumer_main_buffer, height, width);
    
    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }

    //FREE THE SHARED MEMORY
    munmap(stack_pointer, buffer_size*sizeof(RECV_BUF_2));
    
    //end time
    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
    // printing the difference which is the total time
    printf("paster2 execution time: %f seconds\n", times[1]-times[0]);
    
    return 0;
}


void catpng(U8 *all_png, int *height, int *width){

    //Save total height of all png arguments for when we write all.png
    //holds the postion of where the next empty spot will be in Idat data
    int index_buffer = 0;
    //this is where we hold all our idata bytes
    U8 buffer_inf[4000000];
    
    //Start of deflation
    U8 *all_png_compressed_IDAT = malloc(4000000);
    U32 compress_len = 0;
    //use mem_def to compress buffer_inf into an IDAT that can be stored in all.png
    int ret = mem_def(all_png_compressed_IDAT, &compress_len, all_png, 9606*50, Z_DEFAULT_COMPRESSION);
    if (ret == 0) { // success
        // printf("%s\n","Passed deflation");
    } else { // failure
        fprintf(stderr,"mem_def failed. ret = %d.\n", ret);
    }
    
    
    U8 *giant_array = malloc(compress_len + 57);
    FILE *write_ptr = fopen("all.png","wb");
    //write the header
    giant_array[0] = 0x89;
    giant_array[1] = 0x50;
    giant_array[2] = 0x4E;
    giant_array[3] = 0x47;
    giant_array[4] = 0x0D;
    giant_array[5] = 0x0A;
    giant_array[6] = 0x1A;
    giant_array[7] = 0x0A;
    
    ////////////////////////////////IHDR///////////////////////////////////
    //we are writting the length of ihdr here
    U32 Ihdr_len = 13;
    Ihdr_len = ntohl(Ihdr_len); //may need to remove
    //gets the base address of the array, then we add 8 so we can get the the ihdr start, we then read 4 bytes 
    memcpy(giant_array+8,&Ihdr_len,4);
    
    //Write type of ihdr here
    //writting the type from byte 12-15
    giant_array[12] = 'I';
    giant_array[13] = 'H';
    giant_array[14] = 'D';
    giant_array[15] = 'R';
    
    
    //Write entire IHDR data chunk from sample image into giant array
    memcpy(giant_array+16, width, sizeof(int));
    //convert to big endian
    *height = htonl(*height); //may have to remove ... but we didnt 
    //Change the height within that copied IHDR data chunk into the true total height
    memcpy(giant_array+20,height,sizeof(height));
    giant_array[24] = 8;
    giant_array[25] = 6;
    giant_array[26] = 0;
    giant_array[27] = 0;
    giant_array[28] = 0;
    
    
    //IHDR CRC
    unsigned char *temp_IHDR_CRC[17];
    //CRC starts at position 29
    memcpy(temp_IHDR_CRC,giant_array+12,17);
    U64 crc_return = crc(temp_IHDR_CRC,17);
    crc_return = ntohl(crc_return);  //may need to remove
    //Copy that data into IHDR_chunk_CRC
    //Copy IHDR_chunk_CRC into our giant array
    memcpy(giant_array+29,&crc_return,4);
    
    
    ///////////////////////////////////IDAT////////////////////////////////////////////

    //IDAT LENGTH
    compress_len = ntohl(compress_len); //may need to remove
    U8 *new_length = (U8*)&compress_len;
    memcpy(giant_array+33,new_length,4);
    

    //idat type 
    giant_array[37] = 'I';
    giant_array[38] = 'D';
    giant_array[39] = 'A';
    giant_array[40] = 'T';

    
    //Write current data (up to idat type) into all.png
    fwrite(giant_array,1,41,write_ptr);
    
    //Write compressed IDAT to all.png
    compress_len = ntohl(compress_len); //might have to remove
    fwrite(all_png_compressed_IDAT,1,compress_len,write_ptr);

    
    unsigned char CRC_TYPE_DATA_BUFFER[compress_len+4];
    // writing into the buffer for crc type and data
    //adding the idata into the crc_type_data_buffer
    CRC_TYPE_DATA_BUFFER[0] = 'I';
    CRC_TYPE_DATA_BUFFER[1] = 'D';
    CRC_TYPE_DATA_BUFFER[2] = 'A';
    CRC_TYPE_DATA_BUFFER[3] = 'T';

    //adding the compressed data into the buffer
    memcpy(CRC_TYPE_DATA_BUFFER+4, all_png_compressed_IDAT,compress_len);
    
    //ADDING TO THE CRC_TYPE_DATA_BUFFER
    U32 crc_for_idat = crc(CRC_TYPE_DATA_BUFFER,compress_len+4);
    //writting to the all.png file
    crc_for_idat = ntohl(crc_for_idat);
    fwrite(&crc_for_idat,4,1,write_ptr);

    
    /////////////////////////////////////////IEND////////////////////
    
    U8 *IEND_array = malloc(12);
    IEND_array[0] = 0;
    IEND_array[1] = 0;
    IEND_array[2] = 0;
    IEND_array[3] = 0;
    IEND_array[4] = 'I';
    IEND_array[5] = 'E';
    IEND_array[6] = 'N';
    IEND_array[7] = 'D';
    IEND_array[8] = 174;
    IEND_array[9] = 66;
    IEND_array[10] = 96;
    IEND_array[11] = 130;

    fwrite(IEND_array,12,1,write_ptr);

    fclose(write_ptr);
    free(giant_array);
    free(all_png_compressed_IDAT);
    free(IEND_array);
}
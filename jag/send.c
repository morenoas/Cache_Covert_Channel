#include <stdio.h>
#include <stdlib.h>
#include "send.h"
#include "common.h"
#include "../cache/evict.h"
#include "../cjag.h"

void jag_send(cjag_config_t* config, jag_callback_t cb) {   /// selects the channels
    int got = 0;
    const int detection_threshold = 300;

    volatile uint8_t** addrs;
    if(!config->addr) {
        addrs = malloc(config->cache_ways * config->channels * sizeof(void*));
        for (int i = 0; i < config->channels; i++) {
            for (int j = 0; j < config->cache_ways; j++) {
                addrs[i * config->cache_ways + j] = ((uint8_t**)(config->cache_sets))[(i + config->set_offset) * config->cache_ways + j];
            }
        }
        config->addr = (void**)addrs;
    } else {
        addrs = (volatile uint8_t**)config->addr;
    }

    for (int i = 0; i < config->channels; i++) //
    {
        while (1) {
            for (int j = 0; j < config->jag_send_count; ++j) {
                evict_set(addrs + (i * config->cache_ways), config->cache_kill_count);
            }
            uint32_t misses = jag_check_set(addrs + (i * config->cache_ways), detection_threshold, config->jag_send_count * 2, config);
            if (misses >= detection_threshold) {
                got++;
                if(cb) {
                    cb(config, got);
                }
                break;
            }
        }
    }
}


char* readFile (char* fileName,long* filelen){
    FILE *fileptr;
    char *buffer;

    fileptr = fopen(fileName, "r");  
    if (fileptr == NULL){
        printf("File not found\n");
        exit(1);
    }
    fseek(fileptr, 0, SEEK_END);         
    *filelen = ftell(fileptr);             
    rewind(fileptr);                 
    buffer = (char *)malloc((*filelen) * sizeof(char)); 
    int temp = fread(buffer, *filelen, 1, fileptr); 
    if (temp != 1){
        printf("Error in readFile\n");
        exit(1);
    }
    fclose(fileptr);
    return buffer;
}



void jag_sendFile(cjag_config_t* config, jag_callback_t cb) {   
    int debug = 0;
    volatile uint8_t** addrs; //19 channels
    addrs = (volatile uint8_t**)config->addr;
    long filelen;
    char *buffer = readFile("input.txt",&filelen);

    int bitsSent = 0, countSent = 0;
    int seqDec = 1;
    int zerosCount = 0;
    int* seq = (int*)calloc(3,sizeof(int));
    int* nextSeq = (int*)calloc(3,sizeof (int)),*curSeq = (int*)calloc(3,sizeof (int));
    int* EDC = (int*)calloc(4,sizeof(int));

    const int detection_threshold = 300;
    int word[config->channels];
    int connect = 0;
    clock_t begin = 0;


    if(filelen==0){
        printf("empty File\n");
        exit(1);
    }

    decToBinary(seqDec+1,3,nextSeq);
    decToBinary(seqDec,3,curSeq);

    //Resetting the shared clock
    while (seq[0]==0&&seq[1]==0&&seq[2]==0)
    {
        for (int i=0;i<3;i++){
            uint32_t misses = jag_check_set(addrs + (i * config->cache_ways), detection_threshold, config->jag_send_count / 16, config);
            if (misses >= detection_threshold)
                seq[i] = 1;
        }
    }
    while (seq[0]==1&&seq[1]==1&&seq[2]==1){
        for (int i=0;i<3;i++){
            seq[i]=0;
            uint32_t misses = jag_check_set(addrs + (i * config->cache_ways), detection_threshold, config->jag_send_count /16, config);
            if (misses >= detection_threshold)
                seq[i] = 1;
        }
    }
    uint64_t time = rdtscp64();
    uint32_t cyclesToWait = slot;
    //End of reset

    fprintf(stderr,"sender start send file:\n");
    while (1)
    {
        delayloop(time,cyclesToWait);
        cyclesToWait += slot;
        for (int i=0;i<3;i++){
        // gets initial sequence from receiver
            seq[i]=0;
            uint32_t misses = jag_check_set(addrs + (i * config->cache_ways), detection_threshold, config->jag_send_count * 2, config);
            if (misses >= detection_threshold) {
                seq[i] = 1;
            }
        }
        if (debug){
            printf("sender get seq: ");
            for (int i = 0; i < 3; ++i) {
                printf("%d",seq[i]);
            }
            printf("\n");
        }
        if(equalBinary(seq,curSeq,3))
            break;
    }

    while (bitsSent<filelen) {
        zerosCount = 0;
        for (int i=0,k=0,l=0;i<config->channels;i++){   
            if (i < config->channels - 7){  
            // reading 12 bytes from file into word and counting '0'
                word[i] = buffer[k++] - '0';
                if (word[i] == 0)
                    zerosCount++;
            }
            else if (i >= config->channels - 7 && i < config->channels - 4) 
            // writting 3 bytes sequence into word
                word[i] = curSeq[l++];
            else 
                break;
        }
        decToBinary(zerosCount,4,EDC);
        for (int j=0, i = config->channels - 4 ; i<config->channels;i++,j++) 
        // writting 4 bytes EDC ('0' count) into word
            word[i] = EDC[j];

        if (debug){
            printf("sender send message: ");
            for(int i=0;i<config->channels;i++)
                printf("%d",word[i]);
            printf("\n");
        }

        while (1) {
            delayloop(time,cyclesToWait);
            cyclesToWait += slot;
            for (int k = 0; k < config->channels; k++)
                for (int i = 0; i < config->channels; i++) { // sends word
                    if (word[i] == 1)
                        for (int j = 0; j < config->jag_send_count / 32; ++j)
                            evict_set(addrs + (i * config->cache_ways), config->cache_kill_count);
                    else
                        for (int j = 0; j < config->jag_send_count / 32; ++j);
                }

            for (int i = 0; i < 3; ++i) {
            // initiates seq to be ready to get next one
                seq[i]=0;
            }

            while (seq[0]==0&&seq[1]==0&&seq[2]==0){
                delayloop(time,cyclesToWait);
                cyclesToWait += slot;
                for (int l=0;l<5;l++)
                    for (int i = 0; i < 3; i++) {
                    // receives next sequence from receiver
                        uint32_t misses = jag_check_set(addrs + (i * config->cache_ways), detection_threshold,
                                                        config->jag_recv_count /16 , config);
                        if (misses >= detection_threshold) {
                            seq[i] = 1;
                        }
                    }
            }
            if (debug){
                printf("sender get seq:");
                for (int i = 0; i < 3; ++i) {
                    printf("%d",seq[i]);
                }
                printf("\n");
            }
            if (equalBinary(seq, nextSeq, 3))
            // case sender gets the right sequence
                break;
        }
        if (!connect){
            connect = 1;
            begin = clock(); // begin measuring communication time
        }
        if (!debug)
            fprintf(stderr,"#");
        
        bitsSent+=config->channels -7;
        buffer+=config->channels-7;
        countSent += config->channels;
        seqDec++;
        if(seqDec==7)
            seqDec=0;
        int* temp = curSeq;
        curSeq = nextSeq;
        nextSeq = temp;
        decToBinary(seqDec+1,3,nextSeq);
    }
    fprintf(stderr,"\n");
    clock_t end = clock();
    double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    printf("The sender is finished\nspeed: %lf bits per second\n",countSent/time_spent);
    free(seq);



}

#include <stdint.h>
#include <stdlib.h>
#include "receive.h"
#include "common.h"
#include "../cache/evict.h"
#include "../cjag.h"

int errors = 0;
int bits = 0;

void jag_receive(void **ret_addrs, size_t* recv_sets, cjag_config_t *config, jag_callback_t cb) {   /// selects the channels
    uint8_t received_set_count = 0;
    uint16_t *received_sets = calloc(32 * config->cache_slices, sizeof(uint16_t));
    const int detection_threshold = 300;
    volatile uint8_t **all_addrs = (volatile uint8_t**)config->cache_sets;

    while (received_set_count != config->channels) {
        for (int i = 0; i < 32 * config->cache_slices; i++) {
            uint32_t misses = jag_check_set(all_addrs + i * config->cache_ways, detection_threshold, config->jag_send_count * 2, config);

            if (misses >= detection_threshold) {
                for (int j = 0; j < config->jag_recv_count; ++j) {
                    evict_set(all_addrs + i * config->cache_ways, config->cache_ways);
                }

                if (!received_sets[i]) {
                    for (int j = 0; j < config->cache_ways; ++j) {
                        ret_addrs[(received_set_count) * config->cache_ways + j] = (void*)all_addrs[i * config->cache_ways + j];
                    }

                    received_set_count++;
                    if (cb) {
                        if(recv_sets) {
                            recv_sets[received_set_count - 1] = i;
                        }
                        cb(config, received_set_count);
                    }
                }
                received_sets[i]++;

                //jam the last set some more because there is no feedback
                if (received_set_count == config->channels) {
                    for (int j = 0; j < (int)(config->jag_recv_count * 1.5); ++j) {
                        evict_set(all_addrs + i * config->cache_ways, config->cache_kill_count);
                    }
                    break;
                }

            }
        }
    }
    free(received_sets);
}


void receiverTimeout(void *arg) {
    FILE *output = (FILE *) arg;
    printf("The sender stopped sending bits so the program ended\n");
    printf("Error rate:%f\n",((double)errors)/bits);
    fclose(output);
    exit(1);
}


void jag_receiveBit(void **addrs, cjag_config_t *config, jag_callback_t cb) {
    int debug = 0;
    int flagOutput = 1;

    const int detection_threshold = 300;
    volatile uint8_t **all_addrs = (volatile uint8_t**)addrs;
    int seqDec = 1;
    int mesNum = 0; // general word number
    int zerosCount = 0;
    int* EDC = (int*)calloc(4,sizeof(int));
    int* zerosCountBin = (int*)calloc(4,sizeof(int));
    int* seq = calloc(3,sizeof (int));
    decToBinary(seqDec,3,seq);
    int* word = calloc(config->channels,sizeof(int));

    FILE *output = fopen("output.txt", "w+");
    FILE *incorrectMessages = fopen("incorrect messages.txt", "w+");
    watchdog_t watchdog_settings;

    //Resetting the shared clock
    for (int l=0;l<100;l++)
        for (int i = 0; i < 3; i++) {
                for (int j = 0; j < config->jag_send_count * 2; ++j)
                    evict_set(all_addrs + (i * config->cache_ways), config->cache_kill_count);
        }
    
    uint64_t time = rdtscp64();
    uint32_t cyclesToWait = slot;
    //End of reset

    watchdog_start(&watchdog_settings, 3, receiverTimeout, (void *) output);
    printf("The receiver starts receiving the file:\n");

    while (1) {
        watchdog_reset(&watchdog_settings);
        while (1) {
            if (debug){
                printf("receiver send seq:");
                for (int i = 0; i < 3; ++i) {
                    printf("%d",seq[i]);
                }
                printf("\n");
            }
            delayloop(time,cyclesToWait);
            cyclesToWait += slot;
            for (int l=0;l<10;l++)
                for (int i = 0; i < 3; i++) {
                // sends sequences in order
                    if (seq[i] == 1)
                        for (int j = 0; j < config->jag_send_count /16; ++j)
                            evict_set(all_addrs + (i * config->cache_ways), config->cache_kill_count);
                    else
                        for (int j = 0; j < config->jag_send_count /16; ++j);
                }

            for (int i = 0; i < config->channels; ++i) {
            // initiates word to be ready to get next one
                word[i]=0;
            }
            delayloop(time,cyclesToWait);
            cyclesToWait += slot;
            for (int l = 0; l < 3; ++l)
                for (int i = 0; i < config->channels; i++) { // receives word
                    uint32_t misses = jag_check_set(all_addrs + (i * config->cache_ways), detection_threshold,
                                                    config->jag_recv_count /16, config);
                    if (misses >= detection_threshold) {
                        word[i] = 1;
                    }
                }
            if (debug){
                printf("receiver get  seq:");
                for (int i = 0; i < 3; ++i) {
                    printf("%d",(word+config->channels-7)[i]);
                }
                printf("\n");
            }

            if (equalBinary(word+config->channels-7, seq,3)){
            // case receiver gets the right sequence
                zerosCount = 0;
                bits += 12;
                mesNum++;
                for (int i = 0; i < 12; ++i) {
                // counts number of '0' in the message
                    if(word[i]==0)
                        zerosCount++;
                }
                for (int j=0, i = config->channels - 4 ; i<config->channels;i++,j++)
                    EDC[j] = word[i];
                    
                decToBinary(zerosCount,4,zerosCountBin);// transforms zerosCount to binary for later comparison

                if(debug){
                    printf("receiver get  message:");
                    for (int i = 0; i < config->channels; ++i) {
                        printf("%d",word[i]);
                    }
                    printf("\n");
                }

                if(!equalBinary(zerosCountBin, EDC,4)){ 
                // writing incorrect messages into file for error correction stage
                    if (binaryToDec(EDC,4) - zerosCount < 0)
                        errors += zerosCount - binaryToDec(EDC,4);
                    else
                        errors += binaryToDec(EDC,4) - zerosCount;
                    if (flagOutput){
                        fprintf(incorrectMessages,"message: ");
                        for(int i=0;i<config->channels-7;i++)
                            fprintf(incorrectMessages,"%d",word[i]);
                        fprintf(incorrectMessages," id: %d is incorrect.    number_of_'0'_in_message = %d != %d = EDC\n", mesNum, zerosCount, binaryToDec(EDC,4));
                    }
                }

                seqDec++;
                if(seqDec == 8)
                    seqDec = 1;
                decToBinary(seqDec,3,seq);
                break;
            }
        }
        if (flagOutput){
            for(int i=0;i<config->channels-7;i++)
                fprintf(output,"%d",word[i]);
        }
    }
}
#ifndef NITROSKETCH_H
#define NITROSKETCH_H

#include <fstream>

#include "xxhash.h"

// NitroSketch: DS decl
#ifdef NITRO_CMS
// NitroSketch w/ Count-min:
typedef struct CountMinSketch {
    uint8_t converged;
    uint32_t p_count;
    uint32_t seed1[CM_ROW_NO];
    struct minHeap topK;
    int32_t* sketch[CM_ROW_NO];
    uint32_t line_to_update;
    int32_t gap;
    uint32_t nextUpdate;
    double p;
} CountMinSketch;
#endif

// NitroSketch w/ Count Sketch:
#ifdef NITRO_CS
typedef struct CountSketch {
    long double converged_f2;
    uint8_t converged;
    uint32_t p_count;
    uint32_t col_size;
    uint64_t cur_cycle;
    int32_t* sketch[CS_ROW_NO];
    minHeap topK;
    uint32_t seed1[CS_ROW_NO];
    uint32_t seed2[CS_ROW_NO];
    uint32_t line_to_update;
    int32_t gap;
    uint32_t nextUpdate;
    double p;
} CountSketch;
#endif

void print_sketch(CountMinSketch* cm, std::string filename) {
    std::ofstream output(filename, std::ios::trunc);
    for (size_t row = 0; row < CM_ROW_NO; row++) {
        for (size_t col = 0; col < CM_COL_NO; col++) {
            output << cm->sketch[row][col] << " ";
        }
        output << std::endl;
    }
}

// NitroSketch init
#ifdef NITRO_CMS
void cm_init(CountMinSketch* cm, uint32_t col_size, double _prob){
    //init fast rand();
    srand(time(NULL));

    initMinHeap(&(cm->topK), TOPK_SIZE); //init heap
    for (uint32_t i = 0; i < CM_ROW_NO; ++i){
        cm->sketch[i] = (int32_t *)calloc(col_size, sizeof(uint32_t)); //init
    }

#ifdef FASTRAND_UNI
    for (int j = 0; j < 1 << 16; ++j)
        randArr[j] = (double)rand()/RAND_MAX;
#endif

#ifdef FASTRAND_GEO
    for (int j = 0; j < 1 << 16; ++j)
        geoArr[j] = ran_geometric(_prob);
#endif

    cm->gap = (int)(1.0/_prob);
    cm->p = _prob;
    cm->p_count = 0;

    for (uint32_t j = 0; j < CM_ROW_NO; ++j){
        cm->seed1[j]=rand();
    }

#ifdef FASTRAND_GEO
    cm->line_to_update = geoArr[++geoIdx];
#else
    cm->line_to_update = ran_geometric(_prob)-1;
#endif
    cm->nextUpdate = cm->line_to_update / CM_ROW_NO;
    cm->line_to_update = cm->line_to_update%CM_ROW_NO;
    printf("NitroSketch w/ CMS configuration done!\n");
}

void cm_processing(CountMinSketch* cm, uint64_t key){

    uint32_t line_to_update = cm->line_to_update;
    //printf("NitroSketch: CM: %p, Key: %u, Line: %u, CMs1: %p, CM_COL_NO: %d, %d, %d\n",
    //       cm, key, line_to_update, cm->seed1, CM_COL_NO, sizeof(unsigned int), sizeof(int));

    //uint32_t hash_out = XXH32(&key, sizeof(key), cm->seed1[line_to_update]);
    //uint32_t col_loc =  hash_out % CM_COL_NO;

    uint32_t col_loc = XXH64(&key, sizeof(key), cm->seed1[line_to_update]) % ((uint32_t)CM_COL_NO);

    //printf("col_loc: %u\n", col_loc);
    //printf("NitroSketch: hash_out: %u, col: %u, mod: %u\n", hash_out, col_loc, hash_out % 102400);
    cm->sketch[line_to_update][col_loc] += cm->gap;

#ifdef TOPK
    if (rand()%cm->gap==0) { //update heap or not
            int median;
            int found = find(&(cm->topK), key);

            if (found != (-1)) {
                cm->topK.elem[found].count ++;
                for(int i = (cm->topK.size - 1) / 2; i >= 0; i--){
                    heapify(&(cm->topK), i) ;
                }
            }
            else if (cm->topK.size<TOPK_SIZE) {
                median = cm_query(cm, key);
                insertNode(&(cm->topK), key, median);
            }
            else{
                median = cm_query(cm, key);
                if(median > cm->topK.elem[0].count){
                    deleteNode(&(cm->topK));
                    insertNode(&(cm->topK), key, median);
                }
            }
    }
#endif

#ifdef FASTRAND_GEO
    line_to_update += geoArr[++geoIdx];
#else
    line_to_update += ran_geometric(PROB);
#endif
    cm->nextUpdate += line_to_update/CM_ROW_NO;
    cm->line_to_update = line_to_update%CM_ROW_NO;
    //printf("NitroSketch: line_to_update: %u\n", cm->line_to_update);
}

void cm_change_rate(CountMinSketch* cm, float _p){
    cm->p = _p;
    cm->gap = (int)(1.0/_p);
}
#endif

#ifdef NITRO_CS
void cs_init(CountSketch* cs, uint32_t col_size, double _prob){

    initMinHeap(&(cs->topK), TOPK_SIZE); //init heap
    cs->col_size = col_size;
    for (uint32_t i = 0; i < CS_ROW_NO; ++i){
        cs->sketch[i] = (int32_t *)malloc(col_size * sizeof(int32_t)); //init table
    }

#ifdef FASTRAND_UNI
    for (int j = 0; j < 1 << 16; ++j)
        randArr[j] = (double)rand()/RAND_MAX;
#endif

#ifdef FASTRAND_GEO
    for (int j = 0; j < 1 << 16; ++j)
        geoArr[j] = ran_geometric(_prob);
#endif

    cs->p = _prob;
    cs->gap = (int32_t)1.0/_prob;
    cs->p_count = 0;
    cs->converged = 0;
    cs->converged_f2 = 121.0*(1.0+DELAY_TARGET*sqrtl(PROB))*powl(DELAY_TARGET,-4.0)*powl(PROB,-2.0);

    for (uint32_t j = 0; j < CS_ROW_NO; ++j){
        cs->seed1[j]=rand();
        cs->seed2[j]=rand();
    }

#ifdef FASTRAND_GEO
    cs->line_to_update = geoArr[++geoIdx]-1;
#else
    cs->line_to_update = ran_geometric(_prob)-1;
#endif
    cs->nextUpdate = cs->line_to_update/CS_ROW_NO;
    cs->line_to_update = cs->line_to_update%CS_ROW_NO;

    printf("NitroSketch w/ Count Sketch configuration done!\n");
    cs->cur_cycle = __rdtsc();
    //clock_gettime(CLOCK_MONOTONIC, &cs->cur_time);
}

void cs_processing_always_line_rate(CountSketch* cs, uint32_t key)
{
    uint32_t line_to_update = cs->line_to_update;
    uint32_t col_loc = XXH32(&key, sizeof(key), cs->seed1[line_to_update])%CS_COL_NO;
    int f2_filter = XXH32(&key, sizeof(key), cs->seed2[line_to_update])%2;

    cs->sketch[line_to_update][col_loc] += (1 - 2*f2_filter)*cs->gap;
    cs->p_count++;
    if (cs->p_count % INTERVAL == 0){
        uint64_t diff_cycle;
        diff_cycle = __rdtsc();
    }

#ifdef TOPK
    if (rand()%cs->gap==0){ // update heap or not
        int median;
        int found = find(&(cs->topK), key);

        if (found != (-1)){
            cs->topK.elem[found].count ++;

            for(int i = (cs->topK.size - 1) / 2; i >= 0; i--) {
                heapify(&(cs->topK), i) ;
            }

        }
        else if (cs->topK.size<TOPK_SIZE){
            median = cs_query(cs, key);
            insertNode(&(cs->topK), key, median);
        }
        else {
            median = cs_query(cs, key);

            if(median > cs->topK.elem[0].count){
                deleteNode(&(cs->topK));
                insertNode(&(cs->topK), key, median);
            }
        }
    }
#endif

#ifdef FASTRAND_GEO
    line_to_update += geoArr[++geoIdx];
#else
    line_to_update += ran_geometric(cs->p);
#endif
    cs->nextUpdate += line_to_update/CS_ROW_NO;
    cs->line_to_update = line_to_update%CS_ROW_NO;
    //printf("NitroSketch: line_to_update: %u\n", cs->line_to_update);
}
#endif

#endif // NITROSKETCH_H

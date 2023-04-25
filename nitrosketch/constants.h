#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <stdint.h>

// Change the running mode of sketches
#define NITRO_CMS 1
//#define NITRO_CS 1
//#define NITRO_UNIVMON 1
//#define NITRO_KARY 1

//#define FASTRAND_UNI 1
#define FASTRAND_GEO 1

#define EVALUATION_TRACE 1
#define DELAY_SAMPLING 1
#define DELAY_TARGET 0.05
#define Q_INTERVAL 100

//HH related paramters
#define HH_THRESHOLD 0.0005
//#define FLOW_SRCIP 1
//#define FLOW_DSTIP 1
//#define FLOW_SRCDST 1
//#define FIVETUPLE 1


//Different processing modes
#define ALWAYS_CORRECT 1
//#define ALWAYS_LINERATE 1
//#define FIXED_RATE 1

#define CM_ROW_NO 5
#define CM_COL_NO 102400
//1000000 counters for 0.01 and 20*

#define CS_ROW_NO 5
#define CS_COL_NO 204800 //1000000 counters for 0.01 and 20*
#define CS_ONE_COL_NO 100000
#define CS_LVLS 16
//#define TOPK 1
#define TOPK_SIZE 100
#define HASH_SPACE 32767
#define PROB 0.01

//#define TABLE_SIZE 1000
#define INTERVAL 10000 //pkt
#define MILLION 1000000 //
#define BILLION 1000000000
#define BILLIONSEC 1000000000LL

#define AVX 4

#define SIMULATION_GAP INT_MAX

#define ZIPF_ALPHA 0.90
#define SEED 2147483647


uint32_t trace_count=0;

#endif // CONSTANTS_H
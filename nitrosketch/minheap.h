#ifndef MINHEAP_H
#define MINHEAP_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#define LCHILD(x) 2 * x + 1
#define RCHILD(x) 2 * x + 2
#define PARENT(x) (x - 1) / 2

typedef struct node {
  uint32_t key;
  int count;
} node ;

typedef struct minHeap {
    int size ;
    node *elem ;
} minHeap ;

void heapify(minHeap *hp, int i);
void initMinHeap(minHeap *heap, int size);
void deleteNode(minHeap *hp);
void insertNode(minHeap *hp, uint32_t _key, int _counter);
int find(minHeap *hp, uint32_t _key);

#endif // MINHEAP_H

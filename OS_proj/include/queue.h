#ifndef QUEUE_H
#define QUEUE_H

#include <stdio.h>

typedef struct Node {
    void* data;
    struct Node* next;
} Node;

typedef struct {
    Node* front;
    Node* rear;
} Queue;

// Functions
void initQueue(Queue* q);
int isEmpty(Queue* q);
void enqueue(Queue* q, void* data);
void* dequeue(Queue* q);
void printQueue(Queue* q);

void queue_fprint_pids(FILE *f, const Queue *q);

#endif
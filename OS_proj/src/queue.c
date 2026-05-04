#include <stdio.h>
#include <stdlib.h>
#include "../include/queue.h"
#include "../include/pcb.h"

// Initialize queue
void initQueue(Queue* q) {
    q->front = NULL;
    q->rear = NULL;
}

// Check if empty
int isEmpty(Queue* q) {
    return q->front == NULL;
}

// Add to queue
void enqueue(Queue* q, void* data) {
    Node* newNode = (Node*) malloc(sizeof(Node));
    newNode->data = data;
    newNode->next = NULL;

    if (q->rear == NULL) {
        q->front = q->rear = newNode;
    } else {
        q->rear->next = newNode;
        q->rear = newNode;
    }
}

// Remove from queue
void* dequeue(Queue* q) {
    if (isEmpty(q)) return NULL;

    Node* temp = q->front;
    void* data = temp->data;

    q->front = q->front->next;

    if (q->front == NULL)
        q->rear = NULL;

    free(temp);
    return data;
}

// Print queue (for debugging)
void printQueue(Queue* q) {
    Node* current = q->front;

    printf("Queue: ");
    while (current != NULL) {
        printf("[%p] -> ", current->data); // prints pointer (PCB later)
        current = current->next;
    }
    printf("NULL\n");
}

void queue_fprint_pids(FILE *f, const Queue *q) {
    fputs("[", f);
    Node *cur = q->front;
    int first = 1;
    while (cur != NULL) {
        PCB *p = (PCB *)cur->data;
        if (!first)
            fputs(", ", f);
        first = 0;
        fprintf(f, "%d", p->pid);
        cur = cur->next;
    }
    fputs("]", f);
}
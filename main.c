#include <stdio.h>
#include <stdbool.h>
#include <malloc.h>

struct ring_buffer{
    unsigned int head;
    unsigned int tail;
    unsigned int size;
    char buffer[4];
};

bool buffer_has_space(struct ring_buffer *rb){
    signed int next_index = (rb->head+1) % rb->size;
    if (next_index == rb->tail){
        return 1;
    }
    return  0;
}

bool buffer_is_empty(struct ring_buffer *rb){
    return rb->head == rb->tail;
}

unsigned int get_number_of_entries(struct ring_buffer *rb){
    signed int result = rb->head - rb->tail;
    if(result > 0){
        return result;
    }
    if(result < 0){
        result = rb->size - (rb->tail - rb->head);
        return result;
    }
    return 0;
}

void print_buffer_content(struct ring_buffer *rb){
    unsigned int entries = get_number_of_entries(rb);
    printf("Number of elements: %d\n", entries);
    for(int i=0; i < entries; i++){
        printf("Buffer entry: %c\n", rb->buffer[i]);
    }
}

int insert(struct ring_buffer *rb, char command){
    if(buffer_has_space(rb) == 0){
        rb->buffer[rb->head] = command;
        rb->head = (rb->head + 1) % rb->size;
        return 0;
    }
    printf("No space to insert the entry, dropping!\n");
    return 1;
}

char retrieve_command(struct ring_buffer *rb){
    if(buffer_is_empty(rb)){
        printf("Buffer is empty\n");
        return -1;
    }
    char tmp = rb->buffer[rb->tail];
    rb->tail = rb->tail + 1;
    return tmp;

}

char retrieve_command_at_index(struct ring_buffer *rb, int index){
    char tmp = rb->buffer[index];
    if(buffer_is_empty(rb)){
        printf("Buffer is empty\n");
        return -1;
    }
    rb->tail = index;
    return tmp;
}

int main() {
    struct ring_buffer *rb = malloc(sizeof(struct ring_buffer));
    rb->head = 0;
    rb->tail = 0;
    rb->size = 4;

    // When we establish communication we send the information about ring buffer to the consumer;
    //Consumer sends request to read command at the specific index
    //Once the index has been read the value latest_read_index is updated and head can move to up to the latest read index -1;

    for(int i=0; i<4; i++){
        insert(rb, 'i');
        printf(" Tried to insert an element. Number of elements: %d Head: %d Tail: %d\n", get_number_of_entries(rb), rb->head, rb->tail);
    }

    for(int i=0; i<5; i++){
        retrieve_command_at_index(rb, i);
        printf(" Tried to remove an element at index: %d. Number of elements: %d Head: %d Tail: %d\n",i, get_number_of_entries(rb), rb->head, rb->tail);
    }

    insert(rb, 'i');
    printf(" Tried to insert an element. Number of elements: %d Head: %d Tail: %d\n", get_number_of_entries(rb), rb->head, rb->tail);
    retrieve_command_at_index(rb, 0);
    printf(" Tried to remove an element at index: %d. Number of elements: %d Head: %d Tail: %d\n",0, get_number_of_entries(rb), rb->head, rb->tail);
    retrieve_command_at_index(rb, 0);
    printf(" Tried to remove an element at index: %d. Number of elements: %d Head: %d Tail: %d\n",0, get_number_of_entries(rb), rb->head, rb->tail);

    insert(rb, 'i');
    printf(" Tried to insert an element. Number of elements: %d Head: %d Tail: %d\n", get_number_of_entries(rb), rb->head, rb->tail);
    retrieve_command_at_index(rb, 1);
    printf(" Tried to remove an element at index: %d. Number of elements: %d Head: %d Tail: %d\n",1, get_number_of_entries(rb), rb->head, rb->tail);
    retrieve_command_at_index(rb, 2);
    printf(" Tried to remove an element at index: %d. Number of elements: %d Head: %d Tail: %d\n",2, get_number_of_entries(rb), rb->head, rb->tail);
    return 0;
}

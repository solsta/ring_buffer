#include <stdio.h>
#include <stdbool.h>
#include <libpmemobj/types.h>
#include <libpmemobj/pool_base.h>
#include <unistd.h>

struct ring_buffer{
    unsigned int head;
    unsigned int tail;
    unsigned int size;
    char buffer[4];
};

struct pmem_obj_root{
    struct ring_buffer rb;
};

struct variaed_lenght_command{
    char *command_payload;
    int command_lenght;
};


bool buffer_has_space_for_command(struct ring_buffer *rb, int command_lenght){
    signed int next_index = (rb->head+1) % rb->size;
    if (next_index == rb->tail){
        return 1;
    }
    return  0;
}

bool buffer_is_empty(struct ring_buffer *rb){
    return rb->head == rb->tail;
}

/* TODO
 * Update to get the number of commands for commands without fixed size.
 * */
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

/* TODO
 * Update to get the number of commands for commands without fixed size.
 * */
void print_buffer_content(struct ring_buffer *rb){
    unsigned int entries = get_number_of_entries(rb);
    printf("Number of elements: %d\n", entries);
    for(int i=0; i < entries; i++){
        printf("Buffer entry: %c\n", rb->buffer[i]);
    }
}
/* TODO
 * Update to get the number of commands for commands without fixed size.
 * */
int insert(struct ring_buffer *rb, char command){
    if(buffer_has_space(rb) == 0){
        rb->buffer[rb->head] = command;
        rb->head = (rb->head + 1) % rb->size;
        return 0;
    }
    printf("No space to insert the entry, dropping!\n");
    return 1;
}

/* TODO
 * Update to get the number of commands for commands without fixed size.
 * */
char retrieve_command_at_index(struct ring_buffer *rb, int index){
    char tmp = rb->buffer[index];
    if(buffer_is_empty(rb)){
        printf("Buffer is empty\n");
        return -1;
    }
    rb->tail = index;
    return tmp;
}

bool file_exists(const char *path){
    return access(path, F_OK) != 0;
}

struct ring_buffer * initialise_ring_buffer_on_persistent_memory(PMEMobjpool *pop){
    char *path_to_pmem = "/mnt/dax/test_outputs/pmem_log_ring_buffer";
    if (file_exists((path_to_pmem)) != 0) {
    if ((pop = pmemobj_create(path_to_pmem, POBJ_LAYOUT_NAME(list),PMEMOBJ_MIN_POOL, 0666)) == NULL)
            perror("failed to create pool\n");
    } else {
        if ((pop = pmemobj_open(path_to_pmem, POBJ_LAYOUT_NAME(list))) == NULL) {
            perror("failed to open pool\n");
        }
    }
    PMEMoid pool_root = pmemobj_root(pop, sizeof(struct pmem_obj_root));
    struct pmem_obj_root *rootp = pmemobj_direct(pool_root);

    return &rootp->rb;
}

int main() {
    PMEMobjpool *pop;
    struct ring_buffer *rb = initialise_ring_buffer_on_persistent_memory(pop);
    rb->size =sizeof(rb->buffer);
    rb->head = 0;
    rb->tail = 0;
    printf("Initialized ring_buffer\n");

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

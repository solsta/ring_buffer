#include <stdio.h>
#include <stdbool.h>
#include <libpmemobj/types.h>
#include <libpmemobj/pool_base.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>

struct ring_buffer{
    unsigned int head;
    unsigned int tail;
    unsigned int size;
    unsigned int real_size;
    char buffer[30];
};

struct pmem_obj_root{
    struct ring_buffer rb;
};

struct varied_length_command_info{
    int id;
    int command_lenght;
};

struct cmd_and_next_index{
    char *command;
    int next_index;
};

unsigned int get_free_slots_left_in_the_buffer(struct ring_buffer *rb){
    return rb->head > rb->tail ? rb->real_size-rb->head + rb->tail : rb->head + rb->real_size-rb->tail;
}

bool buffer_has_space_for_command(struct ring_buffer *rb, int command_lenght){
    return (get_free_slots_left_in_the_buffer(rb) - sizeof (struct varied_length_command_info) - command_lenght) > 0;
}


void print_available_buffer_slots(struct ring_buffer *rb){
    printf("There are %d free slot/s in the buffer\n", get_free_slots_left_in_the_buffer(rb));
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

int insert_in_to_buffer(struct ring_buffer *rb, char *command){
    /* What happens when the entry fits exactly in to the size of the buffer? */
    /* First check if we can fit the command with it's data within the current buffer's length. */
    unsigned int command_size = strlen(command);
    int cmd_info_size = sizeof (struct varied_length_command_info);
    int required_length = command_size+cmd_info_size;

    /* Special case when we can not fit entry in to a contiguous range*/
    if(rb->head > rb->tail && rb->real_size - rb->head < required_length){
        int remaining_space = rb->real_size - rb->head;
        printf("Command will be split!\n ");
        printf("Space left in the buffer: %d\n", get_free_slots_left_in_the_buffer(rb));
        printf("Space left in the current turn of the buffer: %d\n", remaining_space);
        printf("Rquired length: %d\n", required_length);

        /* Need to add the command here */
        void *full_cmd = malloc(required_length);
        struct varied_length_command_info cmd_info;
        cmd_info.command_lenght = command_size;
        memcpy(full_cmd, &cmd_info, cmd_info_size);
        memcpy(full_cmd+cmd_info_size, command, command_size+1);


        int reminder = required_length-remaining_space;
        void *addr_of_buffer = &rb->buffer[rb->head];
        void *addr_of_entry = full_cmd;
        memcpy(addr_of_buffer, addr_of_entry, remaining_space);
        printf("First memcopy worked\n");
        addr_of_buffer = &rb->buffer[0];
        addr_of_entry+=remaining_space;
        memcpy(addr_of_buffer, addr_of_entry, reminder);
        printf("Second memcopy worked\n");
        rb->head = reminder;
        free(full_cmd);
        return 0;

    } else {
        printf("Normal case insertion\n");
        printf("Inserting %s\n", command);
        /* TODO wrap inside a transaction */
        struct varied_length_command_info *cmd_info = malloc(sizeof (struct varied_length_command_info));
        cmd_info->command_lenght = strlen(command);

        memcpy(&rb->buffer[rb->head], cmd_info, sizeof(struct varied_length_command_info));
        printf("Written command with lenght as : %d, head is currently at : %d\n", cmd_info->command_lenght, rb->head);
        void *rb_addr = &rb->buffer[rb->head];
        char *command_slot = rb_addr + sizeof(struct varied_length_command_info);

        memcpy(command_slot, command, strlen(command)+1);
        printf("Written to slot: %s\n", command_slot);
        //command_slot = command_slot + strlen(command);
        rb->head += sizeof(struct varied_length_command_info) + strlen(command);

        /* TODO */
        //Handle the case when the end of the buffer is reached.
        //rb->buffer[rb->head] = command;
        //rb->head = (rb->head + 1) % rb->size;
        return 0;
    }
}

/* TODO
 * Update to get the number of commands for commands without fixed size.
 * */
int insert(struct ring_buffer *rb, char *command){
    if(buffer_has_space_for_command(rb, strlen(command)) > 0){
        insert_in_to_buffer(rb, command);
        return 0;
    }
    printf("No space to insert the entry, dropping!\n");
    return 1;
}

/* TODO
 * Update to get the number of commands for commands without fixed size.
 * */
struct cmd_and_next_index *retrieve_command_at_index(struct ring_buffer *rb, int index){

    if(buffer_is_empty(rb)){
        printf("Buffer is empty\n");
        return NULL;
    }


    //First check if current index fits the size of the info block

    int space_left_in_this_turn = rb->real_size-index;
    /* Special case when the struct will need to be copied partially */
    /* Case 1: cmd info is split between ring buffer turns. */
    if(space_left_in_this_turn < sizeof(struct varied_length_command_info)){
        printf("Executing the case with a split cmd info\n");
        struct varied_length_command_info *cmd_info = malloc(sizeof (struct varied_length_command_info));
        void *cmd_addr_ptr = cmd_info;

        void *current_turn_address = &rb->buffer[index];
        int remainder = sizeof (struct varied_length_command_info) - space_left_in_this_turn;
        printf("Copying %d bytes to cmd_info\n", space_left_in_this_turn);
        memcpy(cmd_addr_ptr, current_turn_address, space_left_in_this_turn);
        void *start_addr = &rb->buffer[0];
        cmd_addr_ptr+=space_left_in_this_turn;
        printf("Copying %d bytes to cmd_info\n", remainder);
        memcpy(cmd_addr_ptr, start_addr, remainder);
        printf("Command length according to cmd info: %d\n", cmd_info->command_lenght);
        int cmd_l = cmd_info->command_lenght;
        void *cmd_start_addr = &rb->buffer[0]+remainder;
        char *extracted_command = malloc(cmd_info->command_lenght);
        struct cmd_and_next_index *data = malloc(sizeof (struct cmd_and_next_index));
        /* Questionable */
        data->next_index = remainder + cmd_info->command_lenght;
        data->command = extracted_command;
        memcpy(extracted_command, cmd_start_addr, cmd_info->command_lenght);
        free(cmd_info);
        rb->tail = index;
        return data;

    } else {

        struct varied_length_command_info *cmd_info = malloc(sizeof (struct varied_length_command_info));
        memcpy(cmd_info, &rb->buffer[index], sizeof (struct varied_length_command_info));
        printf("Initil copy of cmd_info says that command length is : %d\n", cmd_info->command_lenght);
        void *cmd_info_addr = cmd_info;
        char *extracted_command = malloc(cmd_info->command_lenght);
        int space_left_for_command = rb->real_size-(index+sizeof (struct varied_length_command_info));
        /* Case 2: when info is within current turn of the buffer, but the command is not */
        if(cmd_info->command_lenght > space_left_for_command){
            void *destination = extracted_command;
            memcpy(destination, cmd_info_addr+sizeof (struct varied_length_command_info), space_left_for_command);
            void *src = &rb->buffer[0];
            destination+=space_left_for_command;
            memcpy(destination, src, cmd_info->command_lenght-space_left_for_command);
            struct cmd_and_next_index *data = malloc(sizeof (struct cmd_and_next_index));
            data->command = extracted_command;
            data->next_index = cmd_info->command_lenght-space_left_for_command;
            rb->tail = index;
            return data;
        } else{
            printf("Normal case extraction\n");
            /* Normal case */
            printf("Command lenght: %d\n", cmd_info->command_lenght);
            memcpy(extracted_command,&rb->buffer[index]+sizeof (struct varied_length_command_info), cmd_info->command_lenght);
            //printf("src command: %s\n", cmd_info_addr+sizeof (struct varied_length_command_info));
            printf("Extracted command: %s\n", extracted_command);
            struct cmd_and_next_index *data = malloc(sizeof (struct cmd_and_next_index));
            data->command = extracted_command;
            data->next_index = index+sizeof (struct varied_length_command_info) + cmd_info->command_lenght;
            rb->tail = index;
            return data;
        }



    }
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
    rb->real_size = rb->size-1;
    rb->head = 0;
    rb->tail = 0;
    printf("Initialized ring_buffer\n");
    print_available_buffer_slots(rb);
    // When we establish communication we send the information about ring buffer to the consumer;
    //Consumer sends request to read command at the specific index
    //Once the index has been read the value latest_read_index is updated and head can move to up to the latest read index -1;
    int cmd_index = 0;
    /* Case 1 */
    insert(rb, "abcd");
    struct cmd_and_next_index *cmd_data = retrieve_command_at_index(rb, cmd_index);
    cmd_index = cmd_data->next_index;
    printf("Extracted command: %s, next index: %d\n", cmd_data->command, cmd_data->next_index);

    /* Case 2 */
    insert(rb, "abcd");
    cmd_data = retrieve_command_at_index(rb, cmd_index);
    cmd_index = cmd_data->next_index;
    printf("Extracted command: %s, next index: %d\n", cmd_data->command, cmd_data->next_index);

    /* Case 3 */
    insert(rb, "abcd");
    cmd_data = retrieve_command_at_index(rb, cmd_index);
    cmd_index = cmd_data->next_index;
    printf("Extracted command: %s, next index: %d\n", cmd_data->command, cmd_data->next_index);

    return 0;
}

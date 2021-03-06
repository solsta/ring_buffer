#include <stdio.h>
#include <stdbool.h>
#include <libpmemobj/types.h>
#include <libpmemobj/pool_base.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>
#include <libpmemobj/tx_base.h>

typedef struct assert assert;
struct ring_buffer{
    unsigned int number_of_commands;
    unsigned int head;
    unsigned int tail;
    unsigned int size;
    unsigned int real_size;
    signed int last_seen_index;
    char buffer[30];
};

struct pmem_obj_root{
    struct ring_buffer rb;
};

struct varied_length_command_info{
    int command_lenght;
};

struct cmd_and_next_index{
    char *command;
    int next_index;
};

static void
log_stages(PMEMobjpool *pop_local, enum pobj_tx_stage stage, void *arg)
{
    /* Commenting this out because this is not required during normal execution. */
    /* dr_fprintf(STDERR, "cb stage: ", desc[stage], " "); */
}

unsigned int get_free_slots_left_in_the_buffer(struct ring_buffer *rb){
    /* There is no scenario where this can happen other than on the first iteration */
    if(rb->head == rb->tail && rb->head == 0){
        return rb->real_size;
    }
    return rb->head > rb->tail ? rb->real_size-rb->head + rb->tail : rb->tail - rb->head;
}

bool buffer_has_space_for_command(struct ring_buffer *rb, int command_lenght){
    return (get_free_slots_left_in_the_buffer(rb) - sizeof (struct varied_length_command_info) - command_lenght) >= 0;
}


void print_available_buffer_slots(struct ring_buffer *rb){
    printf("There are %d free slot/s in the buffer\n", get_free_slots_left_in_the_buffer(rb));
}

int insert_in_to_buffer(PMEMobjpool *pop, struct ring_buffer *rb, char *command){
    unsigned int command_size = strlen(command);
    int cmd_info_size = sizeof (struct varied_length_command_info);
    int required_length = command_size+cmd_info_size;
    printf("required lenght: %d, space left: %d\n", required_length, rb->real_size-rb->head);

    /* Special case when we can not fit entry in to a contiguous range*/
    if( rb->real_size - rb->head < required_length){
        int remaining_space = rb->real_size - rb->head;
        printf("Command will be split!\n ");
        printf("Space left in the buffer: %d\n", get_free_slots_left_in_the_buffer(rb));
        printf("Space left in the current turn of the buffer: %d\n", remaining_space);
        printf("Rquired length: %d\n", required_length);

        void *full_cmd = malloc(required_length);
        struct varied_length_command_info cmd_info;
        cmd_info.command_lenght = command_size;
        memcpy(full_cmd, &cmd_info, cmd_info_size);
        memcpy(full_cmd+cmd_info_size, command, command_size+1);

        int reminder = required_length-remaining_space;
        void *addr_of_buffer = &rb->buffer[rb->head];
        void *addr_of_entry = full_cmd;
        pmemobj_memcpy_persist(pop,addr_of_buffer, addr_of_entry, remaining_space);
        printf("First memcopy worked\n");
        addr_of_buffer = &rb->buffer[0];
        addr_of_entry+=remaining_space;
        pmemobj_memcpy_persist(pop,addr_of_buffer, addr_of_entry, reminder);
        printf("Second memcopy worked\n");
        pmemobj_tx_add_range_direct(&rb->head, sizeof (rb->head));
        rb->head = reminder;
        free(full_cmd);
        return 0;

    } else {

        struct varied_length_command_info *cmd_info = malloc(sizeof (struct varied_length_command_info));
        cmd_info->command_lenght = strlen(command);
        pmemobj_memcpy_persist(pop, &rb->buffer[rb->head], cmd_info, sizeof(struct varied_length_command_info));
        void *rb_addr = &rb->buffer[rb->head];
        char *command_slot = rb_addr + sizeof(struct varied_length_command_info);

        pmemobj_memcpy_persist(pop,command_slot, command, strlen(command)+1);
        pmemobj_tx_add_range_direct(&rb->head, sizeof(rb->head));
        rb->head += sizeof(struct varied_length_command_info) + strlen(command);
        return 0;
    }
}

int insert(PMEMobjpool *pop, struct ring_buffer *rb, char *command){
    if(strlen (command) + sizeof (struct varied_length_command_info) >= rb->size){
        printf("No space to insert this entry, dropping!\n");
        return 1;
    }
    if(strlen (command) + sizeof (struct varied_length_command_info) > get_free_slots_left_in_the_buffer(rb)){
        printf("No space to insert this entry, dropping!\n");
        return 1;
    }
    pmemobj_tx_begin(pop, NULL, TX_PARAM_CB, log_stages, NULL,
                     TX_PARAM_NONE);
    insert_in_to_buffer(pop,rb, command);
    pmemobj_tx_add_range_direct(&rb->number_of_commands, sizeof (rb->number_of_commands));
    rb->number_of_commands+=1;
    pmemobj_tx_commit();
    pmemobj_tx_end();
    return 0;
}

struct cmd_and_next_index *retrieve_command_at_index(PMEMobjpool *pop, struct ring_buffer *rb, int index){
    pmemobj_tx_begin(pop, NULL, TX_PARAM_CB, log_stages, NULL,
                     TX_PARAM_NONE);


    if(rb->number_of_commands == 0){
        if(rb->last_seen_index!=index){
            printf("Tried to extract incorrect index\n");
            return NULL;
        }
    }

    if(rb->last_seen_index!=index){
        pmemobj_tx_add_range_direct(&rb->number_of_commands, sizeof (rb->number_of_commands));
        rb->number_of_commands-=1;

    }

    pmemobj_tx_add_range_direct(&rb->last_seen_index, sizeof (rb->last_seen_index));
    rb->last_seen_index = index;

    pmemobj_tx_add_range_direct(&rb->tail, sizeof (rb->tail));
    //Can't just do that

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
        void *cmd_start_addr = &rb->buffer[0]+remainder;
        char *extracted_command = malloc(cmd_info->command_lenght);
        struct cmd_and_next_index *data = malloc(sizeof (struct cmd_and_next_index));
        /* Questionable */
        data->next_index = remainder + cmd_info->command_lenght;
        data->command = extracted_command;
        memcpy(extracted_command, cmd_start_addr, cmd_info->command_lenght);
        free(cmd_info);
        rb->tail = index;
        pmemobj_tx_commit();
        pmemobj_tx_end();
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
            printf("Executing 2nd edge case\n");
            printf("Reading directly from buffer: %s\n", &rb->buffer[index]+sizeof (struct varied_length_command_info));
            void *destination = extracted_command;
            memcpy(destination, &rb->buffer[index]+sizeof (struct varied_length_command_info), space_left_for_command);
            void *src = &rb->buffer[0];
            destination+=space_left_for_command;
            memcpy(destination, src, cmd_info->command_lenght-space_left_for_command);
            struct cmd_and_next_index *data = malloc(sizeof (struct cmd_and_next_index));
            data->command = extracted_command;
            data->next_index = cmd_info->command_lenght-space_left_for_command;
            rb->tail = index;
            pmemobj_tx_commit();
            pmemobj_tx_end();
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
            pmemobj_tx_commit();
            pmemobj_tx_end();
            return data;
        }
    }
}

bool file_exists(const char *path){
    return access(path, F_OK) != 0;
}

PMEMobjpool *mmap_pmem_object_pool(PMEMobjpool *pop){
    char *path_to_pmem = "/mnt/dax/test_outputs/pmem_log_ring_buffer";
    if (file_exists((path_to_pmem)) != 0) {
        if ((pop = pmemobj_create(path_to_pmem, POBJ_LAYOUT_NAME(list),PMEMOBJ_MIN_POOL, 0666)) == NULL)
            perror("failed to create pool\n");
    } else {
        if ((pop = pmemobj_open(path_to_pmem, POBJ_LAYOUT_NAME(list))) == NULL) {
            perror("failed to open pool\n");
        }
    }
    return pop;
}

struct ring_buffer * initialise_ring_buffer_on_persistent_memory(PMEMobjpool *pop){
    PMEMoid pool_root = pmemobj_root(pop, sizeof(struct pmem_obj_root));
    struct pmem_obj_root *rootp = pmemobj_direct(pool_root);
    return &rootp->rb;
}

void print_head_and_tail(struct ring_buffer *rb){
    printf("Head is at: %d, Tail is at: %d\n", rb->head, rb->tail);
}

void print_commands_in_the_buffer(struct ring_buffer *rb){
    printf("Commands in the buffer: %d\n", rb->number_of_commands);
}

struct ring_buffer *initialise_ring_buffer(PMEMobjpool *pop){
    struct ring_buffer *rb = initialise_ring_buffer_on_persistent_memory(pop);
    pmemobj_tx_begin(pop, NULL, TX_PARAM_CB, log_stages, NULL,
                     TX_PARAM_NONE);
    pmemobj_tx_add_range_direct(&rb->size, sizeof (rb->size));
    pmemobj_tx_add_range_direct(&rb->real_size, sizeof (rb->real_size));
    pmemobj_tx_add_range_direct(&rb->head, sizeof (rb->head));
    pmemobj_tx_add_range_direct(&rb->tail, sizeof (rb->tail));
    pmemobj_tx_add_range_direct(&rb->number_of_commands, sizeof (rb->number_of_commands));
    pmemobj_tx_add_range_direct(&rb->last_seen_index, sizeof (rb->last_seen_index));

    rb->size =sizeof(rb->buffer);
    rb->real_size = rb->size-1;
    rb->head = 0;
    rb->tail = 0;
    rb->number_of_commands = 0;
    rb->last_seen_index = -1;
    printf("Initialized ring_buffer\n");
    pmemobj_tx_commit();
    pmemobj_tx_end();
    return rb;
}

void reset_ring_buffer(PMEMobjpool *pop, struct ring_buffer *rb){
    pmemobj_tx_begin(pop, NULL, TX_PARAM_CB, log_stages, NULL,
                     TX_PARAM_NONE);
    pmemobj_tx_add_range_direct(&rb->head, sizeof (rb->head));
    pmemobj_tx_add_range_direct(&rb->tail, sizeof (rb->tail));
    pmemobj_tx_add_range_direct(&rb->number_of_commands, sizeof (rb->number_of_commands));
    pmemobj_tx_add_range_direct(&rb->last_seen_index, sizeof (rb->last_seen_index));
    rb->head = 0;
    rb->tail = 0;
    rb->number_of_commands = 0;
    rb->last_seen_index = -1;
    pmemobj_tx_commit();
    pmemobj_tx_end();
}

int main() {



    PMEMobjpool *pop = NULL;
    pop = mmap_pmem_object_pool(pop);
    struct ring_buffer *rb = initialise_ring_buffer(pop);
    print_available_buffer_slots(rb);

    /* Test swithing off ASLR */
    /* 0x7ffff73c0550 */
    printf("Ring buffer address: %p\n", rb);

    int cmd_index = 0;
    struct cmd_and_next_index *cmd_data;

    /* This must fail because command is too long */
    assert( insert(pop, rb, "foo bar foo bar foo bar foo bar") == 1);

    /* Testing inserting more than buffer can accomodate for */
    assert(insert(pop,rb, "abcdefghij") == 0);
    assert(insert(pop,rb, "abcdefghij") == 0);
    assert(insert(pop,rb, "abcdefghij") == 1);
    reset_ring_buffer(pop, rb);

    /* Testing a reset scenario */
    assert(insert(pop,rb, "abcdef") == 0);
    assert(insert(pop,rb, "abcdef") == 0);
    assert(insert(pop,rb, "abcdef") == 1);
    assert(insert(pop,rb, "abcdefg") == 1);
    assert(insert(pop,rb, "abcde") == 0);
    assert(rb->head == rb->real_size);
    reset_ring_buffer(pop,rb);

    char seen_head_positions[rb->real_size];
    char seen_tail_positions[rb->real_size];
    for(int i = 0; i<=rb->real_size; ++i){
        seen_head_positions[i] = '0';
        seen_tail_positions[i] = '0';
    }

    for(int i=0; i < rb->size+1; ++i){

        seen_head_positions[rb->head] = '1';
        seen_tail_positions[rb->tail] = '1';

        insert(pop,rb, "foo bar");
        assert(rb->head < rb->size);
        assert(rb->tail < rb->size);

        seen_head_positions[rb->head] = '1';
        seen_tail_positions[rb->tail] = '1';

        cmd_data = retrieve_command_at_index(pop,rb, cmd_index);
        assert(strcmp(cmd_data->command, "foo bar") == 0);
        cmd_data = retrieve_command_at_index(pop,rb, cmd_index);
        assert(strcmp(cmd_data->command, "foo bar") == 0);

        cmd_index = cmd_data->next_index;
    }

    for(int i = 0; i<=rb->real_size; ++i){
        assert(seen_head_positions[i] == '1');
        assert(seen_tail_positions[i] == '1');
    }

    /* This must return NULL if the process tries to retrieve a command which does not exist */
    assert(retrieve_command_at_index(pop, rb,cmd_index) == NULL);

    pmemobj_close(pop);

    return 0;
}

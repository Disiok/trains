#include <bwio.h>
#include <io.h>
#include <train.h>
#include <util.h>

void tr_init(TrainController *controller, Clock *clock) {
    bc_init(&(controller->channel), COM1);
    rb_init(&(controller->rvBuffer));
    rb_init(&(controller->swBuffer));
    controller->clock = clock;

    // Set up communication protocol
    tr_init_protocol(controller);

    // Initialize train speed
    tr_init_train_speed(controller);
}

/*
 * Set communication protocol:
 * Baud rate = 2400
 * Start bits (if requested by computer) = 1
 * Stop bits = 2
 * Parity = None
 * Word size = 8 bits
 * FIFO = OFF
 */
void tr_init_protocol(TrainController *controller) {
    BufferedChannel *channel = &(controller->channel);

    setspeed(channel, 2400);
    set2stopbits(channel);
    setnoparity(channel);
    set8wordsize(channel);
    setfifo(channel, 0);
}

void tr_init_train_speed(TrainController *controller) {
    int i;
    for (i = TRAIN_NUMBER_MIN; i < TRAIN_NUMBER_MAX; i++) {
        controller->trainSpeed[i] = TRAIN_SPEED_MIN;    
    }

    return;
}

void tr_poll(TrainController *controller, TerminalController *terminal) {
    BufferedChannel *channel = &(controller->channel);
    bc_poll(channel);

    if (cl_time_changed(controller->clock)) {
        int time_ms = cl_get_time_ms(controller->clock);            

        // Check switch buffer
        RingBuffer *swBuffer = &(controller->swBuffer);
        if (!rb_is_empty(swBuffer)) {
            int sw_scheduled_time = rb_peak_int(swBuffer);

            if (sw_scheduled_time < time_ms) {
                rb_shrink_int(swBuffer);
                char command = rb_shrink(swBuffer);
                putc(channel, command);
            }
        }

        // Check reverse buffer
        RingBuffer *rvBuffer = &(controller->rvBuffer);
        if (!rb_is_empty(rvBuffer)) {
            int rv_scheduled_time = rb_peak_int(rvBuffer);

            if (rv_scheduled_time < time_ms) {
                rb_shrink_int(rvBuffer);
                char command = rb_shrink(rvBuffer);
                putc(channel, command);
            }
        }

        // Poll sensors
        if (!rb_is_empty(&(channel->readBuffer))) {
              
        }
        tr_request_sensors(controller, 5);
        
    }
}

void tr_update_command(TrainController *controller, char *command) {
    if (strncmp(command, "tr", 2) == 0) {
        int train_number = parse_int_arg(command, 0);
        int train_speed = parse_int_arg(command, 1);
        tr_set_speed(controller, train_number, train_speed);
    } else if (strncmp(command, "rv", 2) == 0) {
        int train_number = parse_int_arg(command, 0);
        tr_reverse(controller, train_number);
    } else if (strncmp(command, "sw", 2) == 0) {
        int switch_number = parse_int_arg(command, 0);
        char switch_direction = parse_char_arg(command, 1);
        tr_switch(controller, switch_number, switch_direction);
    } else if (strncmp(command, "go", 2) == 0) {
        tr_go(controller);
    } else if (strncmp(command, "stop", 4) == 0) {
        tr_stop(controller);
    }
}

int tr_go(TrainController *controller) {
    BufferedChannel *channel = &(controller->channel);
    putc(channel, TRAIN_GO);

    return 0;
}

int tr_stop(TrainController *controller) {
    BufferedChannel *channel = &(controller->channel);
    putc(channel, TRAIN_STOP);

    return 0;
}

int tr_set_speed(TrainController *controller, int train_number, int train_speed) {
    if (train_speed > TRAIN_SPEED_MAX || train_speed < TRAIN_SPEED_MIN) {
        return 1; 
    }

    if (train_number > TRAIN_NUMBER_MAX || train_number < TRAIN_NUMBER_MIN) {
        return 1; 
    }

    controller->trainSpeed[train_number] = (char) train_speed;

    BufferedChannel *channel = &(controller->channel);
    putc(channel, (char) train_speed);
    putc(channel, (char) train_number);
    return 0;
}

int tr_reverse(TrainController *controller, int train_number) {
    if (train_number > TRAIN_NUMBER_MAX || train_number < TRAIN_NUMBER_MIN) {
        return 1; 
    }

    BufferedChannel *channel = &(controller->channel);
    putc(channel, (char) 0);
    putc(channel, (char) train_number);

    return tr_schedule_delayed_reverse(controller, train_number);
}

int tr_request_sensors(TrainController *controller, int max) {
    BufferedChannel *channel = &(controller->channel);

    putc(channel, TRAIN_SENSOR_BASE + max);

    return 0;
}

int tr_switch(TrainController *controller, int switch_number, char switch_direction) {
    if (switch_number > TRAIN_SWITCH_MAX || switch_number < TRAIN_SWITCH_MIN) {
        return 1; 
    }
    BufferedChannel *channel = &(controller->channel);
    if (switch_direction == 's') {
        putc(channel, TRAIN_SWITCH_STRAIGHT); 
        putc(channel, (char) switch_number);
    } else if (switch_direction == 'c') {
        putc(channel, TRAIN_SWITCH_CURVE); 
        putc(channel, (char) switch_number);
    } else {
        return 1;
    }

    return tr_schedule_delayed_switch(controller);
}

int tr_schedule_delayed_switch(TrainController *controller) {
    int time_ms = cl_get_time_ms(controller->clock) + TRAIN_SW_DELAY;

    // Turn switch solenoid off
    rb_grow_int(&(controller->swBuffer), time_ms);
    rb_grow(&(controller->swBuffer), TRAIN_SWITCH_OFF);

    return 0;
}

int tr_schedule_delayed_reverse(TrainController *controller, int train_number) {
    int time_ms = cl_get_time_ms(controller->clock) + TRAIN_RV_DELAY;

    // Reverse
    rb_grow_int(&(controller->rvBuffer), time_ms);
    rb_grow(&(controller->rvBuffer), (char) TRAIN_SPEED_REVERSE);
    rb_grow_int(&(controller->rvBuffer), time_ms);
    rb_grow(&(controller->rvBuffer), (char) train_number);

    // Speed up
    rb_grow_int(&(controller->rvBuffer), time_ms);
    rb_grow(&(controller->rvBuffer), (char) controller->trainSpeed[train_number]);
    rb_grow_int(&(controller->rvBuffer), time_ms);
    rb_grow(&(controller->rvBuffer), (char) train_number);

    return 0;
}


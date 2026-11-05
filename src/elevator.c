#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "elevator.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group 37");
MODULE_DESCRIPTION("Elevator Kernel Module");

#define MAX_FLOORS 5
#define MAX_PASSENGERS 5
#define MAX_LOAD 70

#define PART_TIMER 0
#define LAWYER     1
#define BOSS       2
#define VISITOR    3

extern int (*STUB_start_elevator)(void);
extern int (*STUB_issue_request)(int, int, int);
extern int (*STUB_stop_elevator)(void);

typedef struct {
    int type;
    int start_floor;
    int dest_floor;
    struct list_head list;
} Passenger;

typedef enum {
    OFFLINE,
    IDLE,
    LOADING,
    UP,
    DOWN,
    DEACTIVATING
} ElevatorState;

static ElevatorState elevator_state = OFFLINE;
static int current_floor = 1;
static int current_load = 0;
static int num_passengers = 0;
static int num_waiting = 0;
static int num_serviced = 0;

static struct list_head elevator_passengers;
static struct list_head floor_queues[MAX_FLOORS];
static struct mutex elevator_mutex;
static struct task_struct *elevator_thread;
static struct proc_dir_entry *proc_entry;

static int get_weight(int type) {
    switch(type) {
        case PART_TIMER: return 10;
        case LAWYER:     return 15;
        case BOSS:       return 20;
        case VISITOR:    return 5;
        default:         return 0;
    }
}

static char get_type_char(int type) {
    switch(type) {
        case PART_TIMER: return 'P';
        case LAWYER:     return 'L';
        case BOSS:       return 'B';
        case VISITOR:    return 'V';
        default:         return '?';
    }
}

static void load_passengers(void) {
    Passenger *p, *tmp;
    list_for_each_entry_safe(p, tmp, &floor_queues[current_floor - 1], list) {
        int weight = get_weight(p->type);
        if (num_passengers < MAX_PASSENGERS && current_load + weight <= MAX_LOAD) {
            list_del(&p->list);
            list_add_tail(&p->list, &elevator_passengers);
            current_load += weight;
            num_passengers++;
            num_waiting--;
        }
    }
}

static void unload_passengers(void) {
    Passenger *p, *tmp;
    list_for_each_entry_safe(p, tmp, &elevator_passengers, list) {
        if (p->dest_floor == current_floor) {
            list_del(&p->list);
            current_load -= get_weight(p->type);
            num_passengers--;
            num_serviced++;
            kfree(p);
        }
    }
}

static int elevator_run(void *data) {
    Passenger *p;
    int i;
    int needs_down;
    int needs_up;

    while (!kthread_should_stop()) {
        mutex_lock(&elevator_mutex);

        if (elevator_state == OFFLINE) {
            mutex_unlock(&elevator_mutex);
            ssleep(1);
            continue;
        }

        if (elevator_state == IDLE) {
            if (num_waiting > 0)
                elevator_state = UP;
            mutex_unlock(&elevator_mutex);
            ssleep(1);
            continue;
        }

        if (elevator_state == DEACTIVATING) {
            unload_passengers();
            if (num_passengers == 0) {
                elevator_state = OFFLINE;
                mutex_unlock(&elevator_mutex);
                continue;
            }
            if (current_floor < MAX_FLOORS)
                current_floor++;
            else
                current_floor--;
            mutex_unlock(&elevator_mutex);
            ssleep(2);
            continue;
        }

        if (elevator_state == LOADING) {
            unload_passengers();
            load_passengers();

            needs_down = 0;
            needs_up = 0;

            if (num_passengers > 0 || num_waiting > 0) {
                list_for_each_entry(p, &elevator_passengers, list) {
                    if (p->dest_floor < current_floor) needs_down = 1;
                    if (p->dest_floor > current_floor) needs_up = 1;
                }
                for (i = 0; i < current_floor - 1; i++)
                    if (!list_empty(&floor_queues[i])) needs_down = 1;
                for (i = current_floor; i < MAX_FLOORS; i++)
                    if (!list_empty(&floor_queues[i])) needs_up = 1;

                if (needs_down) elevator_state = DOWN;
                else if (needs_up) elevator_state = UP;
                else elevator_state = IDLE;
            } else {
                elevator_state = IDLE;
            }
            mutex_unlock(&elevator_mutex);
            ssleep(1);
            continue;
        }

        if (elevator_state == UP) {
            if (current_floor < MAX_FLOORS)
                current_floor++;
            else
                elevator_state = DOWN;
            if (elevator_state == UP)
                elevator_state = LOADING;
            mutex_unlock(&elevator_mutex);
            ssleep(2);
            continue;
        }

        if (elevator_state == DOWN) {
            if (current_floor > 1)
                current_floor--;
            else
                elevator_state = UP;
            if (elevator_state == DOWN)
                elevator_state = LOADING;
            mutex_unlock(&elevator_mutex);
            ssleep(2);
            continue;
        }

        mutex_unlock(&elevator_mutex);
        ssleep(1);
    }
    return 0;
}

int start_elevator(void) {
    mutex_lock(&elevator_mutex);
    if (elevator_state != OFFLINE) {
        mutex_unlock(&elevator_mutex);
        return 1;
    }
    elevator_state = IDLE;
    mutex_unlock(&elevator_mutex);
    return 0;
}
EXPORT_SYMBOL(start_elevator);

int stop_elevator(void) {
    mutex_lock(&elevator_mutex);
    if (elevator_state == OFFLINE || elevator_state == DEACTIVATING) {
        mutex_unlock(&elevator_mutex);
        return 1;
    }
    elevator_state = DEACTIVATING;
    mutex_unlock(&elevator_mutex);
    return 0;
}
EXPORT_SYMBOL(stop_elevator);

int issue_request(int start_floor, int destination_floor, int type) {
    Passenger *p;

    if (start_floor < 1 || start_floor > MAX_FLOORS ||
        destination_floor < 1 || destination_floor > MAX_FLOORS ||
        type < 0 || type > 3 ||
        start_floor == destination_floor)
        return 1;

    p = kmalloc(sizeof(Passenger), GFP_KERNEL);
    if (!p) return -ENOMEM;

    p->type = type;
    p->start_floor = start_floor;
    p->dest_floor = destination_floor;
    INIT_LIST_HEAD(&p->list);

    mutex_lock(&elevator_mutex);
    list_add_tail(&p->list, &floor_queues[start_floor - 1]);
    num_waiting++;
    mutex_unlock(&elevator_mutex);

    return 0;
}
EXPORT_SYMBOL(issue_request);

static int elevator_show(struct seq_file *m, void *v) {
    Passenger *p;
    int i;
    int count;
    char *state_str;

    mutex_lock(&elevator_mutex);

    switch(elevator_state) {
        case OFFLINE:      state_str = "OFFLINE";      break;
        case IDLE:         state_str = "IDLE";         break;
        case LOADING:      state_str = "LOADING";      break;
        case UP:           state_str = "UP";           break;
        case DOWN:         state_str = "DOWN";         break;
        case DEACTIVATING: state_str = "DEACTIVATING"; break;
        default:           state_str = "UNKNOWN";      break;
    }

    seq_printf(m, "Elevator state: %s\n", state_str);
    seq_printf(m, "Current floor: %d\n", current_floor);
    seq_printf(m, "Current load: %d lbs\n", current_load);

    seq_printf(m, "Elevator status:");
    list_for_each_entry(p, &elevator_passengers, list) {
        seq_printf(m, " %c%d", get_type_char(p->type), p->dest_floor);
    }
    seq_printf(m, "\n\n");

    for (i = MAX_FLOORS; i >= 1; i--) {
        count = 0;
        list_for_each_entry(p, &floor_queues[i - 1], list) count++;
        seq_printf(m, "[%c] Floor %d: %d", (i == current_floor) ? '*' : ' ', i, count);
        list_for_each_entry(p, &floor_queues[i - 1], list) {
            seq_printf(m, " %c%d", get_type_char(p->type), p->dest_floor);
        }
        seq_printf(m, "\n");
    }

    seq_printf(m, "\nNumber of passengers: %d\n", num_passengers);
    seq_printf(m, "Number of passengers waiting: %d\n", num_waiting);
    seq_printf(m, "Number of passengers serviced: %d\n", num_serviced);

    mutex_unlock(&elevator_mutex);
    return 0;
}

static int elevator_open(struct inode *inode, struct file *file) {
    return single_open(file, elevator_show, NULL);
}

static const struct proc_ops elevator_fops = {
    .proc_open    = elevator_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static int __init elevator_init(void) {
    int i;

    INIT_LIST_HEAD(&elevator_passengers);
    for (i = 0; i < MAX_FLOORS; i++)
        INIT_LIST_HEAD(&floor_queues[i]);

    mutex_init(&elevator_mutex);

    proc_entry = proc_create("elevator", 0444, NULL, &elevator_fops);
    if (!proc_entry) return -ENOMEM;

    elevator_thread = kthread_run(elevator_run, NULL, "elevator_thread");
    if (IS_ERR(elevator_thread)) {
        proc_remove(proc_entry);
        return PTR_ERR(elevator_thread);
    }

    STUB_start_elevator = start_elevator;
    STUB_issue_request = issue_request;
    STUB_stop_elevator = stop_elevator;

    printk(KERN_INFO "elevator: module loaded\n");
    return 0;
}

static void __exit elevator_exit(void) {
    Passenger *p, *tmp;
    int i;

    STUB_start_elevator = NULL;
    STUB_issue_request = NULL;
    STUB_stop_elevator = NULL;

    kthread_stop(elevator_thread);
    proc_remove(proc_entry);

    list_for_each_entry_safe(p, tmp, &elevator_passengers, list) {
        list_del(&p->list);
        kfree(p);
    }

    for (i = 0; i < MAX_FLOORS; i++) {
        list_for_each_entry_safe(p, tmp, &floor_queues[i], list) {
            list_del(&p->list);
            kfree(p);
        }
    }

    printk(KERN_INFO "elevator: module unloaded\n");
}

module_init(elevator_init);
module_exit(elevator_exit);

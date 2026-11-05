"""Exercise the actual shutdown branch in C with kernel services stubbed out.
Run: python3 tests/test_shutdown.py (requires gcc).
This verifies scheduling logic, not kernel synchronization or integration.
"""
from pathlib import Path
import subprocess
import tempfile
import os

source = (Path(__file__).resolve().parents[1] / 'src/elevator.c').read_text()
start = source.index('        if (elevator_state == DEACTIVATING) {')
end = source.index('        if (elevator_state == LOADING) {', start)
branch = source[start:end]
harness = r'''
#include <stdio.h>
#define MAX_FLOORS 5
#define OFFLINE 0
#define DEACTIVATING 6
typedef struct { int dest_floor; } Passenger;
static Passenger passengers[5];
static int current_floor, num_passengers, elevator_state, elevator_mutex;
static int elevator_passengers;
#define list_for_each_entry(p, head, member) \
    for ((p) = passengers; (p) < passengers + num_passengers; ++(p))
#define mutex_unlock(x) ((void)(x))
#define ssleep(x) ((void)(x))
static void unload_passengers(void) {
    int remaining = 0;
    for (int i = 0; i < num_passengers; ++i)
        if (passengers[i].dest_floor != current_floor)
            passengers[remaining++] = passengers[i];
    num_passengers = remaining;
}
static int simulate(int floor, int destinations) {
    Passenger *p;
    int needs_down;
    current_floor = floor;
    elevator_state = DEACTIVATING;
    num_passengers = 0;
    for (int i = 1; i <= MAX_FLOORS; ++i)
        if (destinations & (1 << (i - 1)))
            passengers[num_passengers++].dest_floor = i;
    for (int tick = 0; tick < 40 && elevator_state != OFFLINE; ++tick) {
        if (current_floor < 1 || current_floor > MAX_FLOORS) return 0;
BRANCH
    }
    return elevator_state == OFFLINE && num_passengers == 0;
}
int main(void) {
    int failed = 0;
    for (int floor = 1; floor <= MAX_FLOORS; ++floor)
        for (int mask = 0; mask < 32; ++mask)
            if (!simulate(floor, mask)) {
                if (!failed) printf("First failure: start floor=%d, destination bitmask=%d\n", floor, mask);
                ++failed;
            }
    printf("Shutdown scenarios: %d/160 passed\n", 160 - failed);
    return failed ? 1 : 0;
}
'''.replace('BRANCH', branch)
with tempfile.TemporaryDirectory() as folder:
    cfile = Path(folder) / 'shutdown.c'
    exe = Path(folder) / ('shutdown.exe' if os.name == 'nt' else 'shutdown')
    cfile.write_text(harness)
    subprocess.run(['gcc', '-std=c99', str(cfile), '-o', str(exe)], check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)

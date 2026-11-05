#ifndef ELEVATOR_H
#define ELEVATOR_H

int start_elevator(void);
int stop_elevator(void);
int issue_request(int start_floor, int destination_floor, int type);

#endif

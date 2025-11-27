#ifndef TRANSFERCONVEYOR_PROTOCOL_H
#define TRANSFERCONVEYOR_PROTOCOL_H

// Movement Commands
enum class MoveCommand {
    MOVE_TO_ROBOTIC_CELL,
    MOVE_TO_STORAGE_CELL,
    MOVE_TO_DISTANCE,
    STOP,
    GET_STATUS
};

// Movement Status
enum class MoveStatus {
    IDLE,
    QUEUED,
    MOVING,
    AT_ROBOTIC_CELL,
    AT_STORAGE_CELL,
    AT_POSITION,
    ERROR
};

// Request structure for M7 -> M4 communication
struct ConveyorRequest {
    int requestId;
    MoveCommand command;
    float targetDistance;  // Used for MOVE_TO_DISTANCE
};

// Response structure for M4 -> M7 communication
struct ConveyorResponse {
    int requestId;
    MoveStatus status;
    float currentPosition;
    bool roboticCellSwitch;
    bool storageCellSwitch;
};

#endif //TRANSFERCONVEYOR_PROTOCOL_H

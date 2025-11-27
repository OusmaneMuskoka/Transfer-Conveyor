#include <Arduino.h>
#include <RPC.h>
#include <AccelStepper.h>
#include <ezButton.h>
#include "main.h"
#include "protocol.h"

// Hardware objects
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIRECTION_PIN);
ezButton roboticCellSwitch(LIMIT_SWITCH_START_PIN);  // Robotic cell position
ezButton storageCellSwitch(LIMIT_SWITCH_END_PIN);    // Storage cell position

// Current state
volatile int currentRequestId = 0;
volatile MoveStatus currentStatus = MoveStatus::IDLE;
volatile MoveCommand activeCommand = MoveCommand::GET_STATUS;
volatile float targetDistance = 0;
volatile float currentPosition = 0;
volatile bool isBusy = false;

// Movement state machine
void executeMovement() {
    if (!isBusy) {
        return;
    }

    // Run the stepper
    stepper.run();

    // Update limit switches
    roboticCellSwitch.loop();
    storageCellSwitch.loop();

    // Check if we've reached target based on command type
    bool targetReached = false;

    switch (activeCommand) {
        case MoveCommand::MOVE_TO_ROBOTIC_CELL:
            if (roboticCellSwitch.isPressed()) {
                targetReached = true;
                currentStatus = MoveStatus::AT_ROBOTIC_CELL;
                currentPosition = 0; // Reset position at robotic cell
            }
            break;

        case MoveCommand::MOVE_TO_STORAGE_CELL:
            if (storageCellSwitch.isPressed()) {
                targetReached = true;
                currentStatus = MoveStatus::AT_STORAGE_CELL;
            }
            break;

        case MoveCommand::MOVE_TO_DISTANCE:
            if (stepper.distanceToGo() == 0) {
                targetReached = true;
                currentStatus = MoveStatus::AT_POSITION;
            }
            break;

        case MoveCommand::STOP:
            stepper.stop();
            targetReached = true;
            currentStatus = MoveStatus::IDLE;
            break;

        default:
            break;
    }

    // Stop motor when target reached
    if (targetReached) {
        stepper.stop();
        stepper.setCurrentPosition(stepper.currentPosition()); // Hold position
        isBusy = false;
        currentPosition = stepper.currentPosition();
        digitalWrite(LED_BUILTIN, HIGH); // LED on when stopped
    }
}

// RPC Functions callable by M7
void processRequest(int requestId, int command, float distance) {
    currentRequestId = requestId;
    activeCommand = (MoveCommand)command;
    targetDistance = distance;
    isBusy = true;
    currentStatus = MoveStatus::MOVING;

    digitalWrite(LED_BUILTIN, LOW); // LED off when moving

    switch (activeCommand) {
        case MoveCommand::MOVE_TO_ROBOTIC_CELL:
            // Move in negative direction to robotic cell
            stepper.move(-1000000); // Large negative move, will stop on limit switch
            stepper.setSpeed(-SPEED);
            break;

        case MoveCommand::MOVE_TO_STORAGE_CELL:
            // Move in positive direction to storage cell
            stepper.move(1000000); // Large positive move, will stop on limit switch
            stepper.setSpeed(SPEED);
            break;

        case MoveCommand::MOVE_TO_DISTANCE:
            // Move to specific position
            stepper.moveTo(distance);
            break;

        case MoveCommand::STOP:
            stepper.stop();
            isBusy = false;
            currentStatus = MoveStatus::IDLE;
            break;

        default:
            isBusy = false;
            currentStatus = MoveStatus::ERROR;
            break;
    }
}

bool getBusyStatus() {
    return isBusy;
}

int getStatus() {
    // Return current status data to M7
    // M7 will need to call additional RPC functions to get full status
    return currentRequestId;
}

int getStatusCode() {
    return (int)currentStatus;
}

float getCurrentPosition() {
    return currentPosition;
}

bool getRoboticSwitchState() {
    return roboticCellSwitch.isPressed();
}

bool getStorageSwitchState() {
    return storageCellSwitch.isPressed();
}

void setup() {
    // Initialize RPC
    RPC.begin();

    // Register RPC functions for M7
    RPC.bind("processRequest", processRequest);
    RPC.bind("isBusy", getBusyStatus);
    RPC.bind("getStatus", getStatus);
    RPC.bind("getStatusCode", getStatusCode);
    RPC.bind("getCurrentPosition", getCurrentPosition);
    RPC.bind("getRoboticSwitchState", getRoboticSwitchState);
    RPC.bind("getStorageSwitchState", getStorageSwitchState);

    // Setup stepper motor
    pinMode(DIRECTION_PIN, OUTPUT);
    pinMode(STEP_PIN, OUTPUT);
    stepper.setMaxSpeed(SPEED);
    stepper.setAcceleration(ACCELERATION);

    // Setup limit switches
    roboticCellSwitch.setDebounceTime(20);
    storageCellSwitch.setDebounceTime(20);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // LED on when idle

    currentStatus = MoveStatus::IDLE;
}

void loop() {
    // Execute movement state machine
    executeMovement();

    // Small delay to prevent overwhelming the loop
    delayMicroseconds(100);
}
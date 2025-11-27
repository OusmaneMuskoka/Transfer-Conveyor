#ifndef TRANSFERCONVEYOR_MAIN_H
#define TRANSFERCONVEYOR_MAIN_H

#define STEPS_PER_REVOLUTION 1600
#define GEAR_RATIO (100.0f / 15.0f)
#define SPEED 1500
#define SEEK_SPEED 200
#define ZERO_SPEED 50
#define SEEK_ACCELERATION 1000
#define ACCELERATION 600
#define DIRECTION_PIN 3
#define STEP_PIN 4
#define LIMIT_SWITCH_START_PIN 7
#define LIMIT_SWITCH_END_PIN 8
#define ESTOP_BUTTON_PIN 6
#define PRESSED 0

namespace arduino {
    class String;
}

void getCurrentLocation();
bool startSwitchOpen();

void turnOnLED();
void turnOffLED();
void zeroTable();
void stopMotor();
void sendResponse(int statusCode, arduino::String msg);
void correctPosition();
void removeBackLash(double value);
void move(float location);
void handleRequest();

#endif //TRANSFERCONVEYOR_MAIN_H
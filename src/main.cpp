#include "main.h"
#include "protocol.h"
#include <Arduino.h>
#include <RPC.h>

#include <OptaBlue.h>
#include <opta_info.h>
#include <Thread.h>
#include <mbed.h>
#include "DigitalExpansion.h"

#include <WiFi.h>
#include <Ethernet.h>
#include <ArduinoJson.h>
#include <queue>

// Network state
enum class NetworkType { NONE, ETHERNET, WIFI };
NetworkType activeNetwork = NetworkType::NONE;
unsigned long lastConnectionCheck = 0;
const unsigned long CONNECTION_CHECK_INTERVAL = 5000; // Check every 5 seconds

// Request queue for handling multiple requests
std::queue<ConveyorRequest> requestQueue;
int nextRequestId = 1;
EthernetServer server(80);

// WiFi credentials (user should set these)
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Network initialization functions
bool initEthernet() {
    Serial.println("Attempting Ethernet connection...");
    if (Ethernet.begin() == 0) {
        Serial.println("Failed to configure Ethernet using DHCP");
        return false;
    }

    // Check for Ethernet hardware
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        Serial.println("Ethernet hardware not found");
        return false;
    }

    if (Ethernet.linkStatus() == LinkOFF) {
        Serial.println("Ethernet cable is not connected");
        return false;
    }

    server.begin();
    Serial.print("Ethernet connected. IP: ");
    Serial.println(Ethernet.localIP());
    activeNetwork = NetworkType::ETHERNET;
    return true;
}

bool initWiFi() {
    Serial.println("Attempting WiFi connection...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nWiFi connection failed");
        return false;
    }

    server.begin();
    Serial.print("\nWiFi connected. IP: ");
    Serial.println(WiFi.localIP());
    activeNetwork = NetworkType::WIFI;
    return true;
}

void initNetwork() {
    // Try Ethernet first
    if (initEthernet()) {
        return;
    }

    // Fallback to WiFi
    Serial.println("Ethernet unavailable, trying WiFi...");
    if (initWiFi()) {
        return;
    }

    Serial.println("ERROR: No network connection available!");
    activeNetwork = NetworkType::NONE;
}

void checkAndReconnect() {
    unsigned long currentTime = millis();
    if (currentTime - lastConnectionCheck < CONNECTION_CHECK_INTERVAL) {
        return;
    }
    lastConnectionCheck = currentTime;

    bool connected = false;

    if (activeNetwork == NetworkType::ETHERNET) {
        connected = (Ethernet.linkStatus() == LinkON);
    } else if (activeNetwork == NetworkType::WIFI) {
        connected = (WiFi.status() == WL_CONNECTED);
    }

    if (!connected) {
        Serial.println("Connection lost! Attempting to reconnect...");
        activeNetwork = NetworkType::NONE;
        initNetwork();
    }
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
        ; // Wait for serial connection
    }

    Serial.println("=== Transfer Conveyor Controller ===");
    Serial.println("Initializing M4 core...");

    // Initialize RPC for M4 communication
    RPC.begin();

    // Wait for M4 to boot
    delay(1000);

    // Initialize network
    Serial.println("Initializing network...");
    initNetwork();

    pinMode(LED_BUILTIN, OUTPUT);
    Serial.println("Setup complete!");
}

// RPC functions to communicate with M4
void sendRequestToM4(const ConveyorRequest& req) {
    RPC.call("processRequest", req.requestId, (int)req.command, req.targetDistance);
}

ConveyorResponse getStatusFromM4() {
    ConveyorResponse response;

    // Call multiple RPC functions to get full status
    auto reqIdResult = RPC.call("getStatus");
    auto statusCodeResult = RPC.call("getStatusCode");
    auto positionResult = RPC.call("getCurrentPosition");
    auto roboticSwitchResult = RPC.call("getRoboticSwitchState");
    auto storageSwitchResult = RPC.call("getStorageSwitchState");

    if (reqIdResult) {
        response.requestId = reqIdResult.as<int>();
    }

    if (statusCodeResult) {
        response.status = (MoveStatus)statusCodeResult.as<int>();
    }

    if (positionResult) {
        response.currentPosition = positionResult.as<float>();
    }

    if (roboticSwitchResult) {
        response.roboticCellSwitch = roboticSwitchResult.as<bool>();
    }

    if (storageSwitchResult) {
        response.storageCellSwitch = storageSwitchResult.as<bool>();
    }

    return response;
}

void processRequestQueue() {
    if (requestQueue.empty()) {
        return;
    }

    // Check if M4 is busy
    auto busyResult = RPC.call("isBusy");
    if (!busyResult || busyResult.as<bool>()) {
        return; // M4 is still processing
    }

    // Send next request to M4
    ConveyorRequest req = requestQueue.front();
    requestQueue.pop();
    sendRequestToM4(req);
}

void loop() {
    // Check network connection periodically
    checkAndReconnect();

    // Process queued requests
    processRequestQueue();

    // Listen for incoming network requests
    if (activeNetwork != NetworkType::NONE) {
        listenAndServe();
    }

    delay(10); // Small delay to prevent busy-waiting
}

// Routes for the server. called by listenAndServe()
struct RequestData {
    EthernetClient client;
    String path;
    String method;
    String contentType;
    int contentLength{};
    String body;
};

void Response(const RequestData& req, const String& message, int statusCode = 200) {
    EthernetClient client = req.client;
    // Consider buffering the output or using a more efficient string handling
    client.print(F("HTTP/1.1 ")); // Using F() macro to save RAM
    client.println(statusCode);
    // ...rest of the response
    client.println("Content-type: application/json");
    client.println("Connection: close");
    client.println();

    client.println(message);
    client.println();
}

void Failed(const RequestData& req, const String& message) {
    Response(req, message, 400);
}

void Success(const RequestData& req, const String& message) {
    Response(req, message, 200);
}

// New routes for conveyor control
void moveToRoboticCell(const RequestData& req) {
    Serial.println("Request: Move to Robotic Cell");

    ConveyorRequest convReq;
    convReq.requestId = nextRequestId++;
    convReq.command = MoveCommand::MOVE_TO_ROBOTIC_CELL;
    convReq.targetDistance = 0;

    requestQueue.push(convReq);

    JsonDocument doc;
    doc["requestId"] = convReq.requestId;
    doc["status"] = "QUEUED";
    doc["message"] = "Request queued to move to robotic cell";

    String response;
    serializeJson(doc, response);
    Success(req, response);
}

void moveToStorageCell(const RequestData& req) {
    Serial.println("Request: Move to Storage Cell");

    ConveyorRequest convReq;
    convReq.requestId = nextRequestId++;
    convReq.command = MoveCommand::MOVE_TO_STORAGE_CELL;
    convReq.targetDistance = 0;

    requestQueue.push(convReq);

    JsonDocument doc;
    doc["requestId"] = convReq.requestId;
    doc["status"] = "QUEUED";
    doc["message"] = "Request queued to move to storage cell";

    String response;
    serializeJson(doc, response);
    Success(req, response);
}

void moveToDistance(const RequestData& req) {
    Serial.println("Request: Move to Distance");

    if (req.contentType != "application/json") {
        Failed(req, "Content-Type must be application/json");
        return;
    }

    JsonDocument doc;
    auto err = deserializeJson(doc, req.body);

    if (err) {
        Serial.println("JSON parse error");
        Failed(req, "Invalid JSON");
        return;
    }

    if (!doc.containsKey("distance")) {
        Failed(req, "Missing 'distance' field");
        return;
    }

    float distance = doc["distance"];

    ConveyorRequest convReq;
    convReq.requestId = nextRequestId++;
    convReq.command = MoveCommand::MOVE_TO_DISTANCE;
    convReq.targetDistance = distance;

    requestQueue.push(convReq);

    JsonDocument responseDoc;
    responseDoc["requestId"] = convReq.requestId;
    responseDoc["status"] = "QUEUED";
    responseDoc["message"] = "Request queued to move to distance";
    responseDoc["distance"] = distance;

    String response;
    serializeJson(responseDoc, response);
    Success(req, response);
}

void getConveyorStatus(const RequestData& req) {
    Serial.println("Request: Get Status");

    ConveyorResponse status = getStatusFromM4();

    JsonDocument doc;
    doc["requestId"] = status.requestId;
    doc["status"] = (int)status.status;
    doc["currentPosition"] = status.currentPosition;
    doc["roboticCellSwitch"] = status.roboticCellSwitch;
    doc["storageCellSwitch"] = status.storageCellSwitch;
    doc["queueLength"] = requestQueue.size();

    String response;
    serializeJson(doc, response);
    Success(req, response);
}

// Responds with html of the current state of the controller.
void index(const RequestData& req) {
    EthernetClient client = req.client;

    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println("Connection: close");
    client.println();

    client.println("<!DOCTYPE HTML>");
    client.println("<html>");
    client.println("<head>");
    client.println("<title>Transfer Conveyor Controller</title>");
    client.println("</head>");
    client.println("<body>");
    client.println("<h1>Transfer Conveyor Controller</h1>");
    client.println("<h2>API Endpoints:</h2>");
    client.println("<ul>");
    client.println("<li>POST /move/robotic - Move to robotic cell</li>");
    client.println("<li>POST /move/storage - Move to storage cell</li>");
    client.println("<li>POST /move/distance - Move to specific distance (JSON: {\"distance\": 100.0})</li>");
    client.println("<li>GET /conveyor/status - Get current status</li>");
    client.println("<li>GET /status - Get expansion module status</li>");
    client.println("</ul>");
    client.println("</body>");
    client.println("</html>");
    client.println();
}

// Responds with JSON containing the status of the 8 outputs on the expansion module.
void status(const RequestData &req) {
    Serial.println("Fetching status of the expansion module.");

    // Get the first expansion module
    DigitalExpansion exp = OptaController.getExpansion(0);
    if (!exp) {
        Serial.println("Expansion module not found.");
        Failed(req, "Expansion module not found.");
        return;
    }

    // Retrieve the status of the outputs
    JsonDocument doc;
    JsonArray outputs = doc["outputs"].to<JsonArray>();

    for (int i = 0; i < 8; i++) {
        bool state = exp.digitalOutRead(i); // Get the status of each output
        outputs.add(state);
    }

    // Serialize the JSON document and send the response
    String response;
    serializeJson(doc, response);
    Success(req, response);

    Serial.println("Status sent successfully.");
}

void setClamp(const RequestData& req) {
    Serial.println("Setting Clamp.");
    if (req.contentType == "application/json") {
        JsonDocument doc;
        auto err = deserializeJson(doc, req.body);

        if (err)
        {
            Serial.println("deserializeJson() failed: ");
            Serial.println(err.c_str());
            Failed(req, "Invalid JSON.");
            return;
        }
        int clampID = doc["id"];
        bool clampOn = doc["status"];

        if (clampID < 0 || clampID > 7) {
            Serial.println("Clamp ID out of range.");
            Failed(req, "Invalid clamp ID.");
            return;
        }

        Serial.println("Clamp ID: " + String(clampID));
        DigitalExpansion exp = OptaController.getExpansion(0);
        exp.digitalWrite(clampID, clampOn ? LOW : HIGH);
        exp.updateDigitalOutputs();
        JsonDocument responseDoc;
        responseDoc["id"] = clampID;
        responseDoc["status"] = true;
        String response;
        serializeJson(responseDoc, response);
        Success(req, response);
        Serial.println("Setting Clamp: " + String(clampID) + " to " + String(clampOn));
    } else {
        Serial.println("Invalid content type.");
    }
}

void setClamps(const RequestData& req) {
    Serial.println("Setting Clamp.");
    if (req.contentType == "application/json") {
        JsonDocument doc;
        auto err = deserializeJson(doc, req.body);

        if (err)
        {
            Serial.println("deserializeJson() failed: ");
            Serial.println(err.c_str());
            Failed(req, "Invalid JSON.");
            return;
        }

        DigitalExpansion exp = OptaController.getExpansion(0);
        JsonArray array = doc["clamps"].as<JsonArray>();
        JsonDocument responseDoc;
        JsonArray passedClamps = responseDoc["passed"].to<JsonArray>();
        JsonArray failedClamps = responseDoc["failed"].to<JsonArray>();

        for (JsonVariant v: array) {
            int clampID = v["id"];
            bool clampOn = v["status"];

            if (clampID < 0 || clampID > 7) {
                Serial.println("Clamp" + String(clampID) + "ID out of range.");
                failedClamps.add(clampID);  // Add failed clamp ID to "failed" array
                return;
            }
            Serial.println("Clamp ID: " + String(clampID) + "set");
            exp.digitalWrite(clampID, clampOn ? LOW : HIGH);
            passedClamps.add(clampID);  // Add passed clamp ID to "passed" array
        }

        exp.updateDigitalOutputs();
        String response;
        serializeJson(responseDoc, response);
        Success(req, response);
        Serial.println("");
    } else {
        Serial.println("Invalid content type.");
    }
}

void getClamp(const RequestData& req) {
    Serial.println("Getting Clamp Status.");
    // Verify if the content type is JSON
    if (req.contentType != "application/json") {
        Serial.println("Invalid content type. Expected application/json.");
        Failed(req, "Invalid content type. Expected application/json.");
        return;
    }
    // Parse ID from the request body
    JsonDocument doc;
    auto err = deserializeJson(doc, req.body);

    if (err) {
        Serial.println("Failed to parse JSON from request body: ");
        Serial.println(err.c_str());
        Failed(req, "Invalid JSON.");
        return;
    }

    int clampID = doc["id"];
    if (clampID < 0 || clampID > 7) {
        Serial.println("Invalid clamp ID in request body.");
        Failed(req, "Invalid clamp ID. Must be between 0 and 7.");
        return;
    }

    Serial.println("Clamp ID: " + String(clampID));
    // Get the first expansion module
    DigitalExpansion exp = OptaController.getExpansion(0);
    if (!exp) {
        Serial.println("Expansion module not found.");
        Failed(req, "Expansion module not found.");
        return;
    }

// Retrieve the status of the specific output
    bool clampStatus = exp.digitalOutRead(clampID);

// Build the response JSON
    JsonDocument responseDoc;
    responseDoc["id"] = clampID;
    responseDoc["status"] = clampStatus;

// Serialize the JSON document and send the response
    String response;
    serializeJson(responseDoc, response);
    Success(req, response);

    Serial.println("Clamp Status sent successfully: " + response);
}

RequestData buildRequest(EthernetClient client) {
    Serial.println("New Client.");
    // print a message out the serial port
    String currentLine = "";
    RequestData req;
    req.contentLength = -1;
    req.client = client;
    String path = client.readStringUntil('\n');
    int pathStart = path.indexOf(" ");
    int pathEnd = path.indexOf( " ", pathStart + 1);
    req.method = path.substring(0, pathStart);
    req.path = path.substring(pathStart + 1, pathEnd);

    while (client.connected()) {            // loop while the client's connected
        if (client.available()) {           // if there's bytes to read from the client,
            char c = client.read();         // read a byte, then
            Serial.write(c);                // print it out the serial monitor
            if (c == '\n') {
                // if the byte is a newline character
                // if the current line is blank, you got two newline characters in a row.
                // that's the end of the client HTTP request, so send a response:
                if (currentLine.length() == 0) {
                    String body = "";
                    if (req.contentLength != -1) {
                        char buffer[1024];
                        size_t res = client.readBytes(buffer, req.contentLength);
                        Serial.println(res);
                        Serial.println(buffer);
                        body = String(buffer);
                        req.body = body.substring(0, req.contentLength);
                    }
                    break;
                } else {      // if you got a newline, then clear currentLine:
                    if (currentLine.startsWith("Content-Length: ")) {
                        // Extract the number after "Content-Length: "
                        String lengthStr = currentLine.substring(16); // 16 is the length of "Content-Length: "
                        req.contentLength = lengthStr.toInt();
                    }

                    if (currentLine.startsWith("Content-Type:")) {
                        req.contentType = currentLine.substring(14);
                        Serial.println("Content-Type: " + req.contentType);
                    }

                    currentLine = "";
                }
            } else if (c != '\r') {    // if you got anything else but a carriage return character,
                currentLine += c;      // add it to the end of the currentLine
            }
        }
    }
    req.path.toLowerCase();
    return req;
}

void printConnecting() {
    static int dotCount = 0;
    dotCount = dotCount > 3 ? 0 : dotCount + 1;
    //Clear the line
    Serial.print("\\r\\x1b[2K");
    Serial.print("Reconnecting");
    for (int i = 0; i < dotCount; i++) {
        Serial.print(".");
    }
    Serial.flush();
}

void listenAndServe(){
    //Double check connection status, and update if not connected.
    auto linkStatus = Ethernet.linkStatus();
    if (linkStatus != LinkON) {
        int connectionStatus = Ethernet.begin();
        while (connectionStatus == WL_CONNECT_FAILED) {
            printConnecting();
            delay(1000);
            connectionStatus = Ethernet.begin();
        }
        //Waiting
        Serial.println("Connected to network, starting server.");
        server.begin();
        Serial.print("Use this URL to connect: http://");
        Serial.println(Ethernet.localIP());
        Serial.print("Device MAC address: ");
        Serial.println(Ethernet.macAddress());
        delay(2000);
    }

    auto client = server.accept();
    if (!client) {
        return;
    }
    Serial.println("Client Connected.");
    RequestData req = buildRequest(client);
    Serial.println("current request: ");
    Serial.println(req.path);
    Serial.println(req.method);

    // Conveyor movement routes
    if (req.path == "/move/robotic" && req.method == "POST") moveToRoboticCell(req);
    else if (req.path == "/move/storage" && req.method == "POST") moveToStorageCell(req);
    else if (req.path == "/move/distance" && req.method == "POST") moveToDistance(req);
    else if (req.path == "/conveyor/status" && req.method == "GET") getConveyorStatus(req);
    // Original expansion module routes
    else if (req.path == "/clamp" && req.method == "POST") setClamp(req);
    else if (req.path == "/clamps" && req.method == "POST") setClamps(req);
    else if (req.path == "/clamp" && req.method == "GET") getClamp(req);
    else if (req.path == "/status" && req.method == "GET") status(req);
    else if (req.path == "/" && req.method == "GET") index(req);
    else {
        Response(req, "Not Found", 404);
    }

    if (client) client.stop();
    Serial.println("Client Disconnected.");
}
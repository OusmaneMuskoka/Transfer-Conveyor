#include "main.h"
#include "protocol.h"
#include <Arduino.h>
#include <RPC.h>
#include <WiFi.h>
#include <Ethernet.h>
#include <ArduinoJson.h>
#include <queue>

// Network state
enum class NetworkType { NONE, ETHERNET, WIFI };
auto activeNetwork = NetworkType::NONE;
unsigned long lastConnectionCheck = 0;
constexpr unsigned long CONNECTION_CHECK_INTERVAL = 5000; // Check every 5 seconds

// Request queue for handling multiple requests
std::queue<ConveyorRequest> requestQueue;
int nextRequestId = 1;
EthernetServer server(80);

// Wi-Fi credentials (user should set these)
auto WIFI_SSID = "YOUR_WIFI_SSID";
auto WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

void listenAndServe();

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

    // Fallback to Wi-Fi
    Serial.println("Ethernet unavailable, trying WiFi...");
    if (initWiFi()) {
        return;
    }

    Serial.println("ERROR: No network connection available!");
    activeNetwork = NetworkType::NONE;
}

void checkAndReconnect() {
    const unsigned long currentTime = millis();
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
    RPC.call("processRequest", req.requestId, static_cast<int>(req.command), req.targetDistance);
}

ConveyorResponse getStatusFromM4() {
    // Call multiple RPC functions to get full status
    // RPC.call returns a msgpack handle that must be converted directly.
    // We cannot check 'if (result)' because it is not a pointer.
    ConveyorResponse response{};
    response.requestId = RPC.call("getStatus").as<int>();
    response.status = static_cast<MoveStatus>(RPC.call("getStatusCode").as<int>());
    response.currentPosition = RPC.call("getCurrentPosition").as<float>();
    response.roboticCellSwitch = RPC.call("getRoboticSwitchState").as<bool>();
    response.storageCellSwitch = RPC.call("getStorageSwitchState").as<bool>();

    return response;
}

void processRequestQueue() {
    if (requestQueue.empty()) {
        return;
    }

    // Check if M4 is busy
    // We directly extract the bool. If the RPC call had failed completely,
    // the system would likely hang or throw an exception before reaching here.
    if (RPC.call("isBusy").as<bool>()) {
        return; // M4 is still processing
    }

    // Send next request to M4
    const ConveyorRequest req = requestQueue.front();
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

void Response(const RequestData& req, const String& message, const int statusCode = 200) {
    EthernetClient client = req.client;
    client.print(F("HTTP/1.1 "));
    client.println(statusCode);
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

    ConveyorRequest convReq{};
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

    ConveyorRequest convReq{};
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
    const auto err = deserializeJson(doc, req.body);

    if (err) {
        Serial.println("JSON parse error");
        Failed(req, "Invalid JSON");
        return;
    }

    if (!doc["distance"]) {
        Failed(req, "Missing 'distance' field");
        return;
    }

    const auto distance = doc["distance"].as<float>();

    ConveyorRequest convReq{};
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
    doc["status"] = static_cast<int>(status.status);
    doc["currentPosition"] = status.currentPosition;
    doc["roboticCellSwitch"] = status.roboticCellSwitch;
    doc["storageCellSwitch"] = status.storageCellSwitch;
    doc["queueLength"] = requestQueue.size();

    String response;
    serializeJson(doc, response);
    Success(req, response);
}

// Responds with HTML of the current state of the controller.
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
    client.println("</ul>");
    client.println("</body>");
    client.println("</html>");
    client.println();
}


RequestData buildRequest(EthernetClient client) {
    Serial.println("New Client.");
    // print a message out the serial port
    String currentLine = "";
    RequestData req;
    req.contentLength = -1;
    req.client = client;
    const String path = client.readStringUntil('\n');
    const int pathStart = path.indexOf(" ");
    const int pathEnd = path.indexOf( " ", pathStart + 1);
    req.method = path.substring(0, pathStart);
    req.path = path.substring(pathStart + 1, pathEnd);

    while (client.connected()) {            // loop while the client's connected
        if (client.available()) {           // if there are bytes to read from the client,
            const char c = client.read();         // read a byte, then
            Serial.write(c);                // print it out the serial monitor
            if (c == '\n') {
                // if the byte is a newline character,
                // if the current line is blank, you got two newline characters in a row.
                // that's the end of the client HTTP request, so send a response:
                if (currentLine.length() == 0) {
                    String body = "";
                    if (req.contentLength != -1) {
                        char buffer[1024];
                        const size_t res = client.readBytes(buffer, req.contentLength);
                        Serial.println(res);
                        Serial.println(buffer);
                        body = String(buffer);
                        req.body = body.substring(0, req.contentLength);
                    }
                    break;
                } else {      // if you got a newline, then clear the currentLine:
                    if (currentLine.startsWith("Content-Length: ")) {
                        // Extract the number after "Content-Length":
                        String lengthStr = currentLine.substring(16); // 16 is the length of "Content-Length:"
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

void listenAndServe(){
    auto client = server.accept();
    if (!client) {
        return;
    }

    Serial.println("Client Connected.");
    const RequestData req = buildRequest(client);
    Serial.print("Request: ");
    Serial.print(req.method);
    Serial.print(" ");
    Serial.println(req.path);

    // Conveyor movement routes
    if (req.path == "/move/robotic" && req.method == "POST") {
        moveToRoboticCell(req);
    } else if (req.path == "/move/storage" && req.method == "POST") {
        moveToStorageCell(req);
    } else if (req.path == "/move/distance" && req.method == "POST") {
        moveToDistance(req);
    } else if (req.path == "/conveyor/status" && req.method == "GET") {
        getConveyorStatus(req);
    } else if (req.path == "/" && req.method == "GET") {
        index(req);
    } else {
        Response(req, R"({"error":"Not Found"})", 404);
    }

    client.stop();
    Serial.println("Client Disconnected.");
}
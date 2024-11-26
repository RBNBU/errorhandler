#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <MQTTClient.h>

#define ADDRESS "tcp://192.168.0.102:1883"       // MQTT broker URL
#define CLIENTID "Rpi ruben"             // Unique client ID
#define SUB_TOPIC "errors/receive/ruben"               // MQTT subscription topic
#define PUB_TOPIC "errors/processed/ruben"             // MQTT publication topic
#define QOS 1                                    // Quality of Service level
#define TIMEOUT 10000L                           // Timeout in milliseconds

typedef struct ErrorNode {
    char code[10];
    char message[256];
    struct ErrorNode* next;
} ErrorNode;

volatile MQTTClient_deliveryToken deliveredtoken;
ErrorNode* errors = NULL;
void handleError(const char* lang, const char* deviceName, const char* severity, const char* errorCode, const char* extraInfo, MQTTClient* client);
void logError(const char* errorMsg);
ErrorNode* readErrorFile(const char* fileName);
void freeErrorList(ErrorNode* head);
void delivered(void *context, MQTTClient_deliveryToken dt);
int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message);
void connlost(void *context, char *cause);

int main(int argc, char* argv[]) {
    printf("Program started\n"); // Debug output

    if (argc < 2) {
        logError("Usage: <program_name> <language>");
        return -1;
    }

    const char* lang = argv[1];
    char errorFile[256];
    sprintf(errorFile, "./Error_msg_%s.txt", lang);

    printf("Loading error file: %s\n", errorFile); // Debug output

    errors = readErrorFile(errorFile);
    if (errors == NULL) {
        logError("Failed to read error messages from file");
        return -1;
    }

    // Initialize the MQTT client
    printf("Initializing MQTT client\n"); // Debug output
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    rc = MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        logError("Failed to create MQTT client");
        freeErrorList(errors);
        return -1;
    }

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    printf("Setting MQTT callbacks\n"); // Debug output
    MQTTClient_setCallbacks(client, &client, connlost, msgarrvd, delivered);

    printf("Connecting to MQTT broker\n"); // Debug output
    rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect to MQTT broker, return code %d\n", rc); // Debug output
        handleError(lang, "noDevice", "0", "002", "Failed to connect to MQTT broker", &client);
        freeErrorList(errors);
        MQTTClient_destroy(&client);
        return -1;
    } else {
        printf("Successfully connected to MQTT broker\n"); // Debug output
    }

    printf("Subscribing to topic %s\n", SUB_TOPIC); // Debug output
    rc = MQTTClient_subscribe(client, SUB_TOPIC, QOS);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("Failed to subscribe to topic, return code %d\n", rc); // Debug output
        handleError(lang, "noDevice", "0", "003", "Failed to subscribe to topic", &client);
        freeErrorList(errors);
        MQTTClient_destroy(&client);
        return -1;
    } else {
        printf("Successfully subscribed to topic %s\n", SUB_TOPIC); // Debug output
    }

    while (1) {
        printf("Calling MQTTClient_yield()\n"); // Debug output
        MQTTClient_yield();
    }

    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    freeErrorList(errors);

    return 0;
}

void handleError(const char* lang, const char* deviceName, const char* severity, const char* errorCode, const char* extraInfo, MQTTClient* client) {
    if (client == NULL) {
        logError("Invalid MQTT client pointer");
        return;
    }

    ErrorNode* current = errors;
    char error[256] = "";
    while (current != NULL) {
        if (strcmp(current->code, errorCode) == 0) {
            strcpy(error, current->message);
            break;
        }
        current = current->next;
    }

    if (strlen(error) == 0) {
        char errMsg[256];
        sprintf(errMsg, "Error code %s not found", errorCode);
        logError(errMsg);
        return;
    }

    // Add extra explanation and timestamp
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char date[20];
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", t);

    // Prepare the message to be sent
    char payload[512];
    printf("Preparing payload with deviceName: %s, severity: %s errorCode: %s, error: %s, extraInfo: %s, date: %s\n", deviceName, severity, errorCode, error, extraInfo, date); // Debug output

    if (snprintf(payload, sizeof(payload), "%s;%s;%s;%s;%s;%s", deviceName, severity, errorCode, error, extraInfo, date) >= sizeof(payload)) {
        logError("Payload too large");
        return;
    }

    printf("Prepared payload: %s\n", payload); // Debug output

    // Ensure payload duplication
    char* payload_dup = strdup(payload);
    if (payload_dup == NULL) {
        logError("Failed to duplicate payload");
        return;
    }

    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = payload_dup;
    pubmsg.payloadlen = (int)strlen(payload_dup);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;

    printf("Publishing message with payload: %s\n", payload_dup); // Debug output

    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(client, PUB_TOPIC, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        logError("Failed to publish error message");
    } else {
        rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
        if (rc == MQTTCLIENT_SUCCESS) {
            printf("Error message with delivery token %d delivered\n", token);
        } else {
            logError("Failed to deliver error message");
        }
    }

    free(payload_dup); // Free the duplicated payload
}


void logError(const char* errorMsg) {
    fprintf(stderr, "Error: %s\n", errorMsg);
}

ErrorNode* readErrorFile(const char* fileName) {
    FILE *file = fopen(fileName, "r");
    if (file == NULL) {
        char errMsg[256];
        sprintf(errMsg, "Error opening error message file: %s", fileName);
        logError(errMsg);
        return NULL;
    }

    ErrorNode* head = NULL;
    ErrorNode* tail = NULL;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Skip lines that start with '#' or are blank
        if (line[0] == '#' || line[0] == '\n') continue;

        ErrorNode* newNode = (ErrorNode*)malloc(sizeof(ErrorNode));
        if (newNode == NULL) {
            logError("Memory allocation error");
            fclose(file);
            freeErrorList(head);
            return NULL;
        }

        // Expecting lines in the format "ErrCode\tErr text"
        char* code = strtok(line, "\t");
        char* message = strtok(NULL, "\n");
        if (code != NULL && message != NULL) {
            strcpy(newNode->code, code);
            strcpy(newNode->message, message);
            newNode->next = NULL;

            if (tail == NULL) {
                head = tail = newNode;
            } else {
                tail->next = newNode;
                tail = newNode;
            }
        } else {
            free(newNode);
        }
    }

    fclose(file);
    return head;
}

void freeErrorList(ErrorNode* head) {
    while (head != NULL) {
        ErrorNode* temp = head;
        head = head->next;
        free(temp);
    }
}

void delivered(void *context, MQTTClient_deliveryToken dt) {
    printf("Message with token value %d delivery confirmed\n", dt);
    deliveredtoken = dt;
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    if (context == NULL) {
        logError("NULL context received in msgarrvd");
        return 0;
    }

    printf("Message arrived on topic %s: %.*s\n", topicName, message->payloadlen, (char*)message->payload);

    char severity[10] = "", deviceName[256] = "", errorCode[10] = "", optionalText[256] = "";
    int parsed = sscanf((char*)message->payload, "%9[^;];%255[^;];%9[^;];%255[^\n]", severity, deviceName, errorCode, optionalText);

    if (parsed < 3) {
        logError("Failed to parse MQTT message payload");
        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
        return 0;
    }

    printf("Parsed data - Severity: %s, Device Name: %s, Error Code: %s, Optional Text: %s\n", severity, deviceName, errorCode, optionalText);

    handleError("EN", deviceName, severity, errorCode, optionalText, (MQTTClient*)context);

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}


void connlost(void *context, char *cause) {
    logError("Connection lost");
}

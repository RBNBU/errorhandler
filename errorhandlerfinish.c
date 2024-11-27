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
#define TIMEOUT 100L                           // Timeout in milliseconds
#define ERR_OUT_LEN 1024                    // Length for outgoing error messages


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
    printf("Program started\n");

    if (argc < 2) {
        logError("Usage: <program_name> <language>");
        return -1;
    }

    const char* lang = argv[1];
    char errorFile[256];
    sprintf(errorFile, "./Error_msg_%s.txt", lang);

    printf("Loading error file: %s\n", errorFile);
    errors = readErrorFile(errorFile);
    if (errors == NULL) {
        logError("Failed to read error messages from file");
        return -1;
    }

    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    MQTTClient_setCallbacks(client, client, connlost, msgarrvd, delivered);

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", rc);
        handleError(lang, "noDevice", "0", "002", "Failed to connect to MQTT broker", &client);
        freeErrorList(errors);
        MQTTClient_destroy(&client);
        return -1;
    }

    printf("Subscribing to topic %s\n", SUB_TOPIC);
    if ((rc = MQTTClient_subscribe(client, SUB_TOPIC, QOS)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to subscribe, return code %d\n", rc);
        handleError(lang, "noDevice", "0", "003", "Failed to subscribe to topic", &client);
        freeErrorList(errors);
        MQTTClient_destroy(&client);
        return -1;
    }

    while (1) {
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

    char error_out[ERR_OUT_LEN] = "";

    if (strlen(error) == 0) {
        // If error code not found, log it and include extra info
        snprintf(error_out, sizeof(error_out), "Error code %s not found; %s", errorCode, extraInfo);
    } else {
        // Add timestamp to the outgoing message
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char date[20];
        strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", t);

        // Prepare the message to be sent
        printf("Preparing payload with deviceName: %s, severity: %s errorCode: %s, error: %s, extraInfo: %s, date: %s\n", 
            deviceName, severity, errorCode, error, extraInfo, date); // Debug output

        snprintf(error_out, sizeof(error_out), "%s;%s;%s;%s;%s;%s", deviceName, severity, errorCode, error, extraInfo, date);
    }

    printf("Prepared error_out: %s\n", error_out); // Debug output

    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = error_out;
    pubmsg.payloadlen = (int)strlen(error_out);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;

    printf("Publishing error_out: %s\n", error_out); // Debug output

    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(client, PUB_TOPIC, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        logError("Failed to publish error message");
    } else {
        rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
        if (rc == MQTTCLIENT_SUCCESS) {
            printf("Error message with delivery token %d delivered\n", token);
        } /*else {
            logError("Failed to deliver error message");
        }*/
    }
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
        if (code && message) {
            strcpy(newNode->code, code);
            strcpy(newNode->message, message);
            newNode->next = NULL;

            if (!tail) {
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
    while (head) {
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
    printf("Message arrived: %.*s\n", message->payloadlen, (char*)message->payload);

    char error_in[256];
    snprintf(error_in, sizeof(error_in), "%.*s", message->payloadlen, (char*)message->payload);

    char severity[10] = "0";
    char deviceName[256] = "UnknownDevice";
    char errorCode[10] = "000";
    char extraInfo[256] = "NoExtraInfo";

    sscanf(error_in, "%9[^;];%255[^;];%9[^;];%255[^\n]", severity, deviceName, errorCode, extraInfo);

    handleError("EN", deviceName, severity, errorCode, extraInfo, (MQTTClient*)context);

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}


void connlost(void *context, char *cause) {
    logError("Connection lost");
    printf("Cause: %s\n", cause);
}

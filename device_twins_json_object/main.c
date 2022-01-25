/* Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License.
 *
 * This example is built on the Azure Sphere DevX library.
 *   1. DevX is an Open Source community-maintained implementation of the Azure Sphere SDK samples.
 *   2. DevX is a modular library that simplifies common development scenarios.
 *        - You can focus on your solution, not the plumbing.
 *   3. DevX documentation is maintained at https://github.com/gloveboxes/AzureSphereDevX/wiki
 *	 4. The DevX library is not a substitute for understanding the Azure Sphere SDK Samples.
 *          - https://github.com/Azure/azure-sphere-samples
 *
 * DEVELOPER BOARD SELECTION
 *
 * The following developer boards are supported.
 *
 *	 1. AVNET Azure Sphere Starter Kit.
 *   2. AVNET Azure Sphere Starter Kit Revision 2.
 *	 3. Seeed Studio Azure Sphere MT3620 Development Kit aka Reference Design Board or rdb.
 *	 4. Seeed Studio Seeed Studio MT3620 Mini Dev Board.
 *
 * ENABLE YOUR DEVELOPER BOARD
 *
 * Each Azure Sphere developer board manufacturer maps pins differently. You need to select the
 *    configuration that matches your board.
 *
 * Follow these steps:
 *
 *	 1. Open CMakeLists.txt.
 *	 2. Uncomment the set command that matches your developer board.
 *	 3. Click File, then Save to auto-generate the CMake Cache.
 *
 ************************************************************************************************/

#include "main.h"

// Validate sensor readings and report device twins
static void report_now_handler(EventLoopTimer *eventLoopTimer)
{
    if (ConsumeEventLoopTimerEvent(eventLoopTimer) != 0) {
        dx_terminate(DX_ExitCode_ConsumeEventLoopTimeEvent);
        return;
    }

    if (!dx_isAzureConnected()) {
        return;
    }

    float temperature = 25.05f;
    // Add some random value to humidity to trigger data out of range error
    double humidity = 50.00 + (rand() % 70);

    // Validate sensor data to check within expected range
    // Is temperature between -20 and 60, is humidity between 0 and 100
    if (-20.0f < temperature && 60.0f > temperature && 0.0 <= humidity && 100.0 >= humidity) {

        // Update twin with current UTC (Universal Time Coordinate) in ISO format
        dx_deviceTwinReportValue(&dt_reported_utc, dx_getCurrentUtc(msgBuffer, sizeof(msgBuffer)));

        // The type passed in must match the Divice Twin Type DX_DEVICE_TWIN_FLOAT
        dx_deviceTwinReportValue(&dt_reported_temperature, &temperature);

        // The type passed in must match the Divice Twin Type DX_DEVICE_TWIN_DOUBLE
        dx_deviceTwinReportValue(&dt_reported_humidity, &humidity);

    } else { 
        // Report data not in range        
        if (dx_jsonSerialize(msgBuffer, sizeof(msgBuffer), 4, 
                            DX_JSON_STRING, "Sensor", "Environment", 
                            DX_JSON_STRING, "ErrorMessage", "Telemetry out of range", 
                            DX_JSON_FLOAT, "Temperature", temperature,
                            DX_JSON_DOUBLE, "Humidity", humidity)) {

            Log_Debug("%s\n", msgBuffer);

            // Publish sensor out of range error message.
            // The message metadata type property is set to SensorError.
            // Using IoT Hub Message Routing you would route the SensorError messages to a maintainance system.
            dx_azurePublish(msgBuffer, strlen(msgBuffer), sensorErrorProperties,
                            NELEMS(sensorErrorProperties), &contentProperties);
        }
    }
}

static void dt_desired_sample_rate_handler(DX_DEVICE_TWIN_BINDING *deviceTwinBinding)
{
    // validate data is sensible range before applying
    if (deviceTwinBinding->twinType == DX_DEVICE_TWIN_INT &&
        *(int *)deviceTwinBinding->propertyValue >= 0 &&
        *(int *)deviceTwinBinding->propertyValue <= 120) {

        dx_timerChange(&report_now_timer,
                       &(struct timespec){*(int *)deviceTwinBinding->propertyValue, 0});

        dx_deviceTwinAckDesiredValue(deviceTwinBinding, deviceTwinBinding->propertyValue,
                                     DX_DEVICE_TWIN_RESPONSE_COMPLETED);

    } else {
        dx_deviceTwinAckDesiredValue(deviceTwinBinding, deviceTwinBinding->propertyValue,
                                     DX_DEVICE_TWIN_RESPONSE_ERROR);
    }

    /*	Casting device twin state examples

            float value = *(float*)deviceTwinBinding->property_value;
            double value = *(double*)deviceTwinBinding->property_value;
            int value = *(int*)deviceTwinBinding->property_value;
            bool value = *(bool*)deviceTwinBinding->property_value;
            char* value = (char*)deviceTwinBinding->property_value;
    */
}

// check string contain only printable characters
// ! " # $ % & ' ( ) * + , - . / 0 1 2 3 4 5 6 7 8 9 : ; < = > ? @ A B C D E F G H I J K L M N O P Q
// R S T U V W X Y Z [ \ ] ^ _ ` a b c d e f g h i j k l m n o p q r s t u v w x y z { | } ~
bool IsDataValid(char *data)
{
    while (isprint(*data)) {
        data++;
    }
    return 0x00 == *data;
}

// Sample device twin handler that demonstrates how to manage string device twin types.  When an
// application uses a string device twin, the application must make a local copy of the string on
// any device twin update. This gives you memory control as strings can be of arbitrary length.
static void dt_copy_string_handler(DX_DEVICE_TWIN_BINDING *deviceTwinBinding)
{
    char *property_value = (char *)deviceTwinBinding->propertyValue;

    // Validate data. Is data type string, size less than destination buffer and printable characters
    if (deviceTwinBinding->twinType == DX_DEVICE_TWIN_STRING &&
        strlen(property_value) < sizeof(copy_of_property_value) && IsDataValid(property_value)) {

        strncpy(copy_of_property_value, property_value, sizeof(copy_of_property_value));

        // Output the new string to debug
        Log_Debug("Rx device twin update for twin: %s, local value: %s\n",
                  deviceTwinBinding->propertyName, copy_of_property_value);

        dx_deviceTwinAckDesiredValue(deviceTwinBinding, (char *)deviceTwinBinding->propertyValue,
                                     DX_DEVICE_TWIN_RESPONSE_COMPLETED);

    } else {
        Log_Debug("Local copy failed. String too long or invalid data\n");
        dx_deviceTwinAckDesiredValue(deviceTwinBinding, (char *)deviceTwinBinding->propertyValue,
                                     DX_DEVICE_TWIN_RESPONSE_ERROR);
    }
}

// Set Network Connected state LED
static void ConnectionStatus(bool connection_state) {
    dx_gpioStateSet(&network_connected_led, connection_state);
}

static void dt_gpio_handler(DX_DEVICE_TWIN_BINDING *deviceTwinBinding)
{
    // Verify that the context pointer is non-null
    if(deviceTwinBinding->context != NULL){
        
        // Cast the context pointer so we can access the GPIO Binding details
        DX_GPIO_BINDING *gpio = (DX_GPIO_BINDING*)deviceTwinBinding->context;
        
        bool gpio_level = *(bool *)deviceTwinBinding->propertyValue;
    
        if(gpio_level){
            dx_gpioOn(gpio);
        }
        else{
            dx_gpioOff(gpio);
        }
        dx_deviceTwinAckDesiredValue(deviceTwinBinding, deviceTwinBinding->propertyValue, DX_DEVICE_TWIN_RESPONSE_COMPLETED);
    }
    else{
        dx_deviceTwinAckDesiredValue(deviceTwinBinding, deviceTwinBinding->propertyValue, DX_DEVICE_TWIN_RESPONSE_ERROR);
    }
}

// Sample device twin handler that demonstrates how to manage JSON Object device twin types.  
// When passing a JSON Object into a device twin handler, the handler gets a JSON_Object pointer to 
// the value part of the ("key": value) pair.
//
// For example, this sample application expects the following JSON Object to be defined in the Device
// Twin "Desired Properties" Object.
//
// This example uses a JSON {"key": value} pair, where the value part is a JSON Object . . . 
//
//		"SampleJsonObject": {
//		    "keyBool": true,
//		    "keyInt": 2,
//		    "keyFloat": 32.35,
//		    "keyDouble": 4567.891,
//		    "keyString": "Avnet knows IoT!!",
//		    "keyJsonObj": {
//			    "nestedKeyInt": 12
//		    }
//		}
//
// When the handler gets control DevX has already validated that the "SampleJsonObject" key was found,
// and *root_object points to the JSON object.  From here we just need to understand what the object
// "key": value pairs look like and the application code can pull the data from the object.
//
//	-->{
//		"keyBool": true,
//		"keyInt": 2,
//		"keyFloat": 32.35,
//		"keyDouble": 4567.891,
//		"keyString": "Avnet knows IoT!!",
//		"keyJsonObj": {
//			"nestedKeyInt": 12
//		}
//
static void dt_json_object_handler(DX_DEVICE_TWIN_BINDING *deviceTwinBinding)
{

#define MAX_STRING_LEN 64
#define MAX_RESPONSE_LENGTH 256

    // Define variables to hold the expected data from the incomming JSON Object
    bool key_bool_value = true;
    int key_int_value = 0;
    float key_float_value = 0.0F;
    double key_double_value = 0.0L;
    char key_string_value[MAX_STRING_LEN] = {0x00};
    JSON_Object *key_nested_json_obj = NULL;
    int key_nested_key_int_value = 0;
    char reported_property_string[MAX_RESPONSE_LENGTH] = {0X00};

    // Cast the incomming propertyValue to a JSON_Object and verify we have 
    // a valid pointer
    JSON_Object *root_object = (JSON_Object *)deviceTwinBinding->propertyValue;
    if(root_object != NULL){

        // At this point root_object points to the {"key", value) value payload that is a JSON Oject
        // The developer must understand what the JSON Object looks like so data can be pulled. 

        // The SampleJsonObject properties should have a "keyBool" key
        if (json_object_has_value(root_object, "keyBool") != 0) {
            key_bool_value = (bool)json_object_get_boolean(root_object, "keyBool");
            Log_Debug("keyBool = %s\n", key_bool_value? "true": "false");
        } else {
            Log_Debug("keyBool not found!\n");
        }

        // The SampleJsonObject properties should have a "keyInt" key
        if (json_object_has_value(root_object, "keyInt") != 0) {
            key_int_value = (int)json_object_get_number(root_object, "keyInt");
            Log_Debug("keyInt = %d\n", key_int_value);
        } else {
            Log_Debug("keyInt not found!\n");
        }

        // The SampleJsonObject properties should have a "keyFloat" key
        if (json_object_has_value(root_object, "keyFloat") != 0) {
            key_float_value = (float)json_object_get_number(root_object, "keyFloat");
            Log_Debug("keyFloat = %.2f\n", key_float_value);
        } else {
            Log_Debug("keyFloat not found!\n");
        }

        // The SampleJsonObject properties should have a "keyDouble" key
        if (json_object_has_value(root_object, "keyDouble") != 0) {
            key_double_value = (double)json_object_get_number(root_object, "keyDouble");
            Log_Debug("keyDouble = %.4lf\n", key_double_value);
        } else {
            Log_Debug("keyDouble not found!\n");
        }

        // The SampleJsonObject properties should have a "keyString" key
        if (json_object_has_value(root_object, "keyString") != 0) {
            strncpy(key_string_value, json_object_get_string(root_object,"keyString"), MAX_STRING_LEN);
            Log_Debug("keyString = %s\n", key_string_value);
        } else {
            Log_Debug("keyString not found!\n");
        }      

        // When we have nested objects within an object, we get a new JSON_Object pointer to the 
        // nested object and pull data as above.

        // The SampleJsonObject properties should have a "keyJsonObj" key
        if (json_object_has_value(root_object, "keyJsonObj") != 0) {

            // Check to see if the object contains a "keyJsonObj" object
            key_nested_json_obj = json_object_get_object(root_object, "keyJsonObj");
            if (key_nested_json_obj == NULL) {
                Log_Debug("keyJsonObj not found\n");
            }

            // The keyJsonObj properties should have a "nestedKeyInt" key
            if (json_object_has_value(key_nested_json_obj, "nestedKeyInt") != 0) {
                key_nested_key_int_value = (int)json_object_get_number(key_nested_json_obj, "nestedKeyInt");
                Log_Debug("nestedKeyInt = %d\n", key_nested_key_int_value);
            } else {
                Log_Debug("nestedKeyInt not found!\n");
            }
        } else {
            Log_Debug("keyJsonObj not found!\n");
        }      

        // We need to manually build the response JSON
        
        // Define the JSON structure
        static const char reportedPropFormat[] = "{\"keyBool\": %s,\"keyInt\": %d,\"keyFloat\": %.2f,\"keyDouble\": %.4lf,\"keyString\": \"%s\",\"keyJsonObj\":{\"keyInt\": %d}}";
    
        snprintf(reported_property_string, MAX_RESPONSE_LENGTH, reportedPropFormat, key_bool_value ? "true": "false",
                                                                                    key_int_value, 
                                                                                    key_float_value,
                                                                                    key_double_value,
                                                                                    key_string_value,
                                                                                    key_nested_key_int_value);
        Log_Debug("DT Reported: %s\n", reported_property_string);

        dx_deviceTwinAckDesiredValue(deviceTwinBinding, reported_property_string, DX_DEVICE_TWIN_RESPONSE_COMPLETED);
        
        //dx_deviceTwinReportValue(deviceTwinBinding, reported_property_string);

    }
}

/// <summary>
///  Initialize peripherals, device twins, direct methods, timers.
/// </summary>
static void InitPeripheralsAndHandlers(void)
{
    dx_azureConnect(&dx_config, NETWORK_INTERFACE, IOT_PLUG_AND_PLAY_MODEL_ID);
    dx_timerSetStart(timer_binding_set, NELEMS(timer_binding_set));
    dx_gpioSetOpen(gpio_binding_set, NELEMS(gpio_binding_set));
    dx_deviceTwinSubscribe(device_twin_bindings, NELEMS(device_twin_bindings));

    dx_azureRegisterConnectionChangedNotification(ConnectionStatus);
    // Init the random number generator. Used to synthesis humidity telemetry
    srand((unsigned int)time(NULL));
}

/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void)
{
    dx_deviceTwinUnsubscribe();
    dx_timerSetStop(timer_binding_set, NELEMS(timer_binding_set));
    dx_gpioSetClose(gpio_binding_set, NELEMS(gpio_binding_set));
    dx_timerEventLoopStop();
}

int main(int argc, char *argv[])
{
    dx_registerTerminationHandler();
    if (!dx_configParseCmdLineArguments(argc, argv, &dx_config)) {
        return dx_getTerminationExitCode();
    }
    InitPeripheralsAndHandlers();

    // Main loop
    while (!dx_isTerminationRequired()) {
        int result = EventLoop_Run(dx_timerGetEventLoop(), -1, true);
        // Continue if interrupted by signal, e.g. due to breakpoint being set.
        if (result == -1 && errno != EINTR) {
            dx_terminate(DX_ExitCode_Main_EventLoopFail);
        }
    }

    ClosePeripheralsAndHandlers();
    return dx_getTerminationExitCode();
}
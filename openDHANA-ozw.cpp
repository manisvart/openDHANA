/**
 * @file
 * @author  Carl Nordin <openDHANA@manisvart.se>
 * @version 0.1
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "openDHANA-mqtt.h"


#define OPTIONS_FILE_OZW    "/etc/openDHANA/ozw/openDHANA-ozw.options"
#define OZW_USER_PATH       "/etc/openDHANA/ozw/"
#define OZW_CONFIG_PATH     "/usr/local/etc/openzwave/"

#define OZW_INFO(facility, message)         if (openDHANA_ozw_debug) openDHANA__generic__log_info (facility, message, __func__, __LINE__)


#include "Options.h"
#include "Manager.h"
#include "Driver.h"
#include "Node.h"
#include "Group.h"
#include "Notification.h"
#include "ValueID.h"
#include "value_classes/ValueStore.h"
#include "value_classes/Value.h"
#include "value_classes/ValueBool.h"
#include "platform/Log.h"
#include "CommandClass.h"

bool openDHANA_ozw_debug = false;
bool dhana_ozw_ignore_duplicate_messages = true;

typedef enum
{
  state_none, state_driver_ready, state_awake_nodes, state_all_nodes
} stack_state;

stack_state openDHANA_stack_state = state_none;

typedef enum
{
  gate_none, gate_awake_nodes, gate_all_nodes
} message_gate;

message_gate openDHANA_message_gate = gate_awake_nodes;



string_map value_cache;

class raw_message
{
public:
  string internal_topic;
  string message;
};
//bool in_startup = true;
std::queue<raw_message> startup_queue;
CREATE_LOCK (startup_queue);


using namespace OpenZWave;

static std::map<int, std::string> valueTypeToString;
static std::map<std::string, int> stringToValueType;
static std::map<int, std::string> genreToString;
static std::map<std::string, int> stringToGenre;

static std::map<int, std::string> commandClassToString;
static std::map<std::string, int> stringToCommandClass;

uint32 homeID;

//typedef struct
//{
//  uint32 m_homeId;
//  uint8 m_nodeId;
//  bool m_polled;
//  list<ValueID> m_values;
//} NodeInfo;

//static list<NodeInfo*> g_nodes;

static pthread_mutex_t g_criticalSection;
//static pthread_cond_t  initCond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;

/// Create a string representation in the form:
/// 1:user:COMMAND_CLASS_SWITCH_MULTILEVEL:1:0:byte
/// to map it into a MQTT message.
///

void
ozw_publish_mqtt (int nodeID,
                  ValueID valueID,
                  const std::string& message)
{
  char internal_topic[256];

  snprintf (internal_topic, sizeof(internal_topic), "%d:%s:%s:%d:%d:%s", nodeID,
           genreToString[valueID.GetGenre ()].c_str (),
           commandClassToString[valueID.GetCommandClassId ()].c_str (),
           valueID.GetInstance (), valueID.GetIndex (),
           valueTypeToString[valueID.GetType ()].c_str ());

  INFO ("zwave/comms", "openzwave message : \"" + string (internal_topic) + "\" = \"" + message + "\".");

  value_cache[internal_topic] = message;
  openDHANA_mqtt__communication__publish (openDHANA_mqtt_publications,
                                          internal_topic,
                                          message);
}


/// Put a message on the OpenZWave stack.
///

void
openDHANA_ozw__zwave__put_message (const std::string& internal_topic,
                                   const std::string& message)
{
  // TODO: Handle system commands

  if (value_cache[internal_topic] == message
      && dhana_ozw_ignore_duplicate_messages)
    {
      INFO ("zwave/comms",
            "duplicate message \"" + internal_topic + "\" = \"" + message
            + "\", ignored.");
      return;
    }



  string_vector ozw =
          openDHANA__generic__reg_ex_match_groups (6,
                                                   "^(\\w*):(\\w*):(\\w*):(\\w*):(\\w*):(\\w*)$",
                                                   internal_topic);

  if (ozw[0] == "Manager")
    {
      if (ozw[1] == "HealNetwork")
        {
          bool returnRoutes = message == "true" ? true : false;
          Manager::Get ()->HealNetwork (homeID, returnRoutes);
        }
    }
  else
    {
      // "Normal" value
      INFO ("zwave/comms", "openzwave set: \"" + internal_topic + "\" = \"" + message + "\".");

      uint8 nodeId = atoi (ozw[0].c_str ());
      ValueID::ValueGenre genre = (ValueID::ValueGenre) stringToGenre[ozw[1]];
      uint8 commandClassId = stringToCommandClass[ozw[2]];
      uint8 instance = atoi (ozw[3].c_str ());
      uint8 index = atoi (ozw[4].c_str ());
      ValueID::ValueType valueType = (ValueID::ValueType) stringToValueType[ozw[5]];

      //      printf (">>>>> 0x%08x %d:%d:%d:%d:%d:%d=%d\n", homeID, nodeId, genre, commandClassId, instance, index, valueType, atoi (message.c_str ()));
      ValueID vid = ValueID (homeID, nodeId,
                             genre,
                             commandClassId,
                             instance,
                             index,
                             valueType);

      bool result = false;
      bool boolMessage = false;
      uint8 byteMessage = 0;
      string decimalMessage = "0.0";
      int32 intMessage = 0;
      string listMessage = "";
      int16 shortMessage = 0;
      string stringMessage = "";

      switch (valueType)
        {
          // Boolean message
        case OpenZWave::ValueID::ValueType_Bool:
          boolMessage = message == "true" ? true : false;
          result = Manager::Get ()->SetValue (vid, boolMessage);
          break;

          // Byte message
        case OpenZWave::ValueID::ValueType_Byte:
          byteMessage = atoi (message.c_str ());
          result = Manager::Get ()->SetValue (vid, byteMessage);
          break;

          // Decimal message (as a string)
        case OpenZWave::ValueID::ValueType_Decimal:
          decimalMessage = message;
          result = Manager::Get ()->SetValue (vid, decimalMessage);
          break;

          // Int message
        case OpenZWave::ValueID::ValueType_Int:
          intMessage = atoi (message.c_str ());
          result = Manager::Get ()->SetValue (vid, intMessage);
          break;

          // List message
        case OpenZWave::ValueID::ValueType_List:
          listMessage = message;
          result = Manager::Get ()->SetValueListSelection (vid, listMessage);
          break;

          // Schedule message
        case OpenZWave::ValueID::ValueType_Schedule:
          //				listMessage = message;
          //				result = Manager::Get() -> SetValue(vid, listMessage);
          break;

          // Short message
        case OpenZWave::ValueID::ValueType_Short:
          shortMessage = atoi (message.c_str ());
          result = Manager::Get ()->SetValue (vid, shortMessage);
          break;

          // String message
        case OpenZWave::ValueID::ValueType_String:
          stringMessage = message;
          result = Manager::Get ()->SetValue (vid, stringMessage);
          break;

          // Button message
        case OpenZWave::ValueID::ValueType_Button:
          //				stringMessage = message;
          //				result = Manager::Get() -> SetValue(vid, stringMessage);
          break;

          // Raw message
        case OpenZWave::ValueID::ValueType_Raw:
          //				stringMessage = message;
          //				result = Manager::Get() -> SetValue(vid, stringMessage, length);
          break;

        }

      if (result)
        value_cache[internal_topic] = message;
      else
        WARNING ("zwave/comms", "setting value failed.");
    }
}

/// Take all the messages from the queue and put them on the OpenZWave stack.
///

void
openDHANA_ozw__zwave__put_all_messages ()
{
  while (startup_queue.empty () == false)
    {
      raw_message msg = startup_queue.front ();

      openDHANA_ozw__zwave__put_message (msg.internal_topic, msg.message);

      startup_queue.pop ();
    }
}

void
OnNotification (Notification const* _notification, void* _context)
{
  char node_info[255];

  pthread_mutex_lock (&g_criticalSection);

  ValueID valueID = _notification->GetValueID ();
  int nodeId = _notification->GetNodeId ();
  //	std::string	genre		= genreToString[id.GetGenre()];
  //	std::string	commandClass	= commandClassToString[id.GetCommandClassId()];
  //	int		instance	= id.GetInstance();
  //	int		index		= id.GetIndex();
  //	std::string	valueType	= valueTypeToString[id.GetType()];
  //	char		buf[256];
  string message;

  snprintf (node_info, sizeof (node_info), " node: %d.", nodeId);

  switch (_notification->GetType ())
    {

    case Notification::Type_ValueAdded:
      OZW_INFO ("ozw/network", "value added." + string (node_info));
      Manager::Get ()->GetValueAsString (valueID, &message);
      ozw_publish_mqtt (nodeId, valueID, message);
      break;

    case Notification::Type_ValueRemoved:
      OZW_INFO ("ozw/network", "value removed." + string (node_info));
      break;

    case Notification::Type_ValueRefreshed:
      OZW_INFO ("ozw/network", "value refreshed." + string (node_info));
      Manager::Get ()->GetValueAsString (valueID, &message);
      ozw_publish_mqtt (nodeId, valueID, message);
      break;

    case Notification::Type_ValueChanged:
      OZW_INFO ("ozw/network", "value changed." + string (node_info));
      Manager::Get ()->GetValueAsString (valueID, &message);
      ozw_publish_mqtt (nodeId, valueID, message);
      break;

    case Notification::Type_Group:
      OZW_INFO ("ozw/network", "group." + string (node_info));
      break;

    case Notification::Type_NodeNew:
      OZW_INFO ("ozw/network", "node new." + string (node_info));
      break;

    case Notification::Type_NodeAdded:
      OZW_INFO ("ozw/network", "node added." + string (node_info));
      break;

    case Notification::Type_NodeRemoved:
      OZW_INFO ("ozw/network", "node removed." + string (node_info));
      break;

    case Notification::Type_NodeProtocolInfo:
      OZW_INFO ("ozw/network", "node protocol info." + string (node_info));
      break;

    case Notification::Type_NodeNaming:
      OZW_INFO ("ozw/network", "node naming." + string (node_info));
      break;

    case Notification::Type_NodeEvent:
      OZW_INFO ("ozw/network", "node event." + string (node_info));
      break;

    case Notification::Type_PollingDisabled:
      OZW_INFO ("ozw/network", "polling disabled." + string (node_info));
      break;

    case Notification::Type_PollingEnabled:
      OZW_INFO ("ozw/network", "polling enabled." + string (node_info));
      break;

    case Notification::Type_SceneEvent:
      OZW_INFO ("ozw/network", "scene event." + string (node_info));
      break;

    case Notification::Type_CreateButton:
      OZW_INFO ("ozw/network", "create button." + string (node_info));
      break;

    case Notification::Type_DeleteButton:
      OZW_INFO ("ozw/network", "delete button." + string (node_info));
      break;

    case Notification::Type_ButtonOn:
      OZW_INFO ("ozw/network", "button on." + string (node_info));
      break;

    case Notification::Type_ButtonOff:
      OZW_INFO ("ozw/network", "button off." + string (node_info));
      break;

    case Notification::Type_DriverReady:
      OZW_INFO ("ozw/network", "OpenZWave driver ready." + string (node_info));
      homeID = _notification->GetHomeId ();
      openDHANA_stack_state = state_driver_ready;
      if (openDHANA_message_gate == gate_none)
        {
          INFO ("zwave/comms",
                "driver ready and message_gate = none, releasing all queued messages.");
          openDHANA_ozw__zwave__put_all_messages ();
        }
      break;

    case Notification::Type_DriverFailed:
      WARNING ("ozw/network", "OpenZWave driver failed.");
      break;

    case Notification::Type_DriverReset:
      OZW_INFO ("ozw/network", "OpenZWave driver reset." + string (node_info));
      break;

    case Notification::Type_EssentialNodeQueriesComplete:
      OZW_INFO ("ozw/network", "essential nodes queries completed." + string (node_info));
      break;

    case Notification::Type_NodeQueriesComplete:
      OZW_INFO ("ozw/network", "nodes queries completed." + string (node_info));
      break;

    case Notification::Type_AwakeNodesQueried:
      OZW_INFO ("ozw/network", "awake nodes queried.");
      openDHANA_stack_state = state_awake_nodes;
      if (openDHANA_message_gate == gate_awake_nodes)
        {
          INFO ("zwave/comms",
                "awake nodes queried and message_gate = awake_nodes, releasing all queued messages.");
          openDHANA_ozw__zwave__put_all_messages ();
        }
      break;

    case Notification::Type_AllNodesQueriedSomeDead:
      OZW_INFO ("ozw/network", "all nodes queried, some dead.");
      openDHANA_stack_state = state_all_nodes;
      if (openDHANA_message_gate == gate_all_nodes)
        {
          INFO ("zwave/comms",
                "all nodes queried, some dead and message_gate = all_nodes, releasing all queued messages.");
          openDHANA_ozw__zwave__put_all_messages ();
        }
      break;

    case Notification::Type_AllNodesQueried:
      OZW_INFO ("ozw/network", "all nodes queried.");
      openDHANA_stack_state = state_all_nodes;
      if (openDHANA_message_gate == gate_all_nodes)
        {
          INFO ("zwave/comms",
                "all nodes queried and message_gate = all_nodes, releasing all queued messages.");
          openDHANA_ozw__zwave__put_all_messages ();
        }
      break;

    case Notification::Type_Notification:
      //			printf ("***** Type_Notification *****\n");
      // TODO: Update status
      break;

    case Notification::Type_DriverRemoved:
      OZW_INFO ("ozw/network", "OpenZWave driver removed.");
      break;
    }

  pthread_mutex_unlock (&g_criticalSection);
}

// 1:user:COMMAND_CLASS_SWITCH_MULTILEVEL:1:0:byte
// Manager:HealNetwork::::
// Manager:HealNetworkNode:1::::
// ^(\d*):(\w*):(\w*):(\d*):(\d*):(\w*)$

void
moduleMessageCallback (const std::string& internal_topic,
                       const std::string& message)
{

  if ((openDHANA_stack_state == state_driver_ready && openDHANA_message_gate == gate_none)
      || (openDHANA_stack_state == state_awake_nodes && openDHANA_message_gate == gate_awake_nodes)
      || (openDHANA_stack_state == state_all_nodes && openDHANA_message_gate == gate_all_nodes))
    {
      openDHANA_ozw__zwave__put_message (internal_topic, message);
    }
  else
    {
      // The OpenZWave stack is still polling nodes. Queue commands instead
      // of sending them to the stack.

      INFO ("zwave/comms",
            "the message gate is active, \"" + internal_topic + "\" = \"" + message
            + "\", queued.");

      raw_message msg;
      msg.internal_topic = internal_topic;
      msg.message = message;
      startup_queue.push (msg);
    }
}

void
dhana_ozw_setup ()
{
  setDoubleList (valueTypeToString, stringToValueType,
                 OpenZWave::ValueID::ValueType_Bool, "bool");
  setDoubleList (valueTypeToString, stringToValueType,
                 OpenZWave::ValueID::ValueType_Byte, "byte");
  setDoubleList (valueTypeToString, stringToValueType,
                 OpenZWave::ValueID::ValueType_Decimal, "decimal");
  setDoubleList (valueTypeToString, stringToValueType,
                 OpenZWave::ValueID::ValueType_Int, "int");
  setDoubleList (valueTypeToString, stringToValueType,
                 OpenZWave::ValueID::ValueType_List, "list");
  setDoubleList (valueTypeToString, stringToValueType,
                 OpenZWave::ValueID::ValueType_Schedule, "schedule");
  setDoubleList (valueTypeToString, stringToValueType,
                 OpenZWave::ValueID::ValueType_Short, "short");
  setDoubleList (valueTypeToString, stringToValueType,
                 OpenZWave::ValueID::ValueType_String, "string");
  setDoubleList (valueTypeToString, stringToValueType,
                 OpenZWave::ValueID::ValueType_Button, "button");
  setDoubleList (valueTypeToString, stringToValueType,
                 OpenZWave::ValueID::ValueType_Raw, "raw");

  setDoubleList (genreToString, stringToGenre,
                 OpenZWave::ValueID::ValueGenre_Basic, "basic");
  setDoubleList (genreToString, stringToGenre,
                 OpenZWave::ValueID::ValueGenre_User, "user");
  setDoubleList (genreToString, stringToGenre,
                 OpenZWave::ValueID::ValueGenre_Config, "config");
  setDoubleList (genreToString, stringToGenre,
                 OpenZWave::ValueID::ValueGenre_System, "system");

  // TODO: Get this from open-zwave include files?
  setDoubleList (commandClassToString, stringToCommandClass, 0x00,
                 "COMMAND_CLASS_NO_OPERATION");
  setDoubleList (commandClassToString, stringToCommandClass, 0x20,
                 "COMMAND_CLASS_BASIC");
  setDoubleList (commandClassToString, stringToCommandClass, 0x21,
                 "COMMAND_CLASS_CONTROLLER_REPLICATION");
  setDoubleList (commandClassToString, stringToCommandClass, 0x22,
                 "COMMAND_CLASS_APPLICATION_STATUS");
  setDoubleList (commandClassToString, stringToCommandClass, 0x23,
                 "COMMAND_CLASS_ZIP_SERVICES");
  setDoubleList (commandClassToString, stringToCommandClass, 0x24,
                 "COMMAND_CLASS_ZIP_SERVER");
  setDoubleList (commandClassToString, stringToCommandClass, 0x25,
                 "COMMAND_CLASS_SWITCH_BINARY");
  setDoubleList (commandClassToString, stringToCommandClass, 0x26,
                 "COMMAND_CLASS_SWITCH_MULTILEVEL");
  setDoubleList (commandClassToString, stringToCommandClass, 0x27,
                 "COMMAND_CLASS_SWITCH_ALL");
  setDoubleList (commandClassToString, stringToCommandClass, 0x28,
                 "COMMAND_CLASS_SWITCH_TOGGLE_BINARY");
  setDoubleList (commandClassToString, stringToCommandClass, 0x29,
                 "COMMAND_CLASS_SWITCH_TOGGLE_MULTILEVEL");
  setDoubleList (commandClassToString, stringToCommandClass, 0x2A,
                 "COMMAND_CLASS_CHIMNEY_FAN");
  setDoubleList (commandClassToString, stringToCommandClass, 0x2B,
                 "COMMAND_CLASS_SCENE_ACTIVATION");
  setDoubleList (commandClassToString, stringToCommandClass, 0x2C,
                 "COMMAND_CLASS_SCENE_ACTUATOR_CONF");
  setDoubleList (commandClassToString, stringToCommandClass, 0x2D,
                 "COMMAND_CLASS_SCENE_CONTROLLER_CONF");
  setDoubleList (commandClassToString, stringToCommandClass, 0x2E,
                 "COMMAND_CLASS_ZIP_CLIENT");
  setDoubleList (commandClassToString, stringToCommandClass, 0x2F,
                 "COMMAND_CLASS_ZIP_ADV_SERVICES");
  setDoubleList (commandClassToString, stringToCommandClass, 0x30,
                 "COMMAND_CLASS_SENSOR_BINARY");
  setDoubleList (commandClassToString, stringToCommandClass, 0x31,
                 "COMMAND_CLASS_SENSOR_MULTILEVEL");
  setDoubleList (commandClassToString, stringToCommandClass, 0x32,
                 "COMMAND_CLASS_METER");
  setDoubleList (commandClassToString, stringToCommandClass, 0x33,
                 "COMMAND_CLASS_ZIP_ADV_SERVER");
  setDoubleList (commandClassToString, stringToCommandClass, 0x34,
                 "COMMAND_CLASS_ZIP_ADV_CLIENT");
  setDoubleList (commandClassToString, stringToCommandClass, 0x35,
                 "COMMAND_CLASS_METER_PULSE");
  setDoubleList (commandClassToString, stringToCommandClass, 0x3C,
                 "COMMAND_CLASS_METER_TBL_CONFIG");
  setDoubleList (commandClassToString, stringToCommandClass, 0x3D,
                 "COMMAND_CLASS_METER_TBL_MONITOR");
  setDoubleList (commandClassToString, stringToCommandClass, 0x3E,
                 "COMMAND_CLASS_METER_TBL_PUSH");
  setDoubleList (commandClassToString, stringToCommandClass, 0x38,
                 "COMMAND_CLASS_THERMOSTAT_HEATING");
  setDoubleList (commandClassToString, stringToCommandClass, 0x40,
                 "COMMAND_CLASS_THERMOSTAT_MODE");
  setDoubleList (commandClassToString, stringToCommandClass, 0x42,
                 "COMMAND_CLASS_THERMOSTAT_OPERATING_STATE");
  setDoubleList (commandClassToString, stringToCommandClass, 0x43,
                 "COMMAND_CLASS_THERMOSTAT_SETPOINT");
  setDoubleList (commandClassToString, stringToCommandClass, 0x44,
                 "COMMAND_CLASS_THERMOSTAT_FAN_MODE");
  setDoubleList (commandClassToString, stringToCommandClass, 0x45,
                 "COMMAND_CLASS_THERMOSTAT_FAN_STATE");
  setDoubleList (commandClassToString, stringToCommandClass, 0x46,
                 "COMMAND_CLASS_CLIMATE_CONTROL_SCHEDULE");
  setDoubleList (commandClassToString, stringToCommandClass, 0x47,
                 "COMMAND_CLASS_THERMOSTAT_SETBACK");
  setDoubleList (commandClassToString, stringToCommandClass, 0x4C,
                 "COMMAND_CLASS_DOOR_LOCK_LOGGING");
  setDoubleList (commandClassToString, stringToCommandClass, 0x4E,
                 "COMMAND_CLASS_SCHEDULE_ENTRY_LOCK");
  setDoubleList (commandClassToString, stringToCommandClass, 0x50,
                 "COMMAND_CLASS_BASIC_WINDOW_COVERING");
  setDoubleList (commandClassToString, stringToCommandClass, 0x51,
                 "COMMAND_CLASS_MTP_WINDOW_COVERING");
  setDoubleList (commandClassToString, stringToCommandClass, 0x60,
                 "COMMAND_CLASS_MULTI_CHANNEL_V2");
  setDoubleList (commandClassToString, stringToCommandClass, 0x62,
                 "COMMAND_CLASS_DOOR_LOCK");
  setDoubleList (commandClassToString, stringToCommandClass, 0x63,
                 "COMMAND_CLASS_USER_CODE");
  setDoubleList (commandClassToString, stringToCommandClass, 0x66,
                 "COMMAND_CLASS_BARRIER_OPERATOR");
  setDoubleList (commandClassToString, stringToCommandClass, 0x70,
                 "COMMAND_CLASS_CONFIGURATION");
  setDoubleList (commandClassToString, stringToCommandClass, 0x71,
                 "COMMAND_CLASS_ALARM");
  setDoubleList (commandClassToString, stringToCommandClass, 0x72,
                 "COMMAND_CLASS_MANUFACTURER_SPECIFIC");
  setDoubleList (commandClassToString, stringToCommandClass, 0x73,
                 "COMMAND_CLASS_POWERLEVEL");
  setDoubleList (commandClassToString, stringToCommandClass, 0x75,
                 "COMMAND_CLASS_PROTECTION");
  setDoubleList (commandClassToString, stringToCommandClass, 0x76,
                 "COMMAND_CLASS_LOCK");
  setDoubleList (commandClassToString, stringToCommandClass, 0x77,
                 "COMMAND_CLASS_NODE_NAMING");
  setDoubleList (commandClassToString, stringToCommandClass, 0x7A,
                 "COMMAND_CLASS_FIRMWARE_UPDATE_MD");
  setDoubleList (commandClassToString, stringToCommandClass, 0x7B,
                 "COMMAND_CLASS_GROUPING_NAME");
  setDoubleList (commandClassToString, stringToCommandClass, 0x7C,
                 "COMMAND_CLASS_REMOTE_ASSOCIATION_ACTIVATE");
  setDoubleList (commandClassToString, stringToCommandClass, 0x7D,
                 "COMMAND_CLASS_REMOTE_ASSOCIATION");
  setDoubleList (commandClassToString, stringToCommandClass, 0x80,
                 "COMMAND_CLASS_BATTERY");
  setDoubleList (commandClassToString, stringToCommandClass, 0x81,
                 "COMMAND_CLASS_CLOCK");
  setDoubleList (commandClassToString, stringToCommandClass, 0x82,
                 "COMMAND_CLASS_HAIL");
  setDoubleList (commandClassToString, stringToCommandClass, 0x84,
                 "COMMAND_CLASS_WAKE_UP");
  setDoubleList (commandClassToString, stringToCommandClass, 0x85,
                 "COMMAND_CLASS_ASSOCIATION");
  setDoubleList (commandClassToString, stringToCommandClass, 0x86,
                 "COMMAND_CLASS_VERSION");
  setDoubleList (commandClassToString, stringToCommandClass, 0x87,
                 "COMMAND_CLASS_INDICATOR");
  setDoubleList (commandClassToString, stringToCommandClass, 0x88,
                 "COMMAND_CLASS_PROPRIETARY");
  setDoubleList (commandClassToString, stringToCommandClass, 0x89,
                 "COMMAND_CLASS_LANGUAGE");
  setDoubleList (commandClassToString, stringToCommandClass, 0x8A,
                 "COMMAND_CLASS_TIME");
  setDoubleList (commandClassToString, stringToCommandClass, 0x8B,
                 "COMMAND_CLASS_TIME_PARAMETERS");
  setDoubleList (commandClassToString, stringToCommandClass, 0x8C,
                 "COMMAND_CLASS_GEOGRAPHIC_LOCATION");
  setDoubleList (commandClassToString, stringToCommandClass, 0x8D,
                 "COMMAND_CLASS_COMPOSITE");
  setDoubleList (commandClassToString, stringToCommandClass, 0x8E,
                 "COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V2");
  setDoubleList (commandClassToString, stringToCommandClass, 0x8F,
                 "COMMAND_CLASS_MULTI_CMD");
  setDoubleList (commandClassToString, stringToCommandClass, 0x90,
                 "COMMAND_CLASS_ENERGY_PRODUCTION");
  setDoubleList (commandClassToString, stringToCommandClass, 0x91,
                 "COMMAND_CLASS_MANUFACTURER_PROPRIETARY");
  setDoubleList (commandClassToString, stringToCommandClass, 0x92,
                 "COMMAND_CLASS_SCREEN_MD");
  setDoubleList (commandClassToString, stringToCommandClass, 0x93,
                 "COMMAND_CLASS_SCREEN_ATTRIBUTES");
  setDoubleList (commandClassToString, stringToCommandClass, 0x94,
                 "COMMAND_CLASS_SIMPLE_AV_CONTROL");
  setDoubleList (commandClassToString, stringToCommandClass, 0x95,
                 "COMMAND_CLASS_AV_CONTENT_DIRECTORY_MD");
  setDoubleList (commandClassToString, stringToCommandClass, 0x96,
                 "COMMAND_CLASS_AV_RENDERER_STATUS");
  setDoubleList (commandClassToString, stringToCommandClass, 0x97,
                 "COMMAND_CLASS_AV_CONTENT_SEARCH_MD");
  setDoubleList (commandClassToString, stringToCommandClass, 0x98,
                 "COMMAND_CLASS_SECURITY");
  setDoubleList (commandClassToString, stringToCommandClass, 0x99,
                 "COMMAND_CLASS_AV_TAGGING_MD");
  setDoubleList (commandClassToString, stringToCommandClass, 0x9A,
                 "COMMAND_CLASS_IP_CONFIGURATION");
  setDoubleList (commandClassToString, stringToCommandClass, 0x9B,
                 "COMMAND_CLASS_ASSOCIATION_COMMAND_CONFIGURATION");
  setDoubleList (commandClassToString, stringToCommandClass, 0x9C,
                 "COMMAND_CLASS_SENSOR_ALARM");
  setDoubleList (commandClassToString, stringToCommandClass, 0x9D,
                 "COMMAND_CLASS_SILENCE_ALARM");
  setDoubleList (commandClassToString, stringToCommandClass, 0x9E,
                 "COMMAND_CLASS_SENSOR_CONFIGURATION");
  setDoubleList (commandClassToString, stringToCommandClass, 0xEF,
                 "COMMAND_CLASS_MARK");
  setDoubleList (commandClassToString, stringToCommandClass, 0xF0,
                 "COMMAND_CLASS_NON_INTEROPERABLE");
}

void
openDHANA_ozw__options__set_ozw ()
{

  openDHANA_option_store["ozw_port"] =
          Option (OptionOptional, "/dev/ttyUSB0",
                  "^\\s*(ozw_port)\\s*=\\s*\"(.*)\"\\s*$");
  openDHANA_option_store["ozw_port_type"] =
          Option (OptionOptional, "",
                  "^\\s*(ozw_port_type)\\s*=\\s*\"(.*)\"\\s*$");
  openDHANA_option_store["ozw_debug"] =
          Option (OptionOptional, "false",
                  "^\\s*(ozw_debug)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_options_file"] =
          Option (OptionOptional, OPTIONS_FILE_OZW,
                  "^\\s*(ozw_options_file)\\s*=\\s*\"(.*)\"\\s*$");
  openDHANA_option_store["ozw_ignore_duplicate_messages"] =
          Option (OptionOptional, "true",
                  "^\\s*(ozw_ignore_duplicate_messages)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_message_gate"] =
          Option (OptionOptional, "awake_nodes",
                  "^\\s*(ozw_message_gate)\\s*=\\s*(none|awake_nodes|all_nodes)\\s*$");

  // openZWave options
  openDHANA_option_store["ozw_user_path"] =
          Option (OptionOptional, OZW_USER_PATH,
                  "^\\s*(ozw_user_path)\\s*=\\s*\"(.*)\"\\s*$");
  openDHANA_option_store["ozw_config_path"] =
          Option (OptionOptional, OZW_CONFIG_PATH,
                  "^\\s*(ozw_config_path)\\s*=\\s*\"(.*)\"\\s*$");
  openDHANA_option_store["ozw_logging"] =
          Option (OptionOptional, "true",
                  "^\\s*(ozw_logging)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_console_output"] =
          Option (OptionOptional, "true",
                  "^\\s*(ozw_console_output)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_log_file_name"] =
          Option (OptionOptional, "OZW_Log.txt",
                  "^\\s*(ozw_log_file_name)\\s*=\\s*\"(.*)\"\\s*$");
  openDHANA_option_store["ozw_append_log_file"] =
          Option (OptionOptional, "false",
                  "^\\s*(ozw_append_log_file)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_save_log_level"] =
          Option (OptionOptional, "7",
                  "^\\s*(ozw_save_log_level)\\s*=\\s*([0-9])\\s*$");
  openDHANA_option_store["ozw_queue_log_level"] =
          Option (OptionOptional, "8",
                  "^\\s*(ozw_queue_log_level)\\s*=\\s*([0-9])\\s*$");
  openDHANA_option_store["ozw_dump_trigger_level"] =
          Option (OptionOptional, "0",
                  "^\\s*(ozw_dump_trigger_level)\\s*=\\s*([0-9])\\s*$");
  openDHANA_option_store["ozw_associate"] =
          Option (OptionOptional, "true",
                  "^\\s*(ozw_associate)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_notify_transactions"] =
          Option (OptionOptional, "false",
                  "^\\s*(ozw_notify_transactions)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_driver_max_attempts"] =
          Option (OptionOptional, "0",
                  "^\\s*(ozw_driver_max_attempts)\\s*=\\s*([0-9]+)\\s*$");
  openDHANA_option_store["ozw_save_configuration"] =
          Option (OptionOptional, "true",
                  "^\\s*(ozw_save_configuration)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_poll_interval"] =
          Option (OptionOptional, "30000", // Every 10 minutes
                  "^\\s*(ozw_poll_interval)\\s*=\\s*([0-9]+)\\s*$");
  openDHANA_option_store["ozw_interval_between_polls"] =
          Option (OptionOptional, "false",
                  "^\\s*(ozw_interval_between_polls)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_perform_return_routes"] =
          Option (OptionOptional, "true",
                  "^\\s*(ozw_perform_return_routes)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_include"] =
          Option (OptionOptional, "",
                  "^\\s*(ozw_include)\\s*=\\s*\"(.*)\"\\s*$");
  openDHANA_option_store["ozw_exclude"] =
          Option (OptionOptional, "",
                  "^\\s*(ozw_exclude)\\s*=\\s*\"(.*)\"\\s*$");
  openDHANA_option_store["ozw_suppress_value_refresh"] =
          Option (OptionOptional, "false",
                  "^\\s*(ozw_suppress_value_refresh)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_retry_timeout"] =
          Option (OptionOptional, "40000",
                  "^\\s*(ozw_retry_timeout)\\s*=\\s*([0-9]+)\\s*$");
  openDHANA_option_store["ozw_network_key"] =
          Option (OptionOptional, "",
                  "^\\s*(ozw_network_key)\\s*=\\s*\"(.*)\"\\s*$");
  openDHANA_option_store["ozw_enable_sis"] =
          Option (OptionOptional, "true",
                  "^\\s*(ozw_enable_sis)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_assume_awake"] =
          Option (OptionOptional, "true",
                  "^\\s*(ozw_assume_awake)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_refresh_all_user_codes"] =
          Option (OptionOptional, "false",
                  "^\\s*(ozw_refresh_all_user_codes)\\s*=\\s*(true|false)\\s*$");
  openDHANA_option_store["ozw_validate_value_changes"] =
          Option (OptionOptional, "true",
                  "^\\s*(ozw_validate_value_changes)\\s*=\\s*(true|false)\\s*$");
}

///

void
openDHANA_ozw__options__process ()
{

  openDHANA_ozw_debug = OPTION (ozw_debug) == "true" ? true : false;
  dhana_ozw_ignore_duplicate_messages =
          OPTION (ozw_ignore_duplicate_messages) == "true" ? true : false;

  if (OPTION (ozw_message_gate) == "none") openDHANA_message_gate = gate_none;
  if (OPTION (ozw_message_gate) == "awake_nodes") openDHANA_message_gate = gate_awake_nodes;
  if (OPTION (ozw_message_gate) == "all_nodes") openDHANA_message_gate = gate_all_nodes;


  // OpenZWave stack options
  Options::Create (OPTION (ozw_config_path),
                   OPTION (ozw_user_path),
                   "");

  if (OPTION (ozw_user_path) != "")
    Options::Get ()->AddOptionString ("UserPath",
                                      OPTION (ozw_user_path), false);
  if (OPTION (ozw_config_path) != "")
    Options::Get ()->AddOptionString ("ConfigPath",
                                      OPTION (ozw_config_path), false);
  Options::Get ()->AddOptionBool ("Logging",
                                  OPTION (ozw_logging) == "true" ? true : false);
  Options::Get ()->AddOptionBool ("ConsoleOutput",
                                  OPTION (ozw_console_output) == "true" ? true : false);
  if (OPTION (ozw_log_file_name) != "")
    Options::Get ()->AddOptionString ("LogFileName",
                                      OPTION (ozw_log_file_name), false);
  Options::Get ()->AddOptionBool ("AppendLogFile",
                                  OPTION (ozw_append_log_file) == "true" ? true : false);
  Options::Get ()->AddOptionInt ("SaveLogLevel",
                                 atoi (OPTION (ozw_save_log_level).c_str ()));
  Options::Get ()->AddOptionInt ("QueueLogLevel",
                                 atoi (OPTION (ozw_queue_log_level).c_str ()));
  Options::Get ()->AddOptionInt ("DumpTriggerLevel",
                                 atoi (OPTION (ozw_dump_trigger_level).c_str ()));
  Options::Get ()->AddOptionBool ("Associate",
                                  OPTION (ozw_associate) == "true" ? true : false);
  Options::Get ()->AddOptionBool ("NotifyTransactions",
                                  OPTION (ozw_notify_transactions) == "true" ? true : false);
  Options::Get ()->AddOptionInt ("DriverMaxAttempts",
                                 atoi (OPTION (ozw_driver_max_attempts).c_str ()));
  Options::Get ()->AddOptionBool ("SaveConfiguration",
                                  OPTION (ozw_save_configuration) == "true" ? true : false);
  Options::Get ()->AddOptionInt ("PollInterval",
                                 atoi (OPTION (ozw_poll_interval).c_str ()));
  Options::Get ()->AddOptionBool ("IntervalBetweenPolls",
                                  OPTION (ozw_interval_between_polls) == "true" ? true : false);
  Options::Get ()->AddOptionBool ("PerformReturnRoutes",
                                  OPTION (ozw_perform_return_routes) == "true" ? true : false);
  if (OPTION (ozw_log_file_name) != "")
    Options::Get ()->AddOptionString ("Include",
                                      OPTION (ozw_include), false);
  if (OPTION (ozw_log_file_name) != "")
    Options::Get ()->AddOptionString ("Exclude",
                                      OPTION (ozw_exclude), false);
  Options::Get ()->AddOptionBool ("SuppressValueRefresh",
                                  OPTION (ozw_suppress_value_refresh) == "true" ? true : false);
  Options::Get ()->AddOptionInt ("RetryTimeout",
                                 atoi (OPTION (ozw_retry_timeout).c_str ()));
  if (OPTION (ozw_network_key) != "")
    Options::Get ()->AddOptionString ("NetworkKey",
                                      OPTION (ozw_network_key), false);
  Options::Get ()->AddOptionBool ("EnableSIS",
                                  OPTION (ozw_enable_sis) == "true" ? true : false);
  Options::Get ()->AddOptionBool ("AssumeAwake",
                                  OPTION (ozw_assume_awake) == "true" ? true : false);
  Options::Get ()->AddOptionBool ("RefreshAllUserCodes",
                                  OPTION (ozw_refresh_all_user_codes) == "true" ? true : false);
  Options::Get ()->AddOptionBool ("ValidateValueChanges",
                                  OPTION (ozw_validate_value_changes) == "true" ? true : false);
  Options::Get ()->Lock ();
}

//=============================================================================
// OZW -Open Z-Wave
//=============================================================================

int
main (int argc, char* argv[])
{
  dhana_ozw_setup ();

  // Add ozw options
  openDHANA_ozw__options__set_ozw ();

  // Process options
  openDHANA__options__get (OPTION_DEFAULT (ozw_options_file),
                           argc,
                           argv);

  // Set internal values based on options
  openDHANA_ozw__options__process ();

  // Setup the process and daemonize if required
  openDHANA__generic__init_process ();
  STARTING ("openDHANA-ozw with OpenZWave version \""
            + Manager::getVersionAsString () + "\".");

  // Connect to the MQTT broker
  openDHANA_mqtt__communication__connect_broker ();

  pthread_mutexattr_t mutexattr;

  pthread_mutexattr_init (&mutexattr);
  pthread_mutexattr_settype (&mutexattr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init (&g_criticalSection, &mutexattr);
  pthread_mutexattr_destroy (&mutexattr);

  pthread_mutex_lock (&initMutex);

  Manager::Create ();

  Manager::Get ()->AddWatcher (OnNotification, NULL);

  std::string port = OPTION (ozw_port);

  Manager::Get ()->AddDriver (port);

  // Let the threads do their work and wait for exit signal
  openDHANA__generic__process_loop ();


  //	pthread_cond_wait( &initCond, &initMutex );

  Manager::Get ()->RemoveDriver (port);
  Manager::Get ()->RemoveWatcher (OnNotification, NULL);
  Manager::Destroy ();
  Options::Destroy ();

  pthread_mutex_destroy (&g_criticalSection);

  // Disconnect the broker(s)
  openDHANA_mqtt__communication__disconnect_broker ();

  STOPPING ("openDHANA-ozw");

  openDHANA__generic__kill_process ();
}


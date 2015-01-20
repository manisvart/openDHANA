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

// Name standard for function:
// module_name__group_of_functions__fuction
//
// openDHANA_ir__lua_function__log_message()


//#define OPEN_DHANA_CUSTOM_LUA_C_FUNCTIONS
#ifdef  OPEN_DHANA_CUSTOM_LUA_C_FUNCTIONS
extern void openDHANA__lua__custom_scripts ();
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <map>
#include <string>
#include <vector>
#include <time.h>
#include <libgen.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <string.h>
#include <queue>

#include <sys/inotify.h>
#include <sys/time.h>
#include <dirent.h>

#include <string.h>
#include <regex.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <mosquitto.h>

#include "openssl/md5.h"

extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

typedef std::map<std::string, std::string> string_map;
typedef std::vector<std::string> string_vector;
typedef std::string string;

#define CREATE_LOCK(var)    pthread_mutex_t var##_mutex
#define LOCK(var)   pthread_mutex_lock (&var##_mutex)
#define UNLOCK(var) pthread_mutex_unlock (&var##_mutex)

#define OPTION(option)  openDHANA__option__get_value(#option)
#define OPTION_DEFAULT(option)  openDHANA__option__get_default_value(#option)

#define ERROR(facility, message)    openDHANA__generic__log_error (facility, message, __func__, __LINE__)
#define WARNING(facility, message)      openDHANA__generic__log_warning (facility, message, __func__, __LINE__)
#define INFO(facility, message)         if (dhana_mqtt_debug) openDHANA__generic__log_info (facility, message, __func__, __LINE__)

#define STARTING(module) openDHANA__generic__log_info ("main/exec", "starting \"" + string(module) + "\" Version " + VERSION + ".", __func__, __LINE__)
#define STOPPING(module) openDHANA__generic__log_info ("main/exec", "stopping \"" + string(module) + "\".", __func__, __LINE__)


class mqtt_pub
{
public:
  string mqtt_topic;
  bool retain;
  int qos;
};

class mqtt_sub
{
public:
  string internal_topic;
  int qos;
};

#define OptionRequired true
#define OptionOptional false

class Option
{
public:
  void
  setValue (const std::string& value);
  std::string
  getRegEx (void);
  bool
  isRequired (void);
  std::string
  getDefaultValue (void);
  std::string
  getValue (void);
  Option (void);
  Option (const bool required, const std::string& default_value,
          const std::string& reg_ex);
private:
  string _value;
  string _reg_ex;
  string _default_value;
  bool _required;
};

extern std::map<std::string, mqtt_pub> openDHANA_mqtt_publications;
extern std::map<std::string, mqtt_sub> openDHANA_mqtt_subscriptions;
extern std::map<std::string, Option> openDHANA_option_store;
extern bool dhana_mqtt_debug;
extern bool dhana_mqtt_exiting;

//=============================================================================
// openDHANA__generic__
//=============================================================================

extern void
openDHANA__generic__log_error (const std::string& facility,
                               const std::string& error_message,
                               const std::string& function,
                               const int line_number);

extern void
openDHANA__generic__log_warning (const std::string& facility,
                                 const std::string& warning_message,
                                 const std::string& function,
                                 const int line_number);

extern void
openDHANA__generic__log_info (const std::string& facility,
                              const std::string& info_message,
                              const std::string& function,
                              const int line_number);

extern void
openDHANA__generic__log_starting (const std::string& module);

void
openDHANA__generic__log_stopping (const std::string& module);

extern bool
openDHANA__generic__reg_ex_match (const std::string& regex,
                                  const std::string& data);

extern string_vector
openDHANA__generic__reg_ex_match_groups (const int max_groups,
                                         const std::string& regex,
                                         const std::string& data);

extern string_vector
openDHANA__generic__reg_ex_match_groups_loop (const std::string& regex,
                                              const std::string& data);

extern std::string
openDHANA__generic__md5_hash (const std::string& string);

extern std::string
openDHANA___generic__md5_hash_unique ();

extern void
openDHANA__generic__process_loop ();

extern void
openDHANA__generic__signal_handler (int signal);

extern FILE*
openDHANA__generic__open_file (const std::string& path);

extern void
openDHANA__generic__open_log_file (const std::string& path);

extern void
openDHANA__generic__init_process ();

extern void
openDHANA__generic__kill_process ();

//=============================================================================
// openDHANA__config__
//=============================================================================

extern struct stat
openDHANA__config__file_monitor_scan_directory (const std::string& path);

extern void*
openDHANA__config__file_monitor_thread (void *param);

extern void
openDHANA__config__file_monitor_start ();

extern bool
openDHANA__config__file_monitor (const std::string& file_or_dir,
                                 void (*callback) (std::string));

extern bool
openDHANA__config__process_this_line (const std::string& line);

extern string_vector
openDHANA__config__split_line (const std::string& line);

extern string_vector
openDHANA__config__split_parameters (const std::string& all_options);

//=============================================================================
// openDHANA__options__
//=============================================================================

extern string
openDHANA__option__get_value (const string option);

extern string
openDHANA__option__get_default_value (const string option);


extern void
openDHANA__options__invalid_missing_options (const string file,
                                             int line_number);

extern void
openDHANA__options__uknown_definition (const string definition);

extern string_vector
openDHANA__options__read_file (const std::string& path);

extern bool
openDHANA__options__check (const string_vector& given_options,
                           std::map<std::string, Option>& defined_options);

extern void
openDHANA__options__set_generic ();

extern void
openDHANA__options__process ();

extern void
openDHANA__options__get (const std::string& option_file,
                         int argc,
                         char* argv[]);

//=============================================================================
// openDHANA_mqtt__
//=============================================================================

extern void
openDHANA_mqtt__config_files__read_mqttmap (const std::string& path);

extern void
openDHANA_mqtt__config_files__config_changed (const string path);

extern void
openDHANA_mqtt__config_files__monitor_start ();

extern void
openDHANA_mqtt__config_files__monitor ();

extern void
openDHANA_mqtt__communication__subscribe (const std::map<std::string,
                                          mqtt_sub>& mqtt_subscriptions);
extern void
openDHANA_mqtt__communication__unsubscribe (const std::map<std::string, mqtt_sub>& mqtt_subscriptions);

extern void
openDHANA_mqtt__communication__publish (std::map<std::string, mqtt_pub>& mqtt_publications,
                                        const std::string& internal_topic,
                                        const std::string& value);

extern void
openDHANA_mqtt__communication__connect_callback (struct mosquitto *mosq,
                                                 void *userdata,
                                                 int result);

extern void
openDHANA_mqtt__communication__subscribe_callback (struct mosquitto *mosq,
                                                   void *userdata,
                                                   int mid,
                                                   int qos_count,
                                                   const int *granted_qos);

extern void
openDHANA_mqtt__communication__message_callback (struct mosquitto *mosq,
                                                 void *userdata,
                                                 const struct mosquitto_message *message);

extern bool
openDHANA_mqtt__communication__connect_broker ();

extern void
openDHANA_mqtt__communication__disconnect_broker ();

//=============================================================================
// openDHANA__lua__
//=============================================================================

typedef int (*lua_callback)(lua_State *);
extern std::map <std::string, lua_callback> openDHANA__lua__lua_functions;
extern string_map openDHANA__lua__value_cache;

extern void
openDHANA__lua__call_function_in_all_scripts (const std::string& function,
                                              const std::string& message);

extern bool
openDHANA__lua__start_script (const string& path);

extern bool
openDHANA__lua__stop_script (string path);

extern bool
openDHANA__lua__stop_all_scripts ();

extern bool
openDHANA__lua__lua_dir_read (const string& path);

extern void
openDHANA__lua__lua_dir_changed (const string path);

extern void
openDHANA__lua__add_external_function (const string& function,
                                       lua_callback callback);

extern void
openDHANA__lua__add_external_functions ();

extern int
openDHANA__lua_function__log_message (lua_State *L);

extern int
openDHANA__lua_function__publish (lua_State *L);





extern void
moduleMessageCallback (const std::string& internal_topic,
                       const std::string& message);




extern void
setDoubleList (std::map<int, std::string> &forward,
               std::map<std::string, int> &reverse,
               int key,
               string value);

extern void
setDoubleList (string_map &forward,
               string_map &reverse,
               string key,
               string value);


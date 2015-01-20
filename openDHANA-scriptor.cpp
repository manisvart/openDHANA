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


#define OPTIONS_FILE_SCRIPTOR   "/etc/openDHANA/scriptor/openDHANA-scriptor.options"


/// We get a message from the MQTT broker
///
/// @param internal_topic       the internal topic that the MQTT topic
///                             translated into.
/// @param message              the MQTT message
///

void
moduleMessageCallback (const std::string& internal_topic,
                       const std::string& message)
{
  // Cache the message
  openDHANA__lua__value_cache[internal_topic] = message;

  INFO ("mqtt/comms",
        "internal_topic: \"" + internal_topic + "\" = \"" + message + "\".");

  // Find out which scripts have a callback for the internal_topic
  openDHANA__lua__call_function_in_all_scripts (internal_topic, message);
}


/// Set the unique scriptor options
///

void
openDHANA_scriptor__options__set_scriptor ()
{
  openDHANA_option_store["scriptor_options_file"] =
          Option (OptionOptional, OPTIONS_FILE_SCRIPTOR,
                  "^\\s*(scriptor_options_file)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["scriptor_lua_directory"] =
          Option (OptionOptional, "lua",
                  "^\\s*(scriptor_lua_directory)\\s*=\\s*\"(.*)\"\\s*$");
}

/// Process scriptors options
///

void
openDHANA_scriptor__options__process () {
  // Nothing to do here, move along!
}

/// Setup scriptor specific file monitors
///

void
openDHANA_scriptor__config_files__monitor ()
{
}


//=============================================================================
// Scriptor
//=============================================================================

int
main (int argc, char *argv[])
{
  // Add scriptors options
  openDHANA_scriptor__options__set_scriptor ();

  // Process options
  openDHANA__options__get (OPTION_DEFAULT (scriptor_options_file),
                           argc,
                           argv);

  // Set internal values based on options
  openDHANA_scriptor__options__process ();

  // Setup the process and daemonize if required
  openDHANA__generic__init_process ();
  STARTING ("openDHANA-scriptor  (Lua release " + string (LUA_RELEASE) + "\")");

  // Setup additional file monitors
  openDHANA_scriptor__config_files__monitor ();

  // Connect to the MQTT broker
  openDHANA_mqtt__communication__connect_broker ();

  // Get the current Lua scripts, and start them
  openDHANA__lua__add_external_functions ();
  openDHANA__lua__lua_dir_read (OPTION (scriptor_lua_directory));

  // And monitor it for changes
  openDHANA__config__file_monitor (OPTION (scriptor_lua_directory),
                                   &openDHANA__lua__lua_dir_changed);

  // Let the threads do their work and wait for exit signal
  openDHANA__generic__process_loop ();

  // Disconnect the broker
  openDHANA_mqtt__communication__disconnect_broker ();

  // Stop all scripts
  openDHANA__lua__stop_all_scripts ();

  STOPPING ("openDHANA-scriptor");

  openDHANA__generic__kill_process ();
}

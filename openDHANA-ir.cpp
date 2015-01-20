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

#define OPEN_DHANA_CUSTOM_LUA_C_FUNCTIONS

#include "openDHANA-mqtt.h"

#define OPTIONS_FILE_IR     "/etc/openDHANA/ir/openDHANA-ir.options"

//device name="Yamaha receiver" itach="upstairs" ir_port="1:1" equipment="Yamaha Receiver/Preamp RXV1065/765/665/565/465 Main Zone"

class ir_device
{
public:
  string itach;
  string ir_port;
  string equipment;
};

class ir_sender
{
public:
  string address;
  string port;
  string type;
  int socket;
};

class ir_command
{
public:
  string ir_type;
  string ir_command;
};

std::map<string, ir_device> device_store;
CREATE_LOCK (device_store);

std::map<string, ir_sender> ir_sender_store;
CREATE_LOCK (ir_sender_store);

// TODO: We probably need a more advanced data structure here, later... 
std::map<string, ir_command> ircommand_store;
CREATE_LOCK (ircommand_store);


//=============================================================================
// openDHANA_ir__comms__
//=============================================================================
#ifndef openDHANA_ir__comms

/// Convert an ir command in hex  to sendir format
///
/// @param hex                  the command in hex format
/// @return                     The command in senir format
//7

string
openDHANA_ir__comms__hex_to_sendir (const string& hex)
{
  // Get the groups
  string_vector groups =
          openDHANA__generic__reg_ex_match_groups_loop ("\\s*([0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F])\\s*",
                                                        hex);

  char buf[10];
  string sendir;
  int group_count = 0;
  for (string_vector::const_iterator group = groups.begin ();
          group != groups.end ();)
    {
      if (group_count++ >= 4)
        {
          long int l = strtol ((*group).c_str (), NULL, 16);
          snprintf (buf, sizeof(buf), "%ld", l);

          sendir += buf;
          if (++group != groups.end ())
            sendir += ",";
        }
      else
        ++group;
    }

  return "1," + sendir;
}


/// get sockaddr, IPv4 or IPv6:
///

void *
openDHANA_ir__comms__get_in_addr (struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET)
    {
      return &(((struct sockaddr_in*) sa)->sin_addr);
    }

  return &(((struct sockaddr_in6*) sa)->sin6_addr);
}

/// Send data to socket. 
///
/// @param sockfd               socket file descriptor.
/// @param message              the message to send.
///

int
openDHANA_ir__comms__socket_send (int sockfd, const string& message)
{
  //TODO: handle remaining data
  int len, bytes_sent;

  len = strlen (message.c_str ());
  bytes_sent = send (sockfd, message.c_str (), len, 0);

  return bytes_sent;
}

/// Connect to socket
///
/// @param host                 the host.
/// @param port                 the port.
///

int
openDHANA_ir__comms__open_connection (const string& host,
                                      const string& port)
{
  int rv;
  int sockfd = 0;
  struct addrinfo hints;
  struct addrinfo *servinfo;
  struct addrinfo *p;

  memset (&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC; // AF_INET or AF_INET6 to force version
  hints.ai_socktype = SOCK_STREAM;

  INFO ("comms/connection", "connecting \"" + host + ":" + port + "\".");

  if ((rv = getaddrinfo (host.c_str (), port.c_str (), &hints, &servinfo)) != 0)
    {
      ERROR ("comms/connection",
             "getaddrinfo for \"" + host + ":" + port + "\" " + gai_strerror (rv));
      return -1;
    }

  // loop through all the results and connect to the first we can
  for (p = servinfo; p != NULL; p = p->ai_next)
    {
      if ((sockfd = socket (p->ai_family, p->ai_socktype, p->ai_protocol))
          == -1)
        {
          //	  perror ("client: socket");
          continue;
        }

      if (connect (sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
          close (sockfd);
          //	  perror ("client: connect");
          continue;
        }

      break;
    }

  freeaddrinfo (servinfo); // all done with this structure

  return sockfd;
}

/// Disconnect from socket
///
/// @param sockfd               the socket file descriptor.
///

int
openDHANA_ir__comms__close_connection (const int sockfd)
{
  close (sockfd);
  shutdown (sockfd, 0);
  return 0;
}

/// Send a command and get the response
///
/// @param sockfd               the socket file descriptor.
/// @param command              the command to send.
/// @return                     The response.
///

string
openDHANA_ir__comms__send_and_get_response (int sockfd,
                                            const string command)
{
#  define MAXDATASIZE 100 // max number of bytes we can get at once
  int nbytes;
  struct timeval tv;
  char buf[MAXDATASIZE];

  fd_set read_dfs;
  FD_ZERO (&read_dfs);
  FD_SET (sockfd, &read_dfs);

  tv.tv_sec = 1;
  tv.tv_usec = 0;

  int nready;

  bool command_sent = false;
  //  int x = 0;
  while (true)
    {
      FD_ZERO (&read_dfs);
      FD_SET (sockfd, &read_dfs);

      // Send command, if not already sent
      if (!command_sent)
        {
          openDHANA_ir__comms__socket_send (sockfd, command);
          command_sent = true;
        }

      // Wait for data
      nready = select (sockfd + 1, &read_dfs, NULL, NULL, &tv);
      if (nready < 0)
        {
          ERROR ("comms/connection", "\"select\" error.");
          return "*** error ***";
        }
      else if (nready == 0)
        {
          // Nothing, reset timer
          tv.tv_sec = 1;
          tv.tv_usec = 0;
          //	  printf ("res timer\n");
        }
      else if (sockfd != 0 && FD_ISSET (sockfd, &read_dfs))
        {
          memset (&buf, 0, sizeof buf);
          nbytes = recv (sockfd, buf, sizeof buf, 0);
          if (nbytes < 0)
            {
              ERROR ("comms/connection", "error reading from socket.");
              return "*** error ***";
            }
          if (nbytes == 0)
            {
              ERROR ("comms/connection", "connection closed by server.");
              return "*** error ***";
            }
          return buf;
        }
    }
}

/// Connect to the IR senders
///

bool
openDHANA_ir__comms__connect_senders ()
{
  LOCK (ir_sender_store);
  // TODO: some sort of reconnect function
  INFO ("comms/connection", "connecting all ir_senders.");

  for (std::map<string, ir_sender>::iterator ir_sender =
          ir_sender_store.begin (); ir_sender != ir_sender_store.end ();
          ir_sender++)
    {
      int socket =
              openDHANA_ir__comms__open_connection (ir_sender->second.address,
                                                    ir_sender->second.port);

      if (socket > 0)
        ir_sender->second.socket = socket;
      else
        ir_sender->second.socket = 0;
    }
  INFO ("comms/connection", "all ir_senders connected.");
  UNLOCK (ir_sender_store);
  return true;
}

/// Disconnect from the IR senders
///

bool
openDHANA_ir__comms__disconnect_senders ()
{
  LOCK (ir_sender_store);
  INFO ("comms/connection", "disconnecting all ir_senders.");

  for (std::map<string, ir_sender>::iterator ir_sender =
          ir_sender_store.begin (); ir_sender != ir_sender_store.end ();
          ir_sender++)
    {
      int socket = ir_sender->second.socket;

      if (socket != 0)
        openDHANA_ir__comms__close_connection (socket);
    }

  INFO ("comms/connection", "all ir_senders disconnected.");
  UNLOCK (ir_sender_store);

  return true;
}

/// Query the IR senders for version and configuration information.
///

bool
openDHANA_ir__comms__query_senders ()
{
  LOCK (ir_sender_store);
  INFO ("comms/connection", "querying all ir_senders.");

  for (std::map<string, ir_sender>::iterator ir_sender =
          ir_sender_store.begin (); ir_sender != ir_sender_store.end ();
          ir_sender++)
    {
      int socket = ir_sender->second.socket;

      if (ir_sender->second.type == "itach")
        {
          string response =
                  openDHANA_ir__comms__send_and_get_response (socket,
                                                              "getversion\r");
          response.erase (response.size () - 1); // Remove '\r'

          INFO ("comms/connection",
                "itach \"" + ir_sender->second.address + ":"
                + ir_sender->second.port + "\" version \"" + response + "\".");
        }
    }

  INFO ("comms/connection", "all ir_senders queried.");
  UNLOCK (ir_sender_store);
  return true;
}
#endif // openDHANA_ir__comms


//=============================================================================
// openDHANA_ir__lua_function__
//=============================================================================
#ifndef openDHANA_ir__lua_function__



/// Send a named IR command to a named device
/// TODO: add queue functions

int
openDHANA_ir__lua_function__send_ir (lua_State *L)
{
  LOCK (ir_sender_store);
  LOCK (ircommand_store);

  int argc = lua_gettop (L);

  if (argc != 2)
    {
      WARNING ("lua/function",
               "script \"openDHANA_send_ir\" needs exactly 2 parameters, ignored");

      if (!lua_isstring (L, 1) || !lua_isstring (L, 2))
        {
          WARNING ("lua/function",
                   "Lua 'openDAHANA_send_ir' expects 2 strings as parameters.");
          UNLOCK (ircommand_store);
          UNLOCK (ir_sender_store);
          return 0;
        }
      UNLOCK (ircommand_store);
      UNLOCK (ir_sender_store);
      return 0;
    }

  string ir_command = lua_tostring (L, 1);
  string device = lua_tostring (L, 2);

  INFO ("lua/function",
        "openDHANA_send_ir(\"" + ir_command + "\", \"" + device + "\").");

  // Get the the ir port of the device. That is where we send it

  string itach = device_store[device].itach;
  string ir_port = device_store[device].ir_port;
  string equipment = device_store[device].equipment;

  // Get the socket that is opened to the named IR sender

  int socket = ir_sender_store[itach].socket;

  // Get the actual IR command that we need to send
  string key = equipment + "::" + ir_command;

  if (ircommand_store.count (key) == 0)
    {
      WARNING ("comms/protocol",
               "ir command \"" + ir_command + "\" for \"" + device
               + "\" not defined, ignored.");
      UNLOCK (ircommand_store);
      UNLOCK (ir_sender_store);
      return 0;
    }

  // The command exists, check its format

  string ir;
  if (ircommand_store[key].ir_type == "hex")
    ir = openDHANA_ir__comms__hex_to_sendir (ircommand_store[key].ir_command);
  else
    ir = ircommand_store[key].ir_command;

  //sendir,<mod-addr>:<conn-addr>,1,38000,<repeatcount>,1,341,170,22,...

  // TODO: add running serial

  string command = "sendir," + ir_port + ",9999,38000,1," + ir + "\r";

  string response =
          openDHANA_ir__comms__send_and_get_response (socket, command);

  response.erase (response.size () - 1); // Remove '\r'

  if (response != "completeir," + ir_port + ",9999")
    {
      WARNING ("comms/protocol",
               "itach response \"" + response + "\" not expected.");
    }
  UNLOCK (ircommand_store);
  UNLOCK (ir_sender_store);
  return 0;
}




#endif // openDHANA_ir__lua_function__

/// We get a message from the MQTT broker
///
/// @param internal_topic       the internal topic that the MQTT topic
///                             translated into.
/// @param message              the MQTT message
///

void
moduleMessageCallback (const string& internal_topic,
                       const string& message)
{
  // Cache the message
  openDHANA__lua__value_cache[internal_topic] = message;

  INFO ("mqtt/comms",
        "internal_topic: \"" + internal_topic + "\" = \"" + message + "\".");

  openDHANA__lua__call_function_in_all_scripts (internal_topic, message);
}

///
//=============================================================================
// openDHANA_ir__config_files__
//=============================================================================
#ifndef openDHANA_ir__config_files__

/// Read the device file
///
/// @param device_file          the path to the device file.
///

bool
openDHANA_ir__config_files__read_devices (const string& device_file)
{
  LOCK (ir_sender_store);

  char buffer[255];
  int line_number = 0;

  ir_sender_store.clear ();

  FILE *f = openDHANA__generic__open_file (device_file);
  if (f == NULL)
    exit (1);

  while (!feof (f))
    {
      if (fgets (buffer, sizeof (buffer), f))
        {
          if (openDHANA__config__process_this_line (buffer))
            {
              // Split into command and parameters
              string_vector line = openDHANA__config__split_line (buffer);

              //itach name="upstairs" address="upstairs-itach-1.manisvart.se" port=4998

              if (line[0] == "itach")
                {
                  string_vector options =
                          openDHANA__config__split_parameters (line[1]);

                  std::map < string, Option > itach;

                  itach["name"] =
                          Option (OptionRequired, "",
                                  "^\\s*(name)\\s*=\\s*\"(.*)\"\\s*$");
                  itach["address"] =
                          Option (OptionRequired, "",
                                  "^\\s*(address)\\s*=\\s*\"(.*)\"\\s*$");
                  itach["port"] =
                          Option (OptionRequired, "",
                                  "^\\s*(port)\\s*=\\s*([0-9]+)\\s*$");

                  if (!openDHANA__options__check (options, itach))
                    openDHANA__options__invalid_missing_options (device_file,
                                                                 line_number);
                  else
                    {
                      ir_sender sender;
                      sender.address = itach["address"].getValue ();
                      sender.port = itach["port"].getValue ();
                      sender.type = "itach";
                      ir_sender_store[itach["name"].getValue ()] = sender;
                    }
                }
              else if (line[0] == "device")
                {
                  //device name="Yamaha receiver" itach="upstairs" ir_port="1:1" equipment="Yamaha Receiver/Preamp RXV1065/765/665/565/465 Main Zone"

                  string_vector options =
                          openDHANA__config__split_parameters (line[1]);

                  std::map < string, Option > device;

                  device["name"] =
                          Option (OptionRequired, "",
                                  "^\\s*(name)\\s*=\\s*\"(.*)\"\\s*$");
                  device["itach"] =
                          Option (OptionRequired, "",
                                  "^\\s*(itach)\\s*=\\s*\"(.*)\"\\s*$");
                  device["ir_port"] =
                          Option (OptionRequired, "",
                                  "^\\s*(ir_port)\\s*=\\s*\"(1:1|1:2|1:3)\"\\s*$");
                  device["equipment"] =
                          Option (OptionRequired, "",
                                  "^\\s*(equipment)\\s*=\\s*\"(.*)\"\\s*$");

                  if (!openDHANA__options__check (options, device))
                    openDHANA__options__invalid_missing_options (device_file,
                                                                 line_number);
                  else
                    {
                      ir_device dev;
                      dev.itach = device["itach"].getValue ();
                      dev.ir_port = device["ir_port"].getValue ();
                      dev.equipment = device["equipment"].getValue ();
                      device_store[device["name"].getValue ()] = dev;
                    }
                }
              else
                {
                  // Illegal command
                  openDHANA__options__invalid_missing_options (device_file,
                                                               line_number);
                }
            }
        }
    }
  fclose (f);
  UNLOCK (ir_sender_store);

  return true;
}

/// Read the ir commands file
///
/// @param ircommands_file      the path to the ircommands file.
///

bool
openDHANA_ir__config_files__read_ircommands (const string& ircommands_file)
{
  LOCK (ircommand_store);
  char buffer[1024];
  char line_number_str[80];
  int line_number = 0;

  ircommand_store.clear ();

  FILE *f = openDHANA__generic__open_file (ircommands_file);
  if (f == NULL)
    exit (1);

  while (!feof (f))
    {
      if (fgets (buffer, sizeof (buffer), f))
        {
          snprintf (line_number_str, sizeof(line_number_str), "%d", ++line_number);

          if (openDHANA__config__process_this_line (buffer))
            {
              // Split into command and parameters
              string_vector line = openDHANA__config__split_line (buffer);

              //ir_command equipment="Yamaha Receiver/Preamp RXV1065/765/665/565/465 Main Zone" command="power off" type=sendir ir="lksajdlkjsadkljasdlkjasdlkajsdlkj"

              if (line[0] == "ir_command")
                {
                  string_vector options =
                          openDHANA__config__split_parameters (line[1]);

                  std::map < string, Option > irc;

                  irc["equipment"] =
                          Option (OptionRequired, "",
                                  "^\\s*(equipment)\\s*=\\s*\"(.*)\"\\s*$");
                  irc["command"] =
                          Option (OptionRequired, "",
                                  "^\\s*(command)\\s*=\\s*\"(.*)\"\\s*$");
                  irc["type"] =
                          Option (OptionRequired, "",
                                  "^\\s*(type)\\s*=\\s*(sendir|hex)\\s*$");
                  irc["ir"] =
                          Option (OptionRequired, "",
                                  "^\\s*(ir)\\s*=\\s*\"(.*)\"\\s*$");

                  if (!openDHANA__options__check (options, irc))
                    openDHANA__options__invalid_missing_options (ircommands_file,
                                                                 line_number);
                  else
                    {
                      ir_command ircmd;
                      ircmd.ir_command = irc["ir"].getValue ();
                      ircmd.ir_type = irc["type"].getValue ();
                      ircommand_store[irc["equipment"].getValue ()
                              + "::" + irc["command"].getValue ()] = ircmd;
                    }
                }
              else
                {
                  // Illegal command
                  openDHANA__options__invalid_missing_options (ircommands_file,
                                                               line_number);
                }
            }
        }
    }
  fclose (f);
  UNLOCK (ircommand_store);

  return true;
}


#endif // openDHANA_ir__config_files__

/// Set the unique ir options
///

void
openDHANA_ir__options__set_ir ()
{
  openDHANA_option_store["ir_options_file"] =
          Option (OptionOptional, OPTIONS_FILE_IR,
                  "^\\s*(scriptor_options_file)\\s*=\\s*\"(.*)\"\\s*$");
  openDHANA_option_store["ir_lua_directory"] =
          Option (OptionOptional, "lua",
                  "^\\s*(ir_lua_directory)\\s*=\\s*\"(.*)\"\\s*$");
  openDHANA_option_store["ir_devices_file"] =
          Option (OptionOptional, "",
                  "^\\s*(ir_devices_file)\\s*=\\s*\"(.*)\"\\s*$");
  openDHANA_option_store["ir_ircommands_file"] =
          Option (OptionOptional, "",
                  "^\\s*(ir_ircommands_file)\\s*=\\s*\"(.*)\"\\s*$");
}

/// The device file has been modified.
/// Called by @openDHANA__config__file_monitor.
///
/// @param path                 the path to the file.
///

void
openDHANA_ir__config_files__devices_changed (const string path)
{
  openDHANA_ir__comms__disconnect_senders ();
  openDHANA_ir__config_files__read_devices (path);
  openDHANA_ir__comms__connect_senders ();
  openDHANA_ir__comms__query_senders ();
}

/// The ir commands file has been modified.
/// Called by @openDHANA__config__file_monitor.
///
/// @param path                 the path to the file.
///

void
openDHANA_ir__config_files__ircommands_changed (const string path)
{
  openDHANA_ir__config_files__read_ircommands (path);
}




/// Process irs options
///

void
openDHANA_ir__options__process () {
  // Nothing to do here, move along!
}

/// Setup scriptor specific file monitors
///

void
openDHANA_ir__config_files__monitor ()
{
  openDHANA__config__file_monitor (OPTION (ir_devices_file),
                                   &openDHANA_ir__config_files__devices_changed);
  openDHANA__config__file_monitor (OPTION (ir_ircommands_file),
                                   &openDHANA_ir__config_files__ircommands_changed);
}

/// Register custom Lua c functions
///

void
openDHANA__lua__custom_scripts ()
{
  openDHANA__lua__add_external_function ("openDHANA_send_ir",
                                         &openDHANA_ir__lua_function__send_ir);
}
//=============================================================================
// openDHANA_ir__
//=============================================================================

int
main (int argc, char *argv[])
{
  // Add irs options
  openDHANA_ir__options__set_ir ();

  // Process options
  openDHANA__options__get (OPTION_DEFAULT (ir_options_file),
                           argc,
                           argv);

  // Set internal values based on options
  openDHANA_ir__options__process ();

  // Setup the process and daemonize if required
  openDHANA__generic__init_process ();
  STARTING ("openDHANA-ir with Lua release \"" + string (LUA_RELEASE) + "\".");

  // Read config files
  openDHANA_ir__config_files__read_devices (OPTION (ir_devices_file));
  openDHANA_ir__config_files__read_ircommands (OPTION (ir_ircommands_file));

  // Setup additional file monitors
  openDHANA_ir__config_files__monitor ();

  openDHANA_ir__comms__connect_senders ();
  openDHANA_ir__comms__query_senders ();

  // Connect to the MQTT broker
  openDHANA_mqtt__communication__connect_broker ();

  // Get the current scripts, and start them
  openDHANA__lua__add_external_functions ();
  openDHANA__lua__lua_dir_read (OPTION (ir_lua_directory));

  // Let the threads do their work and wait for exit signal
  openDHANA__generic__process_loop ();

  // Disconnect the broker(s)
  openDHANA_mqtt__communication__disconnect_broker ();

  // Stop all scripts
  openDHANA__lua__stop_all_scripts ();

  openDHANA_ir__comms__disconnect_senders ();


  STOPPING ("openDHANA-ir");
  openDHANA__generic__kill_process ();
}

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

bool dhana_mqtt_exiting = false;
bool dhana_mqtt_debug = false;
bool dhana_mqtt_logtofile = false;
FILE *dhana_log_file = NULL;
std::map<string, mqtt_pub> openDHANA_mqtt_publications;
std::map<string, mqtt_sub> openDHANA_mqtt_subscriptions;


//=============================================================================
// openDHANA__generic__
//=============================================================================
#ifndef openDHANA__generic__

/// Format a time_t
///
/// @param time                 the time_t.
/// @return                     A string in the format "2015-01-01 19:24:17".
///

string
openDHANA__generic__log_format_time (time_t time)
{
  struct tm * timeinfo;
  char buffer[255];

  timeinfo = localtime (&time);
  strftime (buffer, sizeof (buffer), "%F %T", timeinfo);

  return string (buffer);
}


/// Print a log message
///
/// If a log file is open, write to it, otherwise write to stderr.
/// The message will be prepended with the current date and time.
///
/// @param log_message          the log message.
///

void
openDHANA__generic__log_message (const string& log_message)
{
  time_t now;

  time (&now);
  if (dhana_mqtt_logtofile)
    {
      fprintf (dhana_log_file, "%s: %s\n",
               openDHANA__generic__log_format_time (now).c_str (),
               log_message.c_str ());
      fflush (dhana_log_file);
    }
  else
    {
      fprintf (stderr, "%s: %s\n",
               openDHANA__generic__log_format_time (now).c_str (),
               log_message.c_str ());
      fflush (stdout);
    }
}


/// Print an error message
///
/// @param facility             the "facility" that the error occurred in.
/// @param error_message	the error message.
/// @param function     	the function that the error occurred in. Use
///                             __func__.
/// @param line_number  	the line number in the function that the error
///                             occurred at. Use __LINE__.
/// @return                     Nothing.
///

void
openDHANA__generic__log_error (const string& facility,
                               const string& error_message,
                               const string& function,
                               const int line_number)
{
  char message[512];

  snprintf (message, sizeof(message), "%8s: %17s: %s (%s:%d)",
           "error",
           facility.c_str (),
           error_message.c_str (),
           function.c_str (),
           line_number);

  openDHANA__generic__log_message (message);
}

/// Print a warning message
///
/// @param facility             the "facility" that the error occurred in.
/// @param warning_message	the warning message.
/// @param function     	the function that the warning occurred in. Use
///                             __func__.
/// @param line_number  	the line number in the function that the warning
///                             occurred at. Use __LINE__.
/// @return                     Nothing.
///

void
openDHANA__generic__log_warning (const string& facility,
                                 const string& warning_message,
                                 const string& function,
                                 const int line_number)
{
  char message[512];

  snprintf (message, sizeof(message), "%8s: %17s: %s",
           "warning",
           facility.c_str (),
           warning_message.c_str ());

  openDHANA__generic__log_message (message);
}

/// Print a log message
///
/// @param facility             the "facility" that the log occurred in.
/// @param log_message          the log message.
/// @param function     	the function that the log occurred in. Use
///                             __func__.
/// @param line_number  	the line number in the function that the log
///                             occurred at. Use __LINE__.
/// @return                     Nothing.
///

void
openDHANA__generic__log_info (const string& facility,
                              const string& info_message,
                              const string& function,
                              const int line_number)
{
  char message[512];

  snprintf (message, sizeof(message), "%8s: %17s: %s",
           "info",
           facility.c_str (),
           info_message.c_str ());

  openDHANA__generic__log_message (message);
}

/// Check if the data matches the regular expression (regex).
///
/// @param regex            the regular expression to match.
/// @param data             the data that is matched against the regular
///                         expression.
/// @returns                __true__ if the data matches, else __false__.
///

bool
openDHANA__generic__reg_ex_match (const string& regex,
                                  const string& data)
{
  regex_t rc;
  regmatch_t m[1 + 1];

  int status = regcomp (&rc, regex.c_str (), REG_EXTENDED);

  if (status != 0)
    {
      WARNING ("regex/comp", "compiling regex \"" + regex
               + "\" with data \"" + data + "\".");
      return false;
    }

  const char *p = data.c_str ();
  int match = regexec (&rc, p, 1, m, 0);
  regfree (&rc);

  if (match == 0)
    return true;

  return false;
}

/// Match data to a regular expression (regex), that must contain groups,
/// and return the groups.
///
/// The regular expression will only be matched one time against the data.
/// The normal group 0, which matches the whole data, will be excluded and the
/// first item in the returned vector will be the first _group_ matched.
///
/// @param max_groups           the maximum number of groups we expect to get,
///                             based on the regular expression we provide.
/// @param regex		the regular expression.
/// @param data			the data that is matched against the regular
///                             expression.
/// @returns			A string vector with the parts, if the data that
///                             matched the groups.
///				If no groups matched, the an empty vector is
///                             returned.
///

string_vector
openDHANA__generic__reg_ex_match_groups (const int max_groups,
                                         const string& regex,
                                         const string& data)
{
  regex_t rc;
  string_vector res;
  regmatch_t m[max_groups + 1];

  int status = regcomp (&rc, regex.c_str (), REG_EXTENDED);

  if (status != 0)
    {
      WARNING ("regex/comp", "compiling regex \"" + regex
               + "\" with data \"" + data + "\".");
      return res;
    }

  const char *p = data.c_str ();
  int nomatch = regexec (&rc, p, max_groups + 1, m, 0);
  if (nomatch)
    {
      regfree (&rc);
      return res;
    }

  for (int i = 0; i != max_groups; i++)
    {
      res.push_back (data.substr (m[i + 1].rm_so,
                                  m[i + 1].rm_eo - m[i + 1].rm_so));
    }

  regfree (&rc);
  return res;
}

/// Match data to a regular expression (regex), that must contain groups,
/// and return the groups.
///
/// The regular expression will be matched as many times as possible until the
/// data is exhausted.
/// The normal group 0, which matches the whole data, will be excluded and the
/// first item in the returned vector will be the first _group_ matched.
/// The regular expression should not contain any anchors (^$), and should not
/// be "greedy".
///
/// @param regex                the regular expression.
/// @param data			the data that is matched against the regular
///                             expression.
/// @returns			A string vector with the parts f the data that
///                             matched the groups.
///				If no groups matched, the an empty vector is
///                             returned.
///

string_vector
openDHANA__generic__reg_ex_match_groups_loop (const string& regex,
                                              const string& data)
{
  regex_t rc;
  string_vector res;
  regmatch_t m;
  const char *exp = regex.c_str ();
  const char *dat = data.c_str ();

  int status = regcomp (&rc, exp, REG_EXTENDED);

  if (status != 0)
    {
      WARNING ("regex/comp", "compiling regex \"" + regex
               + "\" with data \"" + data + "\".");
      return res;
    }

  // First match
  int offset = 0;
  int nomatch = regexec (&rc, dat, 1, &m, 0);
  while (nomatch == 0)
    {
      res.push_back (data.substr (offset + m.rm_so, m.rm_eo - m.rm_so));
      offset += m.rm_eo;

      // Next possible hit
      nomatch = regexec (&rc, dat + offset, 1, &m, 0);
    }

  regfree (&rc);
  return res;
}

/// Generate a MD5 hash.
///
/// @param string               the string that the MD5 hash is to be calculated
///                             for.
/// @return                     The MD5 hash as a string.
///

string
openDHANA__generic__md5_hash (const string& the_string)
{
  const char *s = the_string.c_str ();
  unsigned char digest[MD5_DIGEST_LENGTH];

  MD5 ((unsigned char *) s, strlen (s), (unsigned char *) &digest);

  char mdString[33];
  for (int i = 0; i < 16; i++)
    snprintf (&mdString[i * 2], sizeof(mdString) - (i*2), "%02x", (unsigned int) digest[i]);

  return string (mdString);
}

/// Generate a unique MD5 hash based on the current system times.
///
/// @return                     the unique MD5 hash as a string.
///

string
openDHANA___generic__md5_hash_unique ()
{
  struct timeval tv;

  if (gettimeofday (&tv, NULL))
    ERROR ("md5/calc", "error getting time of day.");

  char tbuf[255];

  snprintf (tbuf, sizeof(tbuf), "%ld-%ld", tv.tv_sec, tv.tv_usec);
  return openDHANA__generic__md5_hash (string (tbuf));
}

/// Loop and sleep until it is time to exit.
///
/// @return                     nothing.
///

void
openDHANA__generic__process_loop ()
{
  while (!dhana_mqtt_exiting)
    {
      sleep (2);
    }
}

/// Handle signals.
///
/// @return                     nothing.
///

void
openDHANA__generic__signal_handler (int signal)
{
  if (signal == SIGINT || signal == SIGTERM)
    {
      if (signal == SIGINT)
        INFO ("main/exec", "exiting on signal \"SIGINT\".");
      if (signal == SIGTERM)
        INFO ("main/exec", "exiting on signal \"SIGTERM\".");

      dhana_mqtt_exiting = true;
    }
}

/// Open a file for read and log an error message if something went wrong.
///
/// @return                     File descriptor or NULL if an error occured.
///

FILE *
openDHANA__generic__open_file (const string& path)
{
  FILE *f = fopen (path.c_str (), "r");
  if (f == NULL)
    ERROR ("file/open", "opening \"" + path + "\" for read.");

  return f;
}

/// Open the log file for write and log an error message if something went
/// wrong.
///
/// @return                     Nothing.
///

void
openDHANA__generic__open_log_file (const string& path)
{
  dhana_log_file = fopen (path.c_str (), "a");
  if (dhana_log_file == NULL)
    {
      ERROR ("file/open", "opening \"" + path + "\" for append.");
      exit (1);
    }
  dhana_mqtt_logtofile = true;
}

/// Setup process and prepare for running as a daemon.
///
/// @return                     Nothing.
///

void
openDHANA__generic__init_process ()
{
  // Register signal and signal handler
  signal (SIGINT, openDHANA__generic__signal_handler);
  signal (SIGTERM, openDHANA__generic__signal_handler);

  pid_t pid, sid;

  if (OPTION (daemon) == "true")
    {
      // Run as a daemon
      /* Fork off the parent process */
      pid = fork ();
      if (pid < 0)
        {
          exit (EXIT_FAILURE);
        }
      /* If we got a good PID, then
       we can exit the parent process. */
      if (pid > 0)
        {
          exit (EXIT_SUCCESS);
        }

      /* Change the file mode mask */
      umask (0);

      /* Open any logs here */
      if (OPTION (mqtt_log_file) != "")
        openDHANA__generic__open_log_file (OPTION (mqtt_log_file));

      /* Create a new SID for the child process */
      sid = setsid ();
      if (sid < 0)
        {
          /* Log any failures here */
          ERROR ("main/exec", "can't create a SID for the child proccess.");
          exit (EXIT_FAILURE);
        }

      /* Change the current working directory */
      if ((chdir ("/")) < 0)
        {
          /* Log any failures here */
          ERROR ("main/exec", "can't chdir to \"/\".");
          exit (EXIT_FAILURE);
        }

      /* Close out the standard file descriptors */
      close (STDIN_FILENO);
      close (STDOUT_FILENO);
      close (STDERR_FILENO);

      /* Daemon-specific initialization goes here */
      INFO ("main/exec", "running as a daemon.");
    }
  else
    {
      // Run in foreground

      if (OPTION (mqtt_log_file) != "")
        openDHANA__generic__open_log_file (OPTION (mqtt_log_file));

      INFO ("main/exec", "running in foreground.");
    }

  // Start file monitor thread and monitor MQTT file
  openDHANA__config__file_monitor_start ();
  openDHANA_mqtt__config_files__monitor ();

}

/// Tear down and clean up the process.
///
/// @return                     Nothing.
///

void
openDHANA__generic__kill_process ()
{
  if (dhana_log_file != NULL)
    fclose (dhana_log_file);

  dhana_mqtt_logtofile = false;
}

#endif // openDHANA__generic__

//=============================================================================
// openDHANA__config__
//=============================================================================
#ifndef openDHANA__config__

class file_monitor_entry /// Helper to keep information about a file or directory
{
public:
  struct stat node_stat;
  void
  (*callback) (string); /// Callback if the node has changed
};

/// List of monitored files or directories and their modification times
///
std::map<string, file_monitor_entry> file_monitor_path_and_info;
CREATE_LOCK (file_monitor_path_and_info);

/// Thread for the background monitoring of changes
///
pthread_t file_monitor_path_and_info_thread;

/// Scan through the files in a directory and return the latest modification
/// time of all the files.
///
/// @param path                 the path to the directory.
/// @return                     A struct stat with the st_mtime and st_ctime.
///

struct stat
openDHANA__config__file_monitor_scan_directory (const string& path)
{
  DIR *dirp;
  struct dirent *dp;
  struct stat saved_stat;
  struct stat node_stat;

  saved_stat.st_mtime = 0;
  saved_stat.st_ctime = 0;

  dirp = opendir (path.c_str ());
  while ((dp = readdir (dirp)) != NULL)
    {
      // Ignore the directory entries

      if (string (dp->d_name) == "." || string (dp->d_name) == "..")
        continue;

      string absolute_path = path + "/" + dp->d_name;

      if (stat (absolute_path.c_str (), &node_stat) == -1)
        {
          // Node doesn't exist
          ERROR ("file/monitor",
                 "\"" + absolute_path + "\", " + strerror (errno));
        }
      else
        {
          if (node_stat.st_mtime > saved_stat.st_mtime
              || node_stat.st_ctime > saved_stat.st_ctime)
            {
              saved_stat = node_stat;
            }
        }
    }

  (void) closedir (dirp);
  return saved_stat;
}

/// Thread to check if any changes are made to configurations files or directories.
///
/// If a file or directory has been modified then its callback function will be called
/// with the path as a parameter.
/// If a file or directory has been deleted from the filesystem, then it will be removed
/// from the list.
///

void *
openDHANA__config__file_monitor_thread (void *param)
{
  struct stat node_stat;

  while (!dhana_mqtt_exiting)
    {
      LOCK (file_monitor_path_and_info);

      for (std::map<string, file_monitor_entry>::iterator it =
              file_monitor_path_and_info.begin ();
              it != file_monitor_path_and_info.end ();)
        {
          if (stat (it->first.c_str (), &node_stat) == -1)
            {
              // TODO: errno = ENOENT
              // Node doesn't exist any longer
              WARNING ("file/monitor",
                       "missing \"" + it->first + "\", removing from list.");
              file_monitor_path_and_info.erase (it++);
              //      exit (EXIT_FAILURE);
            }
          else
            {
              if (S_ISDIR (node_stat.st_mode))
                {
                  // A directorys timestamp is only updated when a file is
                  // added or deleted, not when a file in the directory is
                  // changed.
                  //
                  // Our method is instead to scan the files in the directory
                  // and store the modification date of the last modified file
                  // as the directorys modification time.

                  node_stat =
                          openDHANA__config__file_monitor_scan_directory (it->first);
                }

              if (it->second.node_stat.st_mtime != node_stat.st_mtime
                  || it->second.node_stat.st_ctime != node_stat.st_ctime)
                {
                  // Has changed. Save new time and call callback
                  INFO ("file/monitor", "changed \"" + it->first + "\".");
                  it->second.node_stat = node_stat;
                  (*(it->second).callback) (it->first);
                }
              it++;
            }
        }

      UNLOCK (file_monitor_path_and_info);
      // TODO: add configuration option for time
      sleep (2);
    }
  return NULL;
}

/// Start the file or directory monitoring thread.
///

void
openDHANA__config__file_monitor_start ()
{
  if (pthread_create (&file_monitor_path_and_info_thread,
                      NULL,
                      openDHANA__config__file_monitor_thread,
                      NULL) != 0)
    ERROR ("file/monitor", "error creating thread");

  // TODO: put this line below in the right place:
  //pthread_join (file_monitor_path_and_info_thread, NULL);
}

/// Add a file or directory to the monitor list
///
/// The path provided is checked to see if it is valid to monitor.
///
/// @param file_or_dir          the file or directory path.
/// @param callback             the callback function.
/// @return                     __true__ if the file could be added to the list,
///                             __false__ otherwise.
///

bool
openDHANA__config__file_monitor (const string& file_or_dir,
                                 void (*callback) (string))
{
  LOCK (file_monitor_path_and_info);

  struct stat node_stat;

  if (stat (file_or_dir.c_str (), &node_stat) == -1)
    {
      // Node doesn't exist
      ERROR ("file/monitor", "\"" + file_or_dir + "\" " + strerror (errno));
    }
  else
    {
      // Check what kind of node the path is
      switch (node_stat.st_mode & S_IFMT)
        {
        case S_IFBLK:
          WARNING ("file/monitor",
                   "\"" + file_or_dir
                   + "\", sorry, we don't monitor block devices.");

          UNLOCK (file_monitor_path_and_info);
          return false;
          break;
        case S_IFCHR:
          WARNING ("file/monitor",
                   "\"" + file_or_dir
                   + "\", sorry, we don't monitor character devices.");

          UNLOCK (file_monitor_path_and_info);
          return false;
          break;
        case S_IFDIR:
          //	  printf ("directory\n");
          node_stat =
                  openDHANA__config__file_monitor_scan_directory (file_or_dir);
          break;
        case S_IFIFO:
          WARNING ("file/monitor",
                   "\"" + file_or_dir
                   + "\", sorry, we don't monitor FIFI/pipe devices.");

          UNLOCK (file_monitor_path_and_info);
          return false;
          break;
        case S_IFLNK:
          //	  printf ("symlink\n");
          break;
        case S_IFREG:
          //	  printf ("regular file\n");
          break;
        case S_IFSOCK:
          WARNING ("file/monitor",
                   "\"" + file_or_dir
                   + "\", sorry, we don't monitor socket devices.");

          UNLOCK (file_monitor_path_and_info);
          return false;
          break;
        default:
          WARNING ("file/monitor",
                   "\"" + file_or_dir
                   + "\", sorry, we don't monitor unknown devices.");

          UNLOCK (file_monitor_path_and_info);
          return false;
          break;
        }
    }

  file_monitor_entry entry;
  entry.node_stat = node_stat;
  entry.callback = callback;

  // Save initial modtime
  file_monitor_path_and_info[file_or_dir] = entry;

  INFO ("file/monitor", "monitoring \"" + file_or_dir + "\".");

  UNLOCK (file_monitor_path_and_info);
  return true;
}

/// Check if a line is to be processed or not.
///
/// Comment lines and empty lines are not processed.
///
/// @param line                 a line from a configuration file.
/// @return                     __true__ if the line should be processed,
///                             __false__ otherwise.
///

bool
openDHANA__config__process_this_line (const string& line)
{
  if (openDHANA__generic__reg_ex_match ("^#.*$", line))
    return false; // a comment line, do not process
  if (openDHANA__generic__reg_ex_match ("^;.*$", line))
    return false; // a comment line, do not process
  if (openDHANA__generic__reg_ex_match ("^//.*$", line))
    return false; // a comment line, do not process
  if (openDHANA__generic__reg_ex_match ("^\\s*$", line))
    return false; // an empty line, do not process

  return true;
}

/// Split a configuration file line into the command and the options part
///
/// Example line:
/// multilevel_lamp screen="housemap" display_value="sub_lamp4_get" ...
///
/// @param line                 one line from a configuration file. Empty lines
///                             and lines with comments must not be passed to
///                             this function.
/// @returns                    A string vector with the first element
///                             containing the command and the second element
///                             all the parameters.
///

string_vector
openDHANA__config__split_line (const string& line)
{
  return openDHANA__generic__reg_ex_match_groups (2,
                                                  "^\\s*(\\w+)\\s+(.*)$",
                                                  line);
}

/// Split a string with options into a vector of options
///
/// Example string:
/// screen="housemap" display_value="sub_lamp4_get" ...
///
/// These versions of options are allowed:
/// - key="string"
/// - key='string'
/// - key=letters_or_underscores
/// - key=12345 (positive integers)
/// - key=-12345 (negative integers)
/// - key=0.12345 (postove decimal numbers)
/// - key=-0.12345 (negative decimal numbers)
///
/// @param all_options          the string with options.
///                             @see openDHANA__config__split_line
/// @returns			A vector with the first element containing the
///                             command and the second element all
///                             the parameters.
///

string_vector
openDHANA__config__split_parameters (const string& all_options)
{
  return openDHANA__generic__reg_ex_match_groups_loop ("\\s*\\w+\\s*=\\s*(-?[0-9]*\\.[0-9]+|-?[0-9]+|\\w+|\"[^\"]*\"|'[^']*')\\s*",
                                                       all_options);
}


#endif // openDHANA__config__

//=============================================================================
// openDHANA__option__
//=============================================================================
#ifndef openDHANA__option__

std::map<string, Option> openDHANA_option_store;

Option::Option (void) { }

Option::Option (const bool required, const string& default_value,
                const string& reg_ex)
{
  _reg_ex = reg_ex;
  _default_value = default_value;
  _required = required;
}

void
Option::setValue (const string& value)
{
  _value = value;
}

string
Option::getRegEx ()
{
  return _reg_ex;
}

bool
Option::isRequired (void)
{
  return _required;
}

string
Option::getDefaultValue (void)
{
  return _default_value;
}

string
Option::getValue (void)
{
  return _value;
}

/// Get the value of an option
///
/// If the option doesn't exist, then print an error message
///
/// @param option               the option we want.
/// @return                     the options value or "**error**" if no option
///                             was found

string
openDHANA__option__get_value (const string option)
{

  if (openDHANA_option_store.count (option))
    return openDHANA_option_store[option].getValue ();

  ERROR ("config/option", "option \"" + option + "\" is not defined.");
  return "**error**";
}

/// Get the value of an option
///
/// If the option doesn't exist, then print an error message
///
/// @param option               the option we want.
/// @return                     the options value or "**error**" if no option
///                             was found

string
openDHANA__option__get_default_value (const string option)
{

  if (openDHANA_option_store.count (option))
    return openDHANA_option_store[option].getDefaultValue ();

  ERROR ("config/option", "option \"" + option + "\" is not defined.");
  return "**error**";
}



/// Display a warning message about missing or invalid options.
///
/// @param all_options          the string with options.
///                             @see openDHANA__config__split_line
/// @returns			Nothing.
///

void
openDHANA__options__invalid_missing_options (const string file,
                                             int line_number)
{

  char line_number_str[10];
  snprintf (line_number_str, sizeof(line_number), "%d", line_number);

  WARNING ("config/syntax",
           "line \"" + file + ":" + line_number_str
           + "\" invalid or missing options, ignored.");
}

/// Display a warning message about unknown commands in a config file.
///
/// @param all_options          the string with options.
///                             @see openDHANA__config__split_line
/// @returns			Nothing.
///

void
openDHANA__options__uknown_definition (const string definition)
{

  WARNING ("config/syntax",
           "unknown definition \"" + definition + "\", ignored.");
}

/// Read options from an .option file
///
/// Comments and empty lines are ignored
///
/// @param path                 the path of the file to read.
/// @return                     A string vector with the options. Each option
///                             is still one string.
//

string_vector
openDHANA__options__read_file (const string& path)
{
  char buffer[255];
  string_vector options;

  FILE *f = openDHANA__generic__open_file (path);
  if (f == NULL)
    exit (1);

  while (!feof (f))
    {
      if (fgets (buffer, sizeof (buffer), f))
        {
          if (openDHANA__config__process_this_line (buffer))
            {
              // Save the option

              options.push_back (buffer);
            }
        }
    }
  fclose (f);
  return options;
}

/// Check that the "batch" of options we have are valid.
///
/// @param given_options        the list with options from files and command
///                             line.
/// @param defined_options      the list withe the definitions of required and
///                             optional options.
/// @returns			___true___ if all options are ok, __false__
///                             otherwise.
///

bool
openDHANA__options__check (const string_vector& given_options,
                           std::map<string, Option>& defined_options)
{
  bool all_ok = true;

  // Set the default values

  for (std::map<string, Option>::const_iterator defined_option =
          defined_options.begin (); defined_option != defined_options.end ();
          ++defined_option)
    {
      Option opt = defined_option->second;
      if (opt.getDefaultValue () != "")
        {
          defined_options[defined_option->first]
                  .setValue (opt.getDefaultValue ());
        }
    }

  // Go through the given options and verify that they are valid, and get their
  // values.
  // An option can be present more than one one, if for example a parameter that
  // is present in the .options file is overridden on the command line.
  // Therefore we walk the entire given options list even if we get a match.

  for (string_vector::const_iterator given_option = given_options.begin ();
          given_option != given_options.end ();
          ++given_option)
    {
      bool match = false;
      for (std::map<string, Option>::const_iterator defined_option =
              defined_options.begin ();
              defined_option != defined_options.end ();
              ++defined_option)
        {
          Option opt = defined_option->second;
          if (openDHANA__generic__reg_ex_match (opt.getRegEx (), *given_option))
            {
              match = true;

              // Get the value
              string_vector kv =
                      openDHANA__generic__reg_ex_match_groups (2,
                                                               opt.getRegEx (),
                                                               *given_option);
              defined_options[defined_option->first].setValue (kv[1]);
            }
        }

      if (!match)
        {
          WARNING ("config/syntax", "invalid option \"" + *given_option + "\"");
          all_ok = false;
        }
    }

  // Finally, check that all the required defined options are given

  for (std::map<string, Option>::const_iterator defined_option =
          defined_options.begin (); defined_option != defined_options.end ();
          ++defined_option)
    {
      Option opt = defined_option->second;
      if (opt.isRequired ())
        {
          bool match = false;
          for (string_vector::const_iterator given_option =
                  given_options.begin (); given_option != given_options.end ();
                  ++given_option)
            {
              if (openDHANA__generic__reg_ex_match (opt.getRegEx (),
                                                    *given_option))
                {
                  match = true;
                  break;
                }
            }

          if (!match)
            {

              WARNING ("config/syntax",
                       "required option \"" + defined_option->first
                       + "\" is missing.");
              all_ok = false;
            }
        }
    }
  return all_ok;
}

/// Set the generic options that are needed by all modules
///

void
openDHANA__options__set_generic ()
{

  openDHANA_option_store["mqtt_bind_address"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_bind_address)\\s*=\\s*([0-9]+)\\s*$");

  openDHANA_option_store["mqtt_disable_clean_session"] =
          Option (OptionOptional, "false",
                  "^\\s*(mqtt_disable_clean_session)\\s*=\\s*(true|false)\\s*$");

  openDHANA_option_store["mqtt_cafile"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_cafile)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_cert"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_cert)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_ciphers"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_ciphers)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["daemon"] =
          Option (OptionOptional, "false",
                  "^\\s*(daemon)\\s*=\\s*(true|false)\\s*$");

  openDHANA_option_store["mqtt_debug"] =
          Option (OptionOptional, "false",
                  "^\\s*(mqtt_debug)\\s*=\\s*(true|false)\\s*$");

  openDHANA_option_store["mqtt_host"] =
          Option (OptionOptional, "localhost",
                  "^\\s*(mqtt_host)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_id"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_id)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_id_prefix"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_id_prefix)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_insecure"] =
          Option (OptionOptional, "false",
                  "^\\s*(mqtt_insecure)\\s*=\\s*(true|false)\\s*$");

  openDHANA_option_store["mqtt_keepalive"] =
          Option (OptionOptional, "60",
                  "^\\s*(mqtt_keepalive)\\s*=\\s*([0-9]+)\\s*$");

  openDHANA_option_store["mqtt_key"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_key)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_log_file"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_log_file)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_map_file"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_map_file)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_port"] =
          Option (OptionOptional, "1883",
                  "^\\s*(mqtt_port)\\s*=\\s*([0-9]+)\\s*$");

  openDHANA_option_store["mqtt_pw"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_pw)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_psk"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_psk)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_psk_identity"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_psk_identity)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_srv_lookups"] =
          Option (OptionOptional, "false",
                  "^\\s*(mqtt_srv_lookups)\\s*=\\s*(true|false)\\s*$");

  openDHANA_option_store["mqtt_tls_version"] =
          Option (OptionOptional, "tlsv1.2",
                  "^\\s*(mqtt_tls_version)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_username"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_username)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_wait_for_broker"] =
          Option (OptionOptional, "false",
                  "^\\s*(mqtt_wait_for_broker)\\s*=\\s*(true|false)\\s*$");

  openDHANA_option_store["mqtt_will_payload"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_will_payload)\\s*=\\s*\"(.*)\"\\s*$");

  openDHANA_option_store["mqtt_will_qos"] =
          Option (OptionOptional, "0",
                  "^\\s*(mqtt_will_qos)\\s*=\\s*(0|1|2)\\s*$");

  openDHANA_option_store["mqtt_will_retain"] =
          Option (OptionOptional, "false",
                  "^\\s*(mqtt_will_retain)\\s*=\\s*(true|false)\\s*$");

  openDHANA_option_store["mqtt_will_topic"] =
          Option (OptionOptional, "",
                  "^\\s*(mqtt_will_topic)\\s*=\\s*\"(.*)\"\\s*$");
}

/// Set internal values based on options values
///

void
openDHANA__options__process ()
{

  if (OPTION (mqtt_debug) == "true")
    dhana_mqtt_debug = true;
}

/// Get the options from files and the command line. Also read the mqttmap.
///
/// @param option_file          the file containing the options.
/// @param argc                 from command line.
/// @param argv                 from command line
///

void
openDHANA__options__get (const string& option_file,
                         int argc,
                         char* argv[])
{
  openDHANA__options__set_generic (); // Add MQTT options

  string_vector options = openDHANA__options__read_file (option_file);

  // Read the command line and get its parameters, they will override the same
  // parameter from the file

  for (int i = 1; i != argc; i++)
    options.push_back (argv[i]);


  if (!openDHANA__options__check (options, openDHANA_option_store))
    {

      ERROR ("config/syntax",
             "invalid or missing options was encountered, exiting.");
      exit (1);
    }

  //  for (std::map<string, Option>::const_iterator it =
  //      openDHANA_option_store.begin (); it != openDHANA_option_store.end ();
  //      ++it)
  //    {
  //      Option o = it->second;
  //      printf ("%s---%s\n", it->first.c_str (), o.getValue ().c_str ());
  //    }

  // Set internal options based on options
  openDHANA__options__process ();

  openDHANA_mqtt__config_files__read_mqttmap (OPTION (mqtt_map_file));
}

#endif // openDHANA__option__

static std::map<int, string> valueGenre;

struct mosquitto *mosq = NULL;

// TODO: Remove these

void
setDoubleList (std::map<int, string> &forward,
               std::map<string, int> &reverse, int key, string value)
{

  forward[key] = value;
  reverse[value] = key;
}

void
setDoubleList (string_map &forward,
               string_map &reverse, string key,
               string value)
{

  forward[key] = value;
  reverse[value] = key;
}


//=============================================================================
// openDHANA_mqtt__
//=============================================================================
#ifndef openDHANA_mqtt__

CREATE_LOCK (openDHANA_mqtt_publications);

/// Read a .mqttmap file
///
/// @param path                 the path to the file.
///

void
openDHANA_mqtt__config_files__read_mqttmap (const string& path)
{
  LOCK (openDHANA_mqtt_publications);

  char buffer[255];
  int line_number = 0;

  std::map < string, Option > publish;

  publish["mqtt_topic"] =
          Option (OptionRequired, "",
                  "^\\s*(mqtt_topic)\\s*=\\s*\"(.*)\"\\s*$");

  publish["internal_topic"] =
          Option (OptionRequired, "",
                  "^\\s*(internal_topic)\\s*=\\s*\"(.*)\"\\s*$");

  publish["retain"] =
          Option (OptionRequired, "",
                  "^\\s*(retain)\\s*=\\s*(true|false)\\s*$");

  publish["qos"] =
          Option (OptionRequired, "",
                  "^\\s*(qos)\\s*=\\s*(0|1|2)\\s*$");

  std::map < string, Option > subscribe;

  subscribe["mqtt_topic"] =
          Option (OptionRequired, "",
                  "^\\s*(mqtt_topic)\\s*=\\s*\"(.*)\"\\s*$");

  subscribe["internal_topic"] =
          Option (OptionRequired, "",
                  "^\\s*(internal_topic)\\s*=\\s*\"(.*)\"\\s*$");

  subscribe["qos"] =
          Option (OptionRequired, "",
                  "^\\s*(qos)\\s*=\\s*(0|1|2)\\s*$");

  FILE *f = openDHANA__generic__open_file (path);
  if (f == NULL)
    {
      UNLOCK (openDHANA_mqtt_publications);
      exit (1);
    }

  // Clear the list, we might be rereading the list
  openDHANA_mqtt_publications.clear ();

  while (!feof (f))
    {
      if (fgets (buffer, sizeof (buffer), f))
        {
          if (openDHANA__config__process_this_line (buffer))
            {
              string_vector line = openDHANA__config__split_line (buffer);

              // TODO: add support for "macros" so that you can define a config tree for a specifik type of device and use that with many
              // macro "aeon_labs_multi_sensor_configs" "power_management/on_time" "${1}:config:COMMAND_CLASS_CONFIGURATION:1:3:short"
              // macro "aeon_labs_multi_sensor_configs" "reports/group_1_reports" "${1}:config:COMMAND_CLASS_CONFIGURATION:1:101:int"
              // sweden/strangnas/garage/sensor/config/${aeon_labs_multi_sensor_configs, 2}

              if (line[0] == "publish")
                {
                  string_vector options =
                          openDHANA__config__split_parameters (line[1]);

                  // Are all options provided ok?
                  if (!openDHANA__options__check (options, publish))
                    openDHANA__options__invalid_missing_options (path,
                                                                 line_number);
                  else
                    {
                      mqtt_pub pub;

                      pub.mqtt_topic = publish["mqtt_topic"].getValue ();
                      pub.retain = publish["retain"].getValue () == "true" ?
                              true : false;
                      pub.qos = atoi (publish["qos"].getValue ().c_str ());

                      openDHANA_mqtt_publications[publish["internal_topic"].getValue ()] =
                              pub;
                    }
                }
              if (line[0] == "subscribe")
                {
                  string_vector options =
                          openDHANA__config__split_parameters (line[1]);

                  // Are all options provided ok?
                  if (!openDHANA__options__check (options, subscribe))
                    openDHANA__options__invalid_missing_options (path,
                                                                 line_number);
                  else
                    {

                      mqtt_sub sub;

                      sub.internal_topic =
                              subscribe["internal_topic"].getValue ();
                      sub.qos = atoi (subscribe["qos"].getValue ().c_str ());

                      openDHANA_mqtt_subscriptions[subscribe["mqtt_topic"].getValue ()] =
                              sub;
                    }
                }
            }
        }
    }
  fclose (f);

  UNLOCK (openDHANA_mqtt_publications);
}

/// MQTT configuration has changed. Unsubscribe, reread, and subscribe.
/// Called by @openDHANA__config__file_monitor.
///
/// @param path                 path to file.
///

void
openDHANA_mqtt__config_files__config_changed (const string path)
{

  openDHANA_mqtt__communication__unsubscribe (openDHANA_mqtt_subscriptions);
  openDHANA_mqtt__config_files__read_mqttmap (path);
  openDHANA_mqtt__communication__subscribe (openDHANA_mqtt_subscriptions);
}

/// Set up file monitoring for all the files required.
///

void
openDHANA_mqtt__config_files__monitor ()
{

  openDHANA__config__file_monitor (OPTION (mqtt_map_file),
                                   &openDHANA_mqtt__config_files__config_changed);
}

/// Subscribe to the topics in a list
///
/// @param mqtt_subscriptions   the list with subscriptions.
///

void
openDHANA_mqtt__communication__subscribe (const std::map<string, mqtt_sub>& mqtt_subscriptions)
{
  LOCK (openDHANA_mqtt_publications);

  INFO ("mqtt/comms", "subscribing to topics.");

  for (std::map<string, mqtt_sub>::const_iterator mqtt_subscription =
          mqtt_subscriptions.begin ();
          mqtt_subscription != mqtt_subscriptions.end ();
          ++mqtt_subscription)
    {
      //TODO:addsupport for secondary

      mqtt_sub sub = mqtt_subscription->second;
      mosquitto_subscribe (mosq, NULL, mqtt_subscription->first.c_str (),
                           sub.qos);
      INFO ("mqtt/comms", "subscribe \"" + mqtt_subscription->first + "\".");
    }

  INFO ("mqtt/comms", "all topics subscribed.");

  UNLOCK (openDHANA_mqtt_publications);
}

/// Unsubscribe to the topics in a list
///
/// @param mqtt_subscriptions   the list with subscriptions.
///

void
openDHANA_mqtt__communication__unsubscribe (const std::map<string, mqtt_sub>& mqtt_subscriptions)
{
  LOCK (openDHANA_mqtt_publications);

  INFO ("mqtt/comms", "unsubscribing to topics.");

  for (std::map<string, mqtt_sub>::const_iterator mqtt_subscription =
          mqtt_subscriptions.begin ();
          mqtt_subscription != mqtt_subscriptions.end ();
          ++mqtt_subscription)
    {
      //TODO:addsupport for secondary

      mqtt_sub sub = mqtt_subscription->second;
      mosquitto_unsubscribe (mosq, NULL, mqtt_subscription->first.c_str ());
      INFO ("mqtt/comms", "unsubscribe \"" + mqtt_subscription->first + "\".");
    }

  INFO ("mqtt/comms", "all topics unsubscribed.");

  UNLOCK (openDHANA_mqtt_publications);
}


/// Map internal_topic to MQTT topic and publish the value to the MQTT broker.
///
/// @param mqtt_subscriptions   the list with subscriptions.
/// @param internal_topic       the internal topic from the module.
/// @param value                the value.
///

void
openDHANA_mqtt__communication__publish (std::map<string, mqtt_pub>& mqtt_publications,
                                        const string& internal_topic,
                                        const string& value)
{
  LOCK (openDHANA_mqtt_publications);

  // Check if we have a publish rule for this
  if (mqtt_publications.count (internal_topic) != 0)
    {
      // Yes, publish it

      mqtt_pub mqtt = mqtt_publications[internal_topic];

      mosquitto_publish (mosq,
                         NULL,
                         mqtt.mqtt_topic.c_str (),
                         value.length (),
                         value.c_str (),
                         mqtt.qos,
                         mqtt.retain);
    }
  else
    {

      WARNING ("mqtt/comms",
               "unmapped internal_topic \"" + internal_topic
               + "\", no publish done. check your config files.");
    }

  UNLOCK (openDHANA_mqtt_publications);
}

/// Connect callback from mosquitto.
///

void
openDHANA_mqtt__communication__connect_callback (struct mosquitto *mosq,
                                                 void *userdata,
                                                 int result)
{
  // TODO: if the mqtt server is not responding, wait and try until it works. Control by option

  INFO ("mqtt/comms", "connecting to MQTT broker.");

  if (!result)
    // Connected, subscribe to all our tooics
    openDHANA_mqtt__communication__subscribe (openDHANA_mqtt_subscriptions);

  else
    ERROR ("mqtt/comms", "connect to MQTT broker failed.");
}

/// Subscribe callback from mosquitto.
///

void
openDHANA_mqtt__communication__subscribe_callback (struct mosquitto *mosq,
                                                   void *userdata,
                                                   int mid,
                                                   int qos_count,
                                                   const int *granted_qos) {
  // Do nothing right now
}

/// We got a message from the broker.
///
/// Map the MQTT topic to an internal topic and call the module in order for it
/// to process the message.
///

void
openDHANA_mqtt__communication__message_callback (struct mosquitto *mosq,
                                                 void *userdata,
                                                 const struct mosquitto_message * message)
{
  LOCK (openDHANA_mqtt_publications);

  if (message->payloadlen)
    {
      // Map the mqtt_topic to the internal_topic
      // TODO:Add map as parameter
      string internal_topic =
              openDHANA_mqtt_subscriptions[message->topic].internal_topic;

      // Release the lock because the module might publish a message as a
      // result of the received message.

      UNLOCK (openDHANA_mqtt_publications);

      // Call the module
      moduleMessageCallback (internal_topic, (char *) message->payload);
    }
  else
    {
      // This might be a reset of a retained message
      // (mosquitto_pub -t <topic> -r -n)

      INFO ("mqtt/comms",
            "empty MQTT message \"" + string (message->topic) + "\"");

      UNLOCK (openDHANA_mqtt_publications);
    }
}

/// Connect to the MQTT broker.
///

bool
openDHANA_mqtt__communication__connect_broker ()
{
  mosq = mosquitto_new (openDHANA___generic__md5_hash_unique ().c_str (),
                        true,
                        NULL);

  if (!mosq)
    {
      ERROR ("mqtt/comms", "out of memory when creating mosquitto instance.");
      return false;
    }

  //      mosquitto_log_init(mosq, MOSQ_LOG_DEBUG | MOSQ_LOG_ERR | MOSQ_LOG_WARNING
  //                      | MOSQ_LOG_NOTICE | MOSQ_LOG_INFO, MOSQ_LOG_STDERR);

  mosquitto_connect_callback_set (mosq,
                                  openDHANA_mqtt__communication__connect_callback);
  mosquitto_message_callback_set (mosq,
                                  openDHANA_mqtt__communication__message_callback);
  mosquitto_subscribe_callback_set (mosq,
                                    openDHANA_mqtt__communication__subscribe_callback);

  string host = OPTION (mqtt_host);
  string port = OPTION (mqtt_port);
  string keepalive = OPTION (mqtt_keepalive);

  //TODO: add support for waiting for MQTT broker
  //TODO: add support for multiple brokers
  if (mosquitto_connect (mosq, host.c_str (), atoi (port.c_str ()),
                         atoi (keepalive.c_str ())))
    {

      ERROR ("mqtt/comms",
             "unable to connect to \"" + host + ":" + port
             + "\" with keepalive \"" + keepalive + "\"");
      return false;
    }

  // Start mosquitto loop. It will handle reconnects and processong of events
  mosquitto_loop_start (mosq);

  return true;
}

/// Disconnect from the MQTT broker.
///

void
openDHANA_mqtt__communication__disconnect_broker ()
{

  openDHANA_mqtt__communication__unsubscribe (openDHANA_mqtt_subscriptions);

  mosquitto_disconnect (mosq);
  mosquitto_loop_stop (mosq, false);
  mosquitto_destroy (mosq);
}


#endif // openDHANA_mqtt__

//=============================================================================
// openDHANA__lua__
//=============================================================================
#ifndef openDHANA__lua__

std::map<string, lua_State*> script_state;
CREATE_LOCK (script_state);

// Value cache that is sent to a script when it starts
string_map openDHANA__lua__value_cache;


/// Call a function in a Lua script, if the function exists
///
/// @param function             the function to call.
/// @param message              the string that is sent as a parameter to the
///                             script.
/// @param script               the Lua state for the script.
/// @return                     __true__ if the function existed and was called
///                             ok, __false__ otherwise.
///

bool
openDHANA__lua__call_function_in_script (const string& function,
                                         const string& message,
                                         lua_State* script)
{
  // Does this script have this function?
  lua_getglobal (script, function.c_str ());

  // Check the top of the stack, make sure it's a function
  if (lua_isfunction (script, -1))
    {
      // Yes, push parameter and call it
      lua_pushlstring (script, message.c_str (), message.length ());

      if (lua_pcall (script, 1, 0, 0) != 0)
        {
          WARNING ("lua/exec",
                   "error calling function \"" + function + "\" with \""
                   + message + "\".");
          return false;
        }
    }
  else
    {

      lua_pop (script, 1); // No, didn't have that function, remove from the stack
    }
  return true;
}

/// Call a function in all the Lua scripts, if it exists.
///
/// @param function             the function to call.
/// @param message              the string that is sent as a parameter to the
///

void
openDHANA__lua__call_function_in_all_scripts (const string& function,
                                              const string& message)
{
  // Find out which scripts have a callback for the internal_topic

  LOCK (script_state);

  for (std::map<string, lua_State*>::iterator script =
          script_state.begin (); script != script_state.end (); script++)
    {
      if (!openDHANA__lua__call_function_in_script (function, message, script->second))
        {

          WARNING ("lua/exec",
                   "error running \"" + function + "\" in script \""
                   + script->first + "\": \""
                   + lua_tostring (script->second, -1) + "\".");
        }
    }

  UNLOCK (script_state);
}


typedef int (*lua_callback)(lua_State *);
std::map <string, lua_callback> openDHANA__lua__lua_functions;

/// Start a Lua script
///
/// @param path                 the path to the script.
/// @return                     __true__ if the script started ok, __false__
///                             otherwise.
///

bool
openDHANA__lua__start_script (const string& path)
{
  // Create new Lua state and load the lua libraries

  INFO ("lua/exec", "starting script: \"" + path + "\".");

  script_state[path] = luaL_newstate ();

  luaL_openlibs (script_state[path]);

  for (std::map <string, lua_callback>::const_iterator callback = openDHANA__lua__lua_functions.begin ();
          callback != openDHANA__lua__lua_functions.end (); ++callback)
    {
      // make my_function() available to Lua programs
      lua_register (script_state[path],
                    callback->first.c_str (),
                    callback->second);
    }

  // Tell Lua to load and run the file
  if (luaL_loadfile (script_state[path], path.c_str ()) == LUA_ERRSYNTAX)
    {
      // Remove entry
      script_state.erase (path);

      ERROR ("lua/exec",
             "syntax error in LUA script \"" + path + "\", not loaded.");
      return false;
    }

  /* PRIMING RUN. FORGET THIS AND YOU'RE TOAST */
  if (lua_pcall (script_state[path], 0, 0, 0))
    {
      ERROR ("lua/exec", "priming run error in LUA script \"" + path
             + "\", not loaded.");
      return false;
    }

  INFO ("lua/exec", "script started: \"" + path + "\".");

  // Give the script earlier parameters

  for (string_map::const_iterator value =
          openDHANA__lua__value_cache.begin ();
          value != openDHANA__lua__value_cache.end ();
          ++value)
    {
      // printf("%s--%s\n", value->first.c_str(), value->second.c_str());

      openDHANA__lua__call_function_in_script (value->first,
                                               value->second,
                                               script_state[path]);
    }

  return true;
}


/// Stop a Lua script
///
/// @param path                 the path to the script.
/// @return                     __true__ if the script stopped ok, __false__
///                             otherwise.
///

bool
openDHANA__lua__stop_script (string path)
{

  INFO ("lua/exec", "stopping script \"" + path + "\".");

  lua_close (script_state[path]); // Stop the script
  script_state.erase (path); // Remove from list

  return true;
}

/// Stop all Lua scripts
///

bool
openDHANA__lua__stop_all_scripts ()
{
  LOCK (script_state);

  INFO ("lua/exec", "stopping all scripts.");

  for (std::map<string, lua_State*>::iterator script =
          script_state.begin (); script != script_state.end (); script++)
    {

      openDHANA__lua__stop_script (script->first);
    }

  INFO ("lua/exec", "all scripts stopped.");

  UNLOCK (script_state);
  return true;
}

class script_list_node
{
public:

  struct stat node_stat;
  bool seen;
};

std::map<string, script_list_node> script_list;

/// Scan a Lua directory for scripts. Start new scripts, restart modified 
/// scripts and stop deleted scripts.
///
/// @param path                 path to the directory containing the scripts.
///

bool
openDHANA__lua__lua_dir_read (const string& path)
{
  LOCK (script_state);

  DIR *dirp;
  struct dirent *dp;
  struct stat node_stat;

  // Mark the scripts in the list as not seen
  for (std::map<string, script_list_node>::iterator script =
          script_list.begin (); script != script_list.end (); ++script)
    {
      script->second.seen = false;
    }


  dirp = opendir (path.c_str ());
  while ((dp = readdir (dirp)) != NULL)
    {
      if (openDHANA__generic__reg_ex_match ("^.*\\.lua$", dp->d_name))
        {
          string absolute_path = path + "/" + dp->d_name;
          if (stat (absolute_path.c_str (), &node_stat) == -1)
            {
              // Node doesn't exist
              ERROR ("lua/exec", "\"" + absolute_path + "\", "
                     + strerror (errno));
            }

          if (script_list.count (absolute_path) == 0)
            {
              // New script
              script_list[absolute_path].node_stat = node_stat;
              script_list[absolute_path].seen = true;
              INFO ("lua/exec", "found script: \"" + absolute_path + "\".");
              openDHANA__lua__start_script (absolute_path);
            }
          else
            {
              if (script_list[absolute_path].node_stat.st_mtime
                  != node_stat.st_mtime)
                {
                  // File is changed
                  script_list[absolute_path].node_stat = node_stat;
                  script_list[absolute_path].seen = true;
                  INFO ("lua/exec",
                        "modified script: \"" + absolute_path + "\".");
                  openDHANA__lua__stop_script (absolute_path);
                  openDHANA__lua__start_script (absolute_path);
                }
              else
                {
                  // File is unchanged, but seen
                  script_list[absolute_path].seen = true;
                }
            }
        }
    }
  (void) closedir (dirp);

  // Now check if all files in the list has been seen.
  for (std::map<string, script_list_node>::iterator script =
          script_list.begin (); script != script_list.end ();)
    {
      if (script->second.seen == false)
        {
          // Delete this script
          INFO ("lua/exec", "deleted script: \"" + script->first + "\".");
          openDHANA__lua__stop_script (script->first);
          script_list.erase (script++);
        }

      else
        ++script;
    }

  UNLOCK (script_state);

  INFO ("lua/exec", "all scripts processed.");
  return true;
}

/// The LUA directory has changed, which means that scripts have been added,
/// changed or removed.
/// Called by @openDHANA__config__file_monitor.
///
/// @param path                 the path to the file.
///

void
openDHANA__lua__lua_dir_changed (const string path)
{

  openDHANA__lua__lua_dir_read (path);
}

/// Add a C function that can be called from a Lua script.
///
/// @param function             the functions Lua name.
/// @param callback             the C function.
///

void
openDHANA__lua__add_external_function (const string& function,
                                       lua_callback callback)
{

  openDHANA__lua__lua_functions[function] = callback;
}

/// A Lua function to let a script log to the openDHANA log file.
///

int
openDHANA__lua_function__log_message (lua_State *L)
{
  int argc = lua_gettop (L);

  if (argc != 1)
    {
      WARNING ("lua/function",
               "Lua 'openDHANA_log_message' needs exactly 1 parameter.");
      return 0;
    }

  if (!lua_isstring (L, 1))
    {

      WARNING ("lua/function",
               "Lua 'openDAHANA_log_message' expects a string as parameter.");
      return 0;
    }

  string message = lua_tostring (L, 1);

  INFO ("lua/function", "from Lua script: \"" + message + "\".");
  return 0;
}

/// A Lua function to let a script send a message to the MQTT broker. The 
/// internal_topic the Lua script is referring to must be defined in the 
/// mqttmap.
///

int
openDHANA__lua_function__publish (lua_State *L)
{
  int argc = lua_gettop (L);

  if (argc != 2)
    {
      WARNING ("lua/function",
               "Lua 'openDHANA_publish' needs exactly 2 parameters.");
      return 0;
    }

  if (!lua_isstring (L, 1) || !lua_isstring (L, 2))
    {

      WARNING ("lua/function",
               "Lua 'openDAHANA_log_message' expects 2 strings as parameters.");
      return 0;
    }

  string internal_topic = lua_tostring (L, 1);
  string message = lua_tostring (L, 2);

  // Send the value to MQTT
  openDHANA_mqtt__communication__publish (openDHANA_mqtt_publications,
                                          internal_topic,
                                          message);

  INFO ("lua/function", "Lua publish: \"" + internal_topic + "\" = \"" + message + "\".");
  return 0;
}

/// A Lua function to let a script know if we are exiting or not.
///

int
openDHANA__lua_function__exiting (lua_State *L)
{
  int argc = lua_gettop (L);

  if (argc != 0)
    {

      WARNING ("lua/function",
               "Lua 'openDHANA_exiting' does not accept any parameters.");
      return 0;
    }

  lua_pushboolean (L, dhana_mqtt_exiting);
  return 1;
}

/// Add "standard" openDHANA Lua C functions
///

void
openDHANA__lua__add_external_functions ()
{
  // Add generic Lua functions
  openDHANA__lua__add_external_function ("openDHANA_publish",
                                         &openDHANA__lua_function__publish);
  openDHANA__lua__add_external_function ("openDHANA_log_message",
                                         &openDHANA__lua_function__log_message);
  openDHANA__lua__add_external_function ("openDHANA_exiting",
                                         &openDHANA__lua_function__exiting);

#  ifdef  OPEN_DHANA_CUSTOM_LUA_C_FUNCTIONS
  // Add custom Lua scripts
  openDHANA__lua__custom_scripts ();
#  endif

}

#endif // openDHANA__lua__
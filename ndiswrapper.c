/*
 * Ndiswrapper manager, initially written for GeeXboX
 * Copyright (C) 2006-2007 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
 *                         Andrew Calkin <calkina@geexbox.org>
 *
 * Based on the original ndiswrapper-1.21 to 1.47 Perl script
 *  by Pontus Fuchs and Giridhar Pemmasani, 2005-2007
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>      /* toupper tolower */
#include <sys/types.h>  /* size_t */
#include <sys/stat.h>   /* stat */
#include <dirent.h>     /* opendir closedir readdir */
#include <regex.h>      /* regexec regfree regcomp */
#include <string.h>     /* strcat strcpy strcmp strcasecmp strncasecmp strchr strrchr strlen strncpy */

#ifdef _WIN32
#include <io.h>       /* open close read write mkdir rmdir */
#else /* _WIN32 */
#include <unistd.h>   /* open close read write symlink mkdir rmdir */
#endif /* !_WIN32 */


#define LINEBUFFER    512
#define STRBUFFER     256

#define WRAP_PCI_BUS      5
#define WRAP_PCMCIA_BUS   8
#define WRAP_USB_BUS      15

#define CONFDIR     "/etc/ndiswrapper"
#define ICASE       1
#define SCASE       0

/* regexec : must be a multiple of 3 */
#define OVECCOUNT   30

/* patterns */
#define PS1 "([^,]*),([^,]*),([^,]*),([^,]*),(.*)"
#define PS2 "ndi\\\\params\\\\(.+)"
#define PS3 "(.+)\\\\.*"
#define PS4 "PCI\\\\VEN_([0-9A-Za-z]+)&DEV_([0-9A-Za-z]+)&SUBSYS_([0-9A-Za-z]{4})([^[:space:]]{4})"
#define PS5 "PCI\\\\VEN_([0-9A-Za-z]+)&DEV_([0-9A-Za-z]+)"
#define PS6 "USB\\\\VID_([0-9A-Za-z]+)&PID_([0-9A-Za-z]+)"

/* this is required on windows */
#ifndef O_BINARY
#define O_BINARY (0)
#endif /* O_BINARY */

/* Use structure for replace Perl hash */
typedef struct def_section_s {
  char name[STRBUFFER];
  char **data;
  unsigned int datalen;
} def_section_t;

typedef struct def_strver_s {
  char key[STRBUFFER];
  char val[STRBUFFER];
} def_strver_t;

typedef struct def_fixlist_s {
  char n[20];
  char m[20];
} def_fixlist_t;

static inline int
my_mkdir (const char *path)
{
#ifdef _WIN32
  return mkdir (path);
#else /* _WIN32 */
  return mkdir (path, 0777);
#endif /* !_WIN32 */
}

/* global variables */
static unsigned int nb_sections = 0;
static char *confdir = CONFDIR;
static char alt_install_file[STRBUFFER];
static unsigned int alt_install = 0;
static unsigned int nb_driver = 0;
static def_section_t **sections;

static def_strver_t strings[LINEBUFFER];
static def_strver_t version[STRBUFFER];
static def_strver_t fuzzlist[STRBUFFER];
static def_strver_t buslist[STRBUFFER];
static unsigned int nb_strings = 0;
static unsigned int nb_version = 0;
static unsigned int nb_fuzzlist = 0;
static unsigned int nb_buslist = 0;

static def_fixlist_t param_fixlist[5];

static char driver_name[STRBUFFER];
static char instdir[STRBUFFER];
static char classguid[STRBUFFER];
static char sys_files[STRBUFFER] = "";
static int bus;


/*
 * Hashing processing
 * ------------------
 * - getString    : get "strings" value from a key
 * - getVersion   : get "version" value from a key
 * - getFuzzlist  : get "fuzz" value from a key
 * - getBuslist   : get "bus" value from key
 * - getFixlist   : get "fix" value from a defined value (main init)
 * - def_strings  : put a key and value to the strings table
 * - def_version  : put a key and value to the version table
 * - def_fuzzlist : put a key and value to the fuzzlist table
 * - def_buslist  : put a key and value to the buslist table
 *
 */

static char *
getString (char *s)
{
  unsigned int i = 0;

  do
  {
    if (!strcmp (strings[i].key, s))
    {
      strcpy (s, strings[i].val);
      break;
    }
    i++;
  }
  while (i < nb_strings && i < sizeof (strings) / sizeof (strings[0]));
  return s;
}

static char *
getVersion (char *s)
{
  unsigned int i = 0;

  do
  {
    if (!strcmp (version[i].key, s))
    {
      strcpy (s, version[i].val);
      break;
    }
    i++;
  }
  while (i < nb_version && i < sizeof (version) / sizeof (version[0]));
  return s;
}

static char *
getFuzzlist (char *s)
{
  unsigned int i = 0;

  do
  {
    if (!strcmp (fuzzlist[i].key, s))
    {
      strcpy (s, fuzzlist[i].val);
      break;
    }
    i++;
  }
  while (i < nb_fuzzlist && i < sizeof (fuzzlist) / sizeof (fuzzlist[0]));
  return s;
}

static char *
getBuslist (char *s)
{
  unsigned int i = 0;

  do
  {
    if (!strcmp (buslist[i].key, s))
    {
      strcpy (s, buslist[i].val);
      break;
    }
    i++;
  }
  while (i < nb_buslist && i < sizeof (buslist) / sizeof (buslist[0]));
  return s;
}

static char *
getFixlist (char *s)
{
  unsigned int i = 0;

  do
  {
    if (!strcmp (param_fixlist[i].n, s))
    {
      strcpy (s, param_fixlist[i].m);
      break;
    }
    i++;
  }
  while (i < sizeof (param_fixlist) / sizeof (param_fixlist[0]));
  return s;
}

static void
def_strings (const char *key, const char *val)
{
  unsigned int i = 0;

  do
  {
    if (!strcmp (strings[i].key, key))
    {
      strcpy (strings[i].val, val);
      break;
    }
    if (i == nb_strings)
    {
      strcpy (strings[i].key, key);
      strcpy (strings[i].val, val);
      nb_strings++;
      break;
    }
    i++;
  }
  while (i <= nb_strings && i < sizeof (strings) / sizeof (strings[0]));
}

static void
def_version (const char *key, const char *val)
{
  unsigned int i = 0;

  do
  {
    if (!strcmp (version[i].key, key))
    {
      strcpy (version[i].val, val);
      break;
    }
    if (i == nb_version)
    {
      strcpy (version[i].key, key);
      strcpy (version[i].val, val);
      nb_version++;
      break;
    }
    i++;
  }
  while (i <= nb_version && i < sizeof (version) / sizeof (version[0]));
}

static void
def_fuzzlist (const char *key, const char *val)
{
  unsigned int i = 0;

  do
  {
    if (!strcmp (fuzzlist[i].key, key))
    {
      strcpy (fuzzlist[i].val, val);
      break;
    }
    if (i == nb_fuzzlist)
    {
      strcpy (fuzzlist[i].key, key);
      strcpy (fuzzlist[i].val, val);
      nb_fuzzlist++;
      break;
    }
    i++;
  }
  while (i <= nb_fuzzlist && i < sizeof (fuzzlist) / sizeof (fuzzlist[0]));
}

static void
def_buslist (const char *key, const char *val)
{
  unsigned int i = 0;

  do
  {
    if (!strcmp (buslist[i].key, key))
    {
      strcpy (buslist[i].val, val);
      break;
    }
    if (i == nb_buslist)
    {
      strcpy (buslist[i].key, key);
      strcpy (buslist[i].val, val);
      nb_buslist++;
      break;
    }
    i++;
  }
  while (i <= nb_buslist && i < sizeof (buslist) / sizeof (buslist[0]));
}

/*
 * Others
 * ------
 * - regex      : regular expressions
 * - getSection : get a section pointer
 * - unisort    : sort and unify a table
 * - usage      : help
 *
 */

static int
regex (const char *str_request, const char *str_regex,
       char rmatch[][STRBUFFER], int icase)
{
  int err, match, start, end, res = 0;
  unsigned int i;
  char *text = NULL;
  size_t nmatch = 0;
  size_t size;
  regex_t preg;
  regmatch_t *pmatch = NULL;

  err = regcomp (&preg, str_regex,
                 icase ? REG_EXTENDED | REG_ICASE : REG_EXTENDED);
  if (err == 0)
  {
    nmatch = preg.re_nsub;
    pmatch = malloc (sizeof (*pmatch) * OVECCOUNT);
    if (pmatch)
    {
      match = regexec (&preg, str_request, OVECCOUNT, pmatch, 0);
      regfree (&preg);
      if (match == 0)
      {
        for (i = 0; i <= nmatch; i++)
        {
          start = pmatch[i].rm_so;
          end = pmatch[i].rm_eo;
          size = end - start;
          text = malloc (sizeof (*text) * (size + 1));
          if (text)
          {
            strncpy (text, &str_request[start], size);
            text[size] = '\0';
            strcpy (rmatch[i], text);
            free (text);
          }
        }
        res = 1;
      }
      free (pmatch);
    }
  }
  if (!res)
    rmatch[0][0] = '\0';
  return res;
}

static def_section_t *
getSection (const char *needle)
{
  unsigned int i;

  for (i = 0; i < nb_sections; i++)
    if (!strcasecmp (sections[i]->name, needle))
      return sections[i];
  return NULL;
}

static void
unisort (char tab[][STRBUFFER], unsigned int *last)
{
  unsigned int i, j;
  int change = 1;
  char tmp[STRBUFFER];

  while (change)
  {
    change = 0;
    for (i = 0; i < *last - 1; i++)
    {
      for (j = 0; j < i; j++)
        if (!strcmp (tab[i], tab[j]))
        {
          *last = *last - 1;
          strcpy (tab[i], tab[*last]);
          change = 1;
        }

      if (strcmp (tab[i + 1], tab[i]) < 0)
      {
        strcpy (tmp, tab[i]);
        strcpy (tab[i], tab[i + 1]);
        strcpy (tab[i + 1], tmp);
        change = 1;
      }
    }
  }
}

static void
usage (void)
{
  printf ("Usage: ndiswrapper OPTION [-o]\n\n");
  printf ("Manage ndis drivers for ndiswrapper.\n");
  printf ("-i inffile    Install driver described by 'inffile'\n");
  printf ("  Optionally with:\n");
  printf ("  -a          Use alternate output format\n");
/*
  printf ("-d devid driver   Use installed 'driver' for 'devid'\n");
*/
  printf ("-e driver     Remove 'driver'\n");
/*
  printf ("-l            List installed drivers\n");
  printf ("-m            Write configuration for modprobe\n");
  printf ("-da           Write module alias configuration for all devices\n");
  printf ("-di           Write module install configuration for all devices\n");
  printf ("-v            Report version information\n");
  printf ("\n\nwhere 'devid' is either PCIID or USBID of the form XXXX:XXXX\n");
*/
  printf ("\nOptional:\n");
  printf ("-o output_dir   Use alternate install directory 'output_dir'\n");
  printf ("                (default: '/etc/ndiswrapper')\n");
}

/*
 * Strings processing
 * ------------------
 * - uc           : string to upper case
 * - ul           : string to lower case
 * - trim         : remove spaces at the left and the right
 * - stripquotes  : remove quotes
 * - remComment   : remove INF comments
 * - substStr     : substitute a string from the strings table
 * - getKeyVal    : split a line for get the key and the value
 *
 */

static char *
uc (char *data)
{
  int i;

  for (i = 0; data[i] != '\0'; i++)
    data[i] = toupper (data[i]);
  return data;
}

static char *
lc (char *data)
{
  int i;

  for (i = 0; data[i] != '\0'; i++)
    data[i] = tolower (data[i]);
  return data;
}

static char *
trim (char *s)
{
  char *ptr, *copy;
  copy = s;

  ptr = strchr (s, '\0');
  while (ptr > s && (*(ptr-1) == ' '
         || *(ptr-1) == '\t'
         || *(ptr-1) == '\r'
         || *(ptr-1) == '\n'))
    ptr--;

  *ptr = '\0';
  ptr = s;
  while (*ptr == ' ' || *ptr == '\t')
    ptr++;

  do
  {
    *(copy++) = *ptr;
  }
  while (*(ptr++));

  return s;
}

static char *
stripquotes (char *s)
{
  char *start, *end, *copy;

  start = strchr (s, '"');
  if (!start)
    return s;

  end = strchr (start + 1, '"');
  if (!end)
    return s;

  *end = '\0';
  copy = strdup (start + 1);
  strcpy (s, copy);
  free (copy);
  return s;
}

static char *
remComment (char *s)
{
  char *comment;
  comment = strchr (s, ';');
  if (comment)
    *comment = '\0';
  return s;
}

static char *
substStr (char *s)
{
  char *lbracket, *rbracket;

  lbracket = strchr (s,'%');
  rbracket = strrchr (s,'%');
  if (lbracket && rbracket
      && lbracket != rbracket && s[rbracket-lbracket+1] == '\0')
  {
    strncpy (s, lbracket + 1, rbracket - lbracket - 1);
    s[rbracket - lbracket - 1] = '\0';
    getString (s);
  }
  return s;
}

static void
getKeyVal (const char *line, char tmp[2][STRBUFFER])
{
  char *ptr;
  ptr = strchr (line, '=');
  if (ptr)
  {
    strncpy (tmp[0], line, ptr - line);
    tmp[0][ptr - line] = '\0';
    strcpy (tmp[1], ptr + 1);
    trim (tmp[0]);
    trim (tmp[1]);
  }
  else
  {
    tmp[0][0] = '\0';
    tmp[1][0] = '\0';
  }
}

/*
 * Files processing
 * ----------------
 * - finddir      : depend of copy_file
 * - findfile     : depend of copy_file
 * - copy         : copy file processing
 * - copy_file    : search the real name of the file
 * - copyfiles    : search files for the copy
 * - file_exists  : test if a file exists
 * - rmtree       : remove a dir
 *
 */

static int
finddir (char *file)
{
  unsigned int i = 0;
  int res = -1;
  char sp[2][STRBUFFER], *ptr1, *ptr2;
  def_section_t *sourcedisksfiles = NULL;

  sourcedisksfiles = getSection ("sourcedisksfiles");
  if (!sourcedisksfiles)
  {
    file[0] = '\0';
    return -1;
  }

  for (i = 0; i < sourcedisksfiles->datalen; i++)
  {
    ptr1 = sourcedisksfiles->data[i];
    ptr2 = strchr (sourcedisksfiles->data[i], '=');
    if (!ptr2)
      continue;

    strncpy (sp[0], ptr1, ptr2 - ptr1);
    sp[0][ptr2 - ptr1] = '\0';
    trim (sp[0]);
    ptr2 = strrchr (ptr1, ',');
    if (!ptr2)
      continue;

    strcpy (sp[1], ptr2 + 1);
    trim (sp[1]);
    if (res == -1
        && sp[0][0] != '\0'
        && sp[1][0] != '\0'
        && !strcasecmp (sp[0], file))
    {
      strcpy (file, sp[1]);
      res = 1;
    }
  }
  if (res == -1)
    file[0] = '\0';
  return res;
}

static int
findfile (const char *dir, char *file)
{
  char path[STRBUFFER];
  DIR *d;
  struct dirent *dp;

  snprintf (path, sizeof (path), "%s/%s", instdir, dir);
  if (!(d = opendir (path)))
  {
    printf ("Unable to open %s\n", instdir);
    file[0] = '\0';
    return -1;
  }

  while ((dp = readdir(d)))
    if (!strcasecmp (file, dp->d_name))
    {
      closedir (d);
      strcpy (file, dp->d_name);
      return 1;
    }

  closedir (d);
  file[0] = '\0';
  return -1;
}

static int
copy (const char *file_src, const char *file_dst, int mod)
{
  int infile = 0;
  int outfile = 1;
  int nbytes;
  char rwbuf[1024];

  if ((infile = open (file_src, O_RDONLY | O_BINARY)) == -1)
  {
    printf ("Unable to open %s file read-only!\n", file_src);
    return -1;
  }

  if ((outfile =
       open (file_dst, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, mod)) == -1)
  {
    printf ("Unable to open %s file for create/write/appending!\n", file_dst);
    return -1;
  }

  while ((nbytes = read (infile, rwbuf, 1024)) > 0)
    write (outfile, rwbuf, nbytes);

  close (infile);
  close (outfile);
  return 1;
}

static void
copy_file (char *file)
{
  int nocopy = 0;
  char *ptr;
  char newname[STRBUFFER];
  char src[STRBUFFER], dst[STRBUFFER];
  char dir[STRBUFFER];
  char realname[STRBUFFER];

  ptr = file;
  if (file[0] == ';')
    while (*ptr != '\0')
    {
      *ptr = *(ptr + 1);
      ptr++;
    }

  trim (remComment (file));

  strcpy (dir, file);
  finddir (dir);
  if (dir[0] != '\0')
    findfile ("", dir);

  strcpy (realname, file);
  findfile (dir, realname);

  if (realname[0] != '\0')
  {
    strcpy (newname, realname);
    if (dir[0] != '\0')
      snprintf (realname, sizeof (realname), "%s/%s", dir, newname);
    lc (newname);
    if (!nocopy)
    {
      snprintf (src, sizeof (src), "%s/%s", instdir, realname);
      snprintf (dst, sizeof (dst), "%s/%s/%s", confdir, driver_name, newname);
      copy (src, dst, 0644);
    }
  }
}

static int
copyfiles (const char *copy_name)
{
  unsigned int i = 0, k, l;
  char *copy_ptr;
  char **files;
  char *tmp;
  def_section_t *copy = NULL;

  if (copy_name[0] == '@')
  {
    copy_ptr = strdup (copy_name+1);
    copy_file (copy_ptr);
    if (!strstr (sys_files, lc (copy_ptr)) && strstr (copy_ptr, ".sys"))
      snprintf (sys_files, sizeof (sys_files), "%s%s ", sys_files, copy_ptr);
    free (copy_ptr);
    return 1;
  }

  copy = getSection (copy_name);
  if (!copy)
  {
    printf ("Parse error in inf. Unable to find section %s\n", copy_name);
    return -1;
  }

  files = malloc (LINEBUFFER * sizeof (char *));
  memset (files, 0, LINEBUFFER * sizeof (char *));
  for (i = 0; i < copy->datalen; i++)
  {
    if (copy->data[i][0] == '[')
      break;

    /* Split */
    k = 0;
    if ((tmp = strtok (copy->data[i], ",")) != NULL)
    {
      files[k] = strdup (tmp);
      while ((tmp = strtok (NULL, ",")) != NULL)
      {
        files[++k] = strdup (tmp);
        *(tmp - 1) = ',';
      }
    }
    else
      files[k] = strdup (copy->data[i]);

    l = k + 1;
    for (k = 0; k < l; k++)
    {
      trim (files[k]);
      if (strlen (files[k]) > 0)
      {
        copy_file (files[k]);
        if (!strstr (sys_files, lc (files[k])) && strstr (files[k], ".sys"))
          snprintf (sys_files, sizeof (sys_files),
                    "%s%s ", sys_files, files[k]);
      }
      free (files[k]);
    }
  }
  free (files);
  return 0;
}

static int
file_exists (const char *file)
{
  struct stat st;

  if (stat (file, &st) < 0)
    return 0;
  return 1;
}

static int
rmtree (const char *dir)
{
  char file[STRBUFFER];
  DIR *d;
  struct dirent *dp;

  d = opendir (dir);
  while ((dp = readdir (d)))
  {
    if (strcmp (dp->d_name, ".") != 0 || strcmp (dp->d_name, "..") != 0)
    {
      snprintf (file, sizeof (file), "%s/%s", dir, dp->d_name);
      unlink (file);
    }
  }
  if (rmdir (dir) == 0)
    return 1;
  return 0;
}

/*
 * Parsers
 * -------
 * - addPCIFuzzEntry  : add device in the fuzzlist
 * - addReg           : add registry to the conf
 * - parseDevice      : parse device informations and write conf file
 * - parseID          : parse device ID informations (PCI and USB)
 * - parseVendor      : parse vendor informations
 * - parseMfr         : parse manufacturer informations
 * - parseVersion     : parse version informations
 *
 */

static void
addPCIFuzzEntry (const char *vendor, const char *device,
                 const char *subvendor, const char *subdevice,
                 const char *bt)
{
  char s[STRBUFFER], s2[STRBUFFER];
  char fuzz[STRBUFFER];

  snprintf (s, sizeof (s), "%s:%s", vendor, device);

  strcpy (fuzz, s);
  getFuzzlist (fuzz);
  if (subvendor[0] == '\0' || !strcmp (fuzz, s))
  {
    strcpy (s2, s);
    if (subvendor[0] != '\0')
      snprintf (s2, sizeof (s2), "%s:%s:%s", s, subdevice, subvendor);
    def_fuzzlist (s, s2);
    def_buslist (s, bt);
  }
}

static int
addReg (const char *reg_name, char param_tab[][STRBUFFER], unsigned int *k)
{
  unsigned int i = 0;
  int found = 0, gotParam = 0, driver_desc = 0;
  char ps[6][STRBUFFER];
  char param[STRBUFFER], param_t[STRBUFFER];
  char type[STRBUFFER], val[STRBUFFER], s[STRBUFFER];
  char p1[STRBUFFER], p2[STRBUFFER], p3[STRBUFFER], p4[STRBUFFER];
  char fixlist[STRBUFFER], sOld[STRBUFFER];
  def_section_t *reg = NULL;

  reg = getSection (reg_name);
  if (reg == NULL)
  {
    printf ("Parse error in inf. Unable to find section %s\n", reg_name);
    return -1;
  }

  for (i = 0; i < reg->datalen; i++)
  {
    if (strcmp (reg->data[i], "") != 0)
    {
      regex (reg->data[i], PS1, ps, SCASE);
      strcpy (p1, ps[2]);
      strcpy (p2, ps[3]);
      strcpy (p3, ps[4]);
      strcpy (p4, ps[5]);
      substStr (stripquotes (trim (p1)));
      substStr (stripquotes (trim (p2)));
      substStr (stripquotes (trim (p3)));
      substStr (stripquotes (trim (p4)));
      if (p1[0] != '\0')
      {
        if (regex (p1, PS2, ps, ICASE))
        {
          strcpy (param_t, ps[1]);
          regex (param_t, PS3, ps, SCASE);
          strcpy (param_t, ps[1]);
          if (strcmp (param, param_t) != 0)
          {
            found = 0;
            strcpy (param, param_t);
            type[0] = '\0';
            val[0] = '\0';
          }
          if (!strcasecmp (p2, "type"))
          {
            found++;
            strcpy (type, p4);
          }
          else if (!strcasecmp (p2, "default"))
          {
            found++;
            strcpy (val, p4);
          }

          if (found == 2)
            gotParam = 1;
        }
        else if (strncasecmp (p1, "ndi", 3) || !strcasecmp (p1, "ndi"))
        {
          strcpy (param, p2);
          strcpy (val, p4);
          gotParam = 1;
        }
      }
      else
      {
        strcpy (param, p2);
        strcpy (val, p4);
        gotParam = 1;
      }

      if (gotParam && strcmp (param, "") != 0)
      {
        if (!strcmp (param, "DriverDesc"))
          driver_desc = 1;
        snprintf (s, sizeof (s), "%s|%s", param, val);
        strcpy (fixlist, s);
        getFixlist (fixlist);
        if (strcmp (fixlist, s) != 0)
        {
          strcpy (sOld, s);
          strcpy (s, fixlist);
          printf ("Forcing parameter %s to %s\n", sOld, s);
        }
        strcpy (param_tab[*k], s);
        *k = *k + 1;
        param[0] = '\0';
        gotParam = 0;
      }
    }
  }

  if (!driver_desc)
  {
    strcpy (param_tab[*k], "DriverDesc|NDIS Network Adapter");
    *k = *k + 1;
  }

  return 1;
}

static int
parseDevice (const char *flavour, const char *device_sect,
             const char *device, const char *vendor,
             const char *subvendor, const char *subdevice)
{
  unsigned int i = 0, j, k, push = 0, par_k = 0;
  char **lines;
  char **copy_files;
  char param_tab[LINEBUFFER][STRBUFFER];
  char keyval[2][STRBUFFER];
  char sec[STRBUFFER], addreg[STRBUFFER];
  char filename[STRBUFFER], bt[STRBUFFER], file[STRBUFFER];
  char bustype[STRBUFFER], alt_filename[STRBUFFER];
  char ver[STRBUFFER], provider[STRBUFFER], providerstring[STRBUFFER];
  char *tmp;
  def_section_t *dev = NULL;
  FILE *f;

  /*
   * for RNDIS INF file (for USR5420), vendor section names device
   * section as RNDIS.NT.5.1, but copyfiles section is RNDIS.NT, so
   * first strip flavour from device_sect and then look for matching
   * section
   */
  if (!strcmp (device_sect, "RNDIS.NT.5.1"))
    dev = getSection ("RNDIS.NT");

  if (!dev)
  {
    snprintf (sec, sizeof (sec), "%s.%s", device_sect, flavour);
    dev = getSection (sec);
  }

  if (!dev)
  {
    snprintf (sec, sizeof (sec), "%s.NT", device_sect);
    dev = getSection (sec);
  }

  if (!dev)
  {
    snprintf (sec, sizeof (sec), "%s.NTx86", device_sect);
    dev = getSection (sec);
  }

  if (!dev)
    dev = getSection (device_sect);

  if (!dev)
  {
    printf ("no dev %s %s\n", device_sect, flavour);
    return -1;
  }

  copy_files = malloc (LINEBUFFER * sizeof (char *));
  memset (copy_files, 0, LINEBUFFER * sizeof (char *));

  for (i = 0; i < dev->datalen; i++)
  {
    getKeyVal (dev->data[i], keyval);
    if (keyval[0][0] != '\0')
    {
      if (!strcasecmp (keyval[0], "addreg"))
        strcpy (addreg, keyval[1]);
      else if (!strcasecmp (keyval[0], "copyfiles"))
      {
        copy_files[push] = strdup (keyval[1]);
        push++;
      }
      else if (!strcasecmp (keyval[0], "BusType"))
        def_strings (keyval[0], keyval[1]);
    }
  }

  snprintf (filename, sizeof (filename), "%s:%s", device, vendor);
  if (subvendor[0] != '\0')
  {
    strcat (filename, ":");
    strcat (filename, subdevice);
    strcat (filename, ":");
    strcat (filename, subvendor);
  }

  snprintf (bt, sizeof (bt), "%X", bus);
  strcat (filename, ".");
  strcat (filename, bt);
  strcat (filename, ".conf");
  if (bus == WRAP_PCI_BUS || bus == WRAP_PCMCIA_BUS)
    addPCIFuzzEntry (device, vendor, subvendor, subdevice, bt);

  if (alt_install)
  {
    snprintf (file, sizeof (file), "driver%d", nb_driver);
    snprintf (alt_filename, sizeof (file), "%s", filename);
    if ((f = fopen (alt_install_file, "ab")))
    {
      fprintf (f, "%s %s\n", file, alt_filename);
      fclose (f);
    }
    else
    {
      printf ("Unable to create file %s\n", alt_install_file);
      return -1;
    }
    snprintf (file, sizeof (file),
              "%s/%s/driver%d", confdir, driver_name, nb_driver++);
  }
  else
    snprintf (file, sizeof (file), "%s/%s/%s", confdir, driver_name, filename);

  if (!(f = fopen (file, "wb")))
  {
    printf ("Unable to create file %s\n", filename);
    return -1;
  }

  /* Split */
  i = 0;
  lines = malloc (LINEBUFFER * sizeof (char *));
  memset (lines, 0, LINEBUFFER * sizeof (char *));
  if ((tmp = strtok (addreg, ",")) != NULL)
  {
    lines[i] = strdup (tmp);
    while ((tmp = strtok (NULL, ",")) != NULL)
      lines[++i] = strdup (tmp);
  }
  else
    lines[i] = strdup (addreg);

  j = i + 1;
  for (i = 0; i < j; i++)
  {
    trim (lines[i]);
    addReg (lines[i], param_tab, &par_k);
    free (lines[i]);
  }

  for (k = 0; k < push; k++)
  {
    /* Split */
    i = 0;
    if ((tmp = strtok (copy_files[k], ",")) != NULL)
    {
      lines[i] = strdup (tmp);
      while ((tmp = strtok (NULL, ",")) != NULL)
        lines[++i] = strdup (tmp);
    }
    else
      lines[i] = strdup (copy_files[k]);
    free (copy_files[k]);

    j = i + 1;
    for (i = 0; i < j; i++)
    {
      trim (lines[i]);
      copyfiles (lines[i]);
      free (lines[i]);
    }
  }

  fprintf (f, "sys_files|%s\n", sys_files);
  strcpy (ver, "DriverVer");
  getVersion (ver);
  strcpy (provider, "Provider");
  getVersion (provider);
  strcpy (providerstring, provider);
  stripquotes (substStr (trim (providerstring)));

  fputs ("NdisVersion|0x50001\n", f);
  fputs ("Environment|1\n", f);
  fprintf (f, "class_guid|%s\n", classguid);
  fprintf (f, "driver_version|%s,%s\n", providerstring, ver);
  strcpy (bustype, "BusType");
  getString (bustype);
  fprintf (f, "BusType|%s\n", bustype);
  fputs ("SlotNumber|01\n", f);
  fputs ("NetCfgInstanceId|{28022A01-1234-5678-ABCDE-123813291A00}\n", f);
  fputs ("\n", f);

  /* sort and unify before writing */
  unisort (param_tab, &par_k);
  for (i = 0; i < par_k; i++)
    fprintf (f, "%s\n", param_tab[i]);

  fclose (f);
  free (copy_files);
  free (lines);
  return 1;
}

static int
parseID (const char *id, int *bt, char *vendor,
         char *device, char *subvendor, char *subdevice)
{
  char *ptr1, *ptr2, *ptr3;
  ptr1 = strstr (id, "PCI\\VEN_");
  ptr2 = strstr (id, "&DEV_");
  ptr3 = strstr (id, "&SUBSYS_");
  if (ptr1 && ptr2)
  {
    *bt = WRAP_PCI_BUS;
    strncpy (vendor, ptr1 + strlen ("PCI\\VEN_"), 4);
    vendor[4] = '\0';
    strncpy (device, ptr2 + strlen ("&DEV_"), 4);
    device[4] = '\0';
    if (ptr3)
    {
      strncpy (subdevice, ptr3 + strlen ("&SUBSYS_"), 4);
      subdevice[4] = '\0';
      strncpy (subvendor, ptr3 + strlen ("&SUBSYS_")+4, 4);
      subvendor[4] = '\0';
    }
    else
    {
      subvendor[0] = '\0';
      subdevice[0] = '\0';
    }
  }
  else
  {
    ptr1 = strstr (id, "USB\\VID_");
    ptr2 = strstr (id, "&PID_");
    *bt = WRAP_USB_BUS;
    strncpy (vendor, ptr1 + strlen ("USB\\VID_"), 4);
    vendor[4] = '\0';
    strncpy (device, ptr2 + strlen ("&PID_"), 4);
    device[4] = '\0';
    subvendor[0] = '\0';
    subdevice[0] = '\0';
  }
  return 1;
}

static int
parseVendor (const char *flavour, const char *vendor_name)
{
  unsigned int i = 0;
  int bt;
  char keyval[2][STRBUFFER];
  char section[STRBUFFER], id[STRBUFFER];
  char vendor[5], device[5];
  char subvendor[5], subdevice[5];
  def_section_t *vend = NULL;

  vend = getSection (vendor_name);
  if (vend == NULL)
  {
    printf ("Could not find section for %s in inf file!\n", vendor_name);
    return -1;
  }

  for (i = 0; i < vend->datalen; i++)
  {
    getKeyVal (vend->data[i], keyval);
    if (keyval[1][0] != '\0')
    {
      strcpy (section, strtok (keyval[1], ","));
      strcpy (id, strtok (NULL, ","));
      trim (section);
      uc (substStr (trim (id)));
      parseID (id, &bt, vendor, device, subvendor, subdevice);
      bus = bt;
      if (vendor[0] != '\0')
        parseDevice (flavour, section, vendor, device, subvendor, subdevice);
    }
  }
  return 0;
}

static int
parseMfr (void)
{
  /*
   * Examples:
   * Vendor
   * Vendor,ME,NT,NT.5.1
   * Vendor.NTx86
   */
  unsigned int i = 0, k, l;
  int res = 0;
  char keyval[2][STRBUFFER];
  char **flavours;
  char sp[2][STRBUFFER];
  char ver[STRBUFFER];
  char section[STRBUFFER] = "";
  char flavour[STRBUFFER];
  char *tmp;
  def_section_t *manu = NULL;

  manu = getSection ("manufacturer");
  if (!manu)
  {
    printf ("Could not find section 'manufacturer' in inf file!\n");
    return -1;
  }

  for (i = 0; i < manu->datalen; i++)
  {
    getKeyVal (manu->data[i], keyval);

    strcpy (ver, "Provider");
    getVersion (ver);
    if (!strcmp (keyval[0], ver))
      def_strings (keyval[0], keyval[1]);

    if (keyval[1][0] != '\0')
    {
      flavour[0] = '\0';
      /* Split */
      k = 0;
      flavours = malloc (LINEBUFFER * sizeof (char *));
      memset (flavours, 0, LINEBUFFER * sizeof (char *));
      if ((tmp = strtok (keyval[1], ",")) != NULL)
      {
        flavours[k] = strdup (tmp);
        stripquotes (trim (flavours[k]));
        while ((tmp = strtok (NULL, ",")) != NULL)
        {
          flavours[++k] = strdup (tmp);
          *(tmp - 1) = ',';
          stripquotes (trim (flavours[k]));
        }
      }
      else
      {
        flavours[k] = strdup (keyval[1]);
        stripquotes (trim (flavours[k]));
      }

      if (k == 0)
      {
        /* Vendor */
        strcpy (section, flavours[0]);
        free (flavours[0]);
      }
      else
      {
        l = k + 1;
        for (k = 1; k < l; k++)
        {
          regex (flavours[k],
                 "[[:space:]]*([^[:space:]]+)[[:space:]]*", sp, SCASE);
          if (!strcasecmp (sp[1], "NT.5.1"))
          {
            /* This is the best (XP) */
            snprintf (section, sizeof (section), "%s.%s", flavours[0], sp[1]);
            strcpy (flavour, sp[1]);
          }
          else
          {
            if (!strncasecmp(sp[1], "NT", 2) && section[0] == '\0')
            {
              /* This is the second best (win2k) */
              snprintf (section, sizeof (section), "%s.%s", flavours[0], sp[1]);
              strcpy (flavour, sp[1]);
            }
          }
          free (flavours[k]);
        }
        free (flavours[0]);
      }
      if (!res)
        res = parseVendor (flavour, section);
      free (flavours);
    }
  }
  return res;
}

static int
parseVersion (void)
{
  unsigned int i = 0;
  char keyval[2][STRBUFFER];
  char *ptr1, *ptr2;
  def_section_t *s = NULL;

  s = getSection ("version");
  if (!s)
  {
    printf ("Could not find section 'version' in inf file!\n");
    return -1;
  }

  /* Split */
  for (i = 0; i < s->datalen; i++)
  {
    getKeyVal (s->data[i], keyval);
    if (!strcmp (keyval[0], "Provider"))
    {
      stripquotes (keyval[1]);
      def_version (keyval[0], keyval[1]);
    }

    if (!strcmp (keyval[0], "DriverVer"))
    {
      stripquotes (keyval[1]);
      def_version (keyval[0], keyval[1]);
    }

    if (!strcmp (keyval[0], "ClassGUID"))
    {
      ptr1 = strchr (keyval[1], '{');
      ptr2 = strchr (keyval[1], '}');
      strncpy (classguid, ptr1 + 1, ptr2 - ptr1 - 1);
      classguid[ptr2-ptr1-1] = '\0';
      lc (classguid);
    }
  }
  parseMfr ();
  return 1;
}

/*
 * INF installation
 * ----------------
 * - initStrings    : init "strings" section
 * - loadinf        : load INF in memory
 * - isInstalled    : test if the driver is already installed
 * - processPCIFuzz : create symbolic link
 * - install        : install driver described by INF
 *
 */

static int
initStrings (void)
{
  unsigned int i = 0;
  char keyval[2][STRBUFFER];
  char ps[STRBUFFER], *ptr1, *ptr2;
  def_section_t *s = NULL;

  s = getSection ("strings");
  if (s == NULL)
  {
    printf ("Could not find section 'strings' in inf file!\n");
    return -1;
  }

  for (i = 0; i < s->datalen; i++)
  {
    getKeyVal (s->data[i], keyval);
    if (keyval[1][0] != '\0')
    {
      ptr1 = strchr (keyval[1], '"');
      ptr2 = strchr (ptr1+1, '"');
      strncpy (ps, ptr1 + 1, ptr2 - ptr1 - 1);
      ps[ptr2-ptr1-1] = '\0';
      def_strings (keyval[0], ps);
    }
  }
  return 1;
}

static int
loadinf (const char *filename)
{
  char s[STRBUFFER];
  char *lbracket, *rbracket;
  FILE *f;
  int res = 0;

  if (!sections)
  {
    printf ("Memory for 'sections' not allocated!\n");
    return res;
  }
  if ((f = fopen (filename, "rb")) == NULL)
  {
    printf ("Could not open %s for reading!\n", filename);
    return res;
  }

  while (fgets (s, sizeof (s), f))
  {
    res = 1;
    if (!nb_sections)
    {
      nb_sections++;
      sections[nb_sections-1] = malloc (sizeof (def_section_t));
      strcpy (sections[nb_sections-1]->name, "none");
      sections[nb_sections-1]->data = malloc (LINEBUFFER * sizeof (char *));
      memset (sections[nb_sections-1]->data, 0, LINEBUFFER * sizeof (char *));
      sections[nb_sections-1]->datalen = 0;
    }
    lbracket = strchr (s,'[');
    rbracket = strchr (s,']');
    if (lbracket && rbracket)
    {
      nb_sections++;
      sections[nb_sections - 1] = malloc (sizeof (def_section_t));
      strncpy (sections[nb_sections - 1]->name,
               lbracket + 1, rbracket - lbracket - 1);
      sections[nb_sections - 1]->name[rbracket - lbracket - 1] = '\0';
      sections[nb_sections - 1]->data = malloc (LINEBUFFER * sizeof (char *));
      memset (sections[nb_sections - 1]->data, 0, LINEBUFFER * sizeof (char *));
      sections[nb_sections - 1]->datalen = 0;
    }
    else
    {
      trim (remComment (s));
      if (strlen (s) > 0)
        sections[nb_sections - 1]->data[sections[nb_sections - 1]->datalen++] =
          strdup (s);
    }
  }
  fclose (f);
  return res;
}

static int
isInstalled (const char *name)
{
  int installed = 0;
  char f_path[STRBUFFER];
  DIR *d;
  struct dirent *dp;
  struct stat st;

  stat (confdir, &st);
  if (!S_ISDIR (st.st_mode))
    return 0;

  d = opendir (confdir);
  while ((dp = readdir (d)))
  {
    snprintf (f_path, sizeof (f_path), "%s/%s", confdir, dp->d_name);
    stat (f_path, &st);
    if (S_ISDIR (st.st_mode) && !strcmp (name, dp->d_name))
    {
      installed = 1;
      break;
    }
  }
  closedir (d);
  return installed;
}

static int
processPCIFuzz (void)
{
  unsigned int i;
  int ret = 1;
  char bl[STRBUFFER];
  char src[STRBUFFER], dst[STRBUFFER];
  FILE *f;

  for (i = 0; i < nb_fuzzlist; i++)
  {
    if (strcmp (fuzzlist[i].key, fuzzlist[i].val) != 0)
    {
      strcpy (bl, fuzzlist[i].key);
      getBuslist (bl);

      if (alt_install)
      {
        /* source file */
        snprintf (src, sizeof (src), "%s.%s.conf", fuzzlist[i].val, bl);

        /* destination link */
        snprintf (dst, sizeof (dst), "%s.%s.conf", fuzzlist[i].key, bl);
        f = fopen (alt_install_file, "ab");
        if (f)
        {
          fprintf (f, "%s %s\n", src, dst);
          fclose (f);
          return 1;
        }
        else
        {
          printf ("Failed to open %s file!\n", alt_install_file);
          return 0;
        }
      }
      else
      {
        /* destination link */
        snprintf (dst, sizeof (dst), "%s/%s/%s.%s.conf",
                  confdir, driver_name, fuzzlist[i].key, bl);
#ifdef _WIN32
        /* source file */
        snprintf (src, sizeof (src), "%s/%s/%s.%s.conf",
                  confdir, driver_name, fuzzlist[i].val, bl);
        if (!file_exists (dst) && 1 != copy (src, dst, 0644))
        {
          printf ("Failed to copy file!\n");
          ret = 0;
        }
#else /* _WIN32 */
        /* source file */
        snprintf (src, sizeof (src), "%s.%s.conf", fuzzlist[i].val, bl);
        if (!file_exists (dst) && 0 != symlink (src, dst))
        {
          printf ("Failed to create symlink!\n");
          ret = 0;
        }
#endif /* !_WIN32 */
      }
    }
  }
  return ret;
}

static int
install (const char *inf)
{
  char install_dir[STRBUFFER];
  char dst[STRBUFFER];
  DIR *dir;
  unsigned int i, j;
  char *slash, *ext;
  int retval = -1;

  if (!file_exists (inf))
  {
    printf ("Unable to locate %s\n", inf);
    return retval;
  }

  ext = strstr (inf,".inf");
  if (!ext)
    ext=strstr (inf,".INF");
  slash = strrchr (inf,'/');
  if (!slash || !ext)
  {
    printf ("%s is not a valid inf filename, "
            "please provide in format /path/filename.inf\n", inf);
    return retval;
  }

  strncpy (driver_name, slash + 1, ext - slash - 1);
  driver_name[ext-slash] = '\0';
  lc (driver_name);
  if (alt_install)
    snprintf (alt_install_file, sizeof (alt_install_file),
              "%s/%s/ndiswrapper", confdir, driver_name);
  strncpy (instdir, inf, slash - inf);
  instdir[slash-inf + 1] = '\0';

  if (isInstalled (driver_name))
  {
    printf ("%s is already installed. Use -e to remove it\n", driver_name);
    return retval;
  }

  sections = malloc (STRBUFFER * sizeof (def_section_t *));
  memset (sections, 0, STRBUFFER * sizeof (def_section_t *));
  if (loadinf (inf))
  {
    if ((dir = opendir (confdir)) != NULL)
      closedir (dir);
    else
      my_mkdir (confdir);

    printf ("Installing %s\n", driver_name);
    snprintf (install_dir, sizeof (install_dir), "%s/%s", confdir, driver_name);
    if (my_mkdir (install_dir) == -1)
    {
      printf ("Unable to create directory %s. "
              "Make sure you are running as root\n", install_dir);
      return retval;
    }

    initStrings ();
    parseVersion ();
    snprintf (dst, sizeof (dst), "%s/%s.inf", install_dir, driver_name);
    if (!copy (inf, dst, 0644))
    {
      printf ("couldn't copy %s\n", inf);
      return retval;
    }

    if (processPCIFuzz ())
      retval = 0;
  }
  if (sections)
  {
    for (i = 0; i < nb_sections; i++)
      if (sections[i])
      {
        for (j = 0; j < sections[i]->datalen; j++)
          free (sections[i]->data[j]);
        free (sections[i]->data);
        free (sections[i]);
      }
    free (sections);
  }
  return retval;
}

/*
 * Driver tools
 * ------------
 * - remove : remove a driver
 *
 */

static int
remove_driver (const char *name)
{
  char driver[STRBUFFER];

  if (!isInstalled (name))
  {
    printf
      ("Driver %s is not installed, Use -l to list installed drivers\n", name);
    return -1;
  }

  snprintf (driver, sizeof (driver), "%s/%s", confdir, name);
  if (rmtree (driver))
    return 0;

  printf ("Could not remove driver!\n");
  return -1;
}

/*
 * Main
 * ----
 *
 */

int
main (int argc, char **argv)
{
  /* main initialisation */
  int loc;
  int res = 0;

  /* param_fixlist initialisation */
  strcpy (param_fixlist[0].n, "EnableRadio|0");
  strcpy (param_fixlist[0].m, "EnableRadio|1");
  strcpy (param_fixlist[1].n, "IBSSGMode|0");
  strcpy (param_fixlist[1].m, "IBSSGMode|2");
  strcpy (param_fixlist[2].n, "PrivacyMode|0");
  strcpy (param_fixlist[2].m, "PrivacyMode|2");
  strcpy (param_fixlist[3].n, "MapRegisters|256");
  strcpy (param_fixlist[3].m, "MapRegisters|64");
  strcpy (param_fixlist[4].n, "AdhocGMode|1");
  strcpy (param_fixlist[4].m, "AdhocGMode|0");

  /* arguments */
  if (argc < 2)
  {
    usage ();
    return res;
  }

  /* optional argument */
  for (loc = 4; loc < argc; loc++)
    if (!strcmp (argv[loc-1], "-o"))
      confdir = argv[loc];

  if (!strcmp (argv[1], "-i") && argc < 7 && argc > 2)
  {
    for (loc = 3; loc < argc; loc++)
      if (!strcmp (argv[loc], "-a"))
        alt_install = 1;
    res = install (argv[2]);
  }
/*
  else if (!strcmp (argv[1], "-d") && argc == 4)
    res = devid_driver (argv[2], argv[3]);
*/
  else if (!strcmp (argv[1], "-e") && argc < 6 && argc > 2)
    res = remove_driver (argv[2]);
/*
  else if (!strcmp (argv[1], "-l") && argc == 2)
    res = list ();
  else if (!strcmp (argv[1], "-m") && argc == 2)
    res = modalias ();
  else if (!strcmp (argv[1], "-v") && argc == 2)
  {
    printf ("utils ");
    system ("/sbin/loadndisdriver -v");
    printf ("driver ");
    system ("modinfo ndiswrapper | grep -E '^version|^vermagic'");
    res = 0;
  }
  else if (!strcmp (argv[1], "-da") && argc == 2)
    res = genmoddevconf (0);
  else if (!strcmp (argv[1], "-di") && argc == 2)
    res = genmoddevconf (1);
*/
  else
    usage ();

  return res;
}

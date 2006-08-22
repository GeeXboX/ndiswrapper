/*
 * Ndiswrapper manager, initially written for GeeXboX
 * Copyright (C) 2006 Mathieu Schroeter <mathieu.schroeter@gamesover.ch>
 *
 * Based on the original ndiswrapper-1.21 Perl script
 *  by Pontus Fuchs and Giridhar Pemmasani, 2005
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

#ifndef _NDISWRAPPER_H_
#define _NDISWRAPPER_H_

#define FGETSBUFFER       512
#define STRBUFFER         256
#define DATABUFFER        16384

#define WRAP_PCI_BUS      5
#define WRAP_PCMCIA_BUS   8
#define WRAP_USB_BUS      15

#define CONFDIR           "/etc/ndiswrapper"
#define ICASE             1

/* regexec : must be a multiple of 3 */
#define OVECCOUNT         30

/* Use structure for replace Perl hash */
struct DEF_SECTION {
    char name[STRBUFFER];
    char data[DATABUFFER];
};

struct DEF_STRVER {
    char key[STRBUFFER];
    char val[STRBUFFER];
};

struct DEF_FIXLIST {
    char n[16];
    char m[16];
};

struct delim_s {
    unsigned int loc;
    struct delim_s *next;
};

/* inf installation */
int install(const char *inf);
int isInstalled(const char *name);
int loadinf(const char *filename);
int initStrings(void);
void processPCIFuzz(void);
void addPCIFuzzEntry(const char *vendor, const char *device,
                     const char *subvendor, const char *subdevice,
                     const char *bt);
int addReg(const char *reg_name, char param_tab[][STRBUFFER], int *k);

/* driver tools */
int remove(const char *name);

/* parsers */
int parseVersion(void);
int parseMfr(void);
int parseVendor(const char *flavour, const char *vendor_name);
int parseID(const char *id, int *bt, char *vendor,
            char *device, char *subvendor, char *subdevice);
int parseDevice(const char *flavour, const char *device_sect,
                const char *device, const char *vendor,
                const char *subvendor, const char *subdevice);

/* hashing processing */
void getKeyVal(const char *line, char tmp[2][STRBUFFER]);
char *getString(char *s);
char *getVersion(char *s);
char *getFuzzlist(char *s);
char *getBuslist(char *s);
char *getFixlist(char *s);
void def_strings(const char *key, const char *val);
void def_version(const char *key, const char *val);
void def_fuzzlist(const char *key, const char *val);
void def_buslist(const char *key, const char *val);

/* files processing */
int copyfiles(const char *copy_name);
void copy_file(char *file);
int copy(const char *file_src, const char *file_dst, int mod);
int finddir(char *file);
int findfile(const char *dir, char *file);
int file_exists(const char *file);
int rmtree(const char *dir);

/* strings processing */
char *uc(char *data);
char *lc(char *data);
char *trim(char *s);
char *stripquotes(char *s);
char *remComment(char *s);
char *substStr(char *s);

/* others */
int regex(const char *str_request, const char *str_regex,
          char rmatch[][STRBUFFER], ...);
struct DEF_SECTION *getSection(const char *needle);
void unisort(char tab[][STRBUFFER], int *last);
struct delim_s *storedelim(const char *input, const char delim);
void restoredelim(struct delim_s *delimlist, char *input, const char delim);
void usage(void);

#endif /* _NDISWRAPPER_H_ */

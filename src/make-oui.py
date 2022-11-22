#!/usr/bin/env python
#
# Generates a 'oui-generated.c' for Watt-32 and 'winadinf.c'.
#
from __future__ import print_function

import sys, os, time, re, getopt, codecs

prefixes = dict()
PY3 = (sys.version_info[0] >= 3)

def info (s):
  if PY3:
     os.write (2, bytes(s,"UTF-8"))
  else:
     os.write (2, s)

OUI_URL = "http://standards-oui.ieee.org/oui.txt"
OUI_TXT = 'oui.txt'

OUI_HEAD = r"""/*
 * "Organizationally Unique Identifier" list
 * obtained from %s.
 *
 * This file was generated by %s at
 * %s.
 * DO NOT EDIT!
 */
struct tok {
       unsigned    v;  /* value */
       const char *s;  /* string */
     };

const struct tok oui_values[] = {
"""

OUI_BOTTOM = r"""
};

static size_t num_oui_values = %d;

/* Comparision routine needed by bsearch() routines.
 * Use signed arithmetic.
 *
 * This function MUST be 'cdecl' except for OpenWatcom.
 */
#ifdef __WATCOMC__
  #define CALL_CONV
#else
  #define CALL_CONV  cdecl
#endif

static int CALL_CONV compare (const struct tok *a, const struct tok *b)
{
  return ((long)a->v - (long)b->v);
}

typedef int (*CompareFunc) (const void *, const void *);

const char *oui_val_to_name (unsigned oui)
{
  const struct tok *t = bsearch (&oui, oui_values, num_oui_values, sizeof(*t),
                                 (CompareFunc)compare);
  return (t ? t->s : "Unknown OUI");
}
"""

def write_oui_data (f):
  f.write (OUI_HEAD % (OUI_URL, __file__, time.ctime()))

  for p in sorted(prefixes):
    try:
      f.write ("    { 0x%.2s%.2s%.2s, \"%s\" },\n" % (p[0:], p[3:], p[6:], prefixes[p]))
    except UnicodeEncodeError:
      f.write ("    { 0x%.2s%.2s%.2s, \"%s\" },\n" % (p[0:], p[3:], p[6:], "??"))
      pass

  f.write ("    { 0xFFFFFF, NULL } /* Since XEROX CORPORATION has value 0, use this */")
  f.write (OUI_BOTTOM % len(prefixes));
  info ("Wrote %d OUI records.\n" % len(prefixes))

#
# The progress callback for 'urllib.urlretrieve()'.
#
def url_progress (blocks, block_size, total_size):
  if blocks:
     percent = 100 * (blocks * block_size) / total_size
     kBbyte_so_far = (blocks * block_size) / 1024
     info ("Got %d kBytes (%u%%)\r" % (kBbyte_so_far, percent))

#
# Parse the lines from the download 'oui-generated.txt' file.
# Extract only the first line from each record.
# Grow the 'prefixes[]' dictionary as we walk the list of 'lines'.
#
def parse_oui_txt (lines):
  #
  # The format of the oui-generated.txt looks like:
  #
  #  OUI/MA-L                                                    Organization
  #  company_id                                                  Organization
  #                                                              Address
  #
  #  E0-43-DB   (hex) Shenzhen ViewAt Technology Co.,Ltd.
  #  E043DB     (base 16) Shenzhen ViewAt Technology Co.,Ltd.
  #             9A,Microprofit,6th Gaoxin South Road, High-Tech Industrial Park, Nanshan, Shenzhen, CHINA.
  #             shenzhen  guangdong  518057
  #             CN
  #
  # We want to parse only "(hex)" lines.
  #
  for line in lines:
    line = line.rstrip()
    if not re.match("^[0-9A-F]{2}-[0-9A-F]{2}-[0-9A-F]{2}[\t ]*\(hex\)[\t ]*", line):
       continue
    prefix = line [0:8]
    vendor = line [line.rindex('(hex)')+5:].lstrip()
    prefixes [prefix] = vendor

#
# Check if a local 'fname' exist. Otherwise download it from 'url'.
# Return the contents as a list.
#
def get_local_file_or_download (fname, url):
  if os.path.exists(fname):
     info ("A local %s already exist.\n" % fname)
  else:
     try:
       from urllib import urlretrieve as url_get
     except ImportError:
       from urllib.request import urlretrieve as url_get

     info ("Downloading %s from %s\n" % (fname, url))
     url_get (url, filename = fname, reporthook = url_progress)
     info ("\n")

  # Now read 'fname' and return it as a list.
  #
  data = codecs.open (fname, 'rb', 'UTF-8')
  lines = data.read().splitlines()
  data.close()
  return lines

def usage (err=""):
  info ("%s%s [-h | --help] > file:\n" % (err, os.path.realpath(sys.argv[0])))
  info (r"""  This script generates a 'oui_values[]' array from a local %s file or from one downloaded from here:
  %s""" % (OUI_TXT, OUI_URL))

  sys.exit (0)

##  __main__

try:
  opts, args = getopt.getopt (sys.argv[1:], "?h", ["help"])
except getopt.GetoptError as e:
  usage (e.msg + "\n")

for o, a in opts:
  if o in ["-h", "-?", "--help"]:
     usage()

l = get_local_file_or_download (OUI_TXT, OUI_URL)
parse_oui_txt (l)
write_oui_data (sys.stdout)



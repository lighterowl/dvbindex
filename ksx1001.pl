#!/usr/bin/perl

use warnings;
use strict;

print <<'HDR'
#include <stdint.h>

static const uint16_t ksx1001_to_codepoint[][2] = {
HDR
;

# Input : ftp://unicode.org/Public/MAPPINGS/OBSOLETE/EASTASIA/KSC/KSX1001.TXT
while(<STDIN>) {
  next if /^#/;
  my @pair = split /\s+/;
  print "{ $pair[0] , $pair[1] },\n";
}

print "};\n";
exit 0;

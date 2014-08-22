BEGIN {
  print "/*";
  print "    This file is part of telegram-client.";
  print "";
  print "    Telegram-client is free software: you can redistribute it and/or modify";
  print "    it under the terms of the GNU General Public License as published by";
  print "    the Free Software Foundation, either version 2 of the License, or";
  print "    (at your option) any later version.";
  print "";
  print "    Telegram-client is distributed in the hope that it will be useful,";
  print "    but WITHOUT ANY WARRANTY; without even the implied warranty of";
  print "    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the";
  print "    GNU General Public License for more details.";
  print "";
  print "    You should have received a copy of the GNU General Public License";
  print "    along with this telegram-client.  If not, see <http://www.gnu.org/licenses/>.";
  print "";
  print "    Copyright Vitaly Valtman 2013";
  print "*/";
  print "#ifndef CONSTANTS_H";
  print "#define CONSTANTS_H";
}
//  {
  if (split ($1, a, "#") == 2) {
    gsub (/[[:upper:]]/, "_&", a[1]);
    gsub (/[.]/, "_", a[1]);
    if (a[2] in h) {
      print "ERROR: Duplicate magic " a[2] " for define " a[1] " and " h[a[2]] >"/dev/stderr/"
      exit 1;
    }
    h[a[2]] = a[1];
    print "#define", "CODE_" tolower(a[1]), "0x" a[2];
  }
}
END {
  print "#endif";
}

BEGIN {
  print "#ifndef CONSTANTS_H";
  print "#define CONSTANTS_H";
}
//  {
  if (split ($1, a, "#") == 2) {
    gsub (/[A-Z]/, "_&", a[1]);
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

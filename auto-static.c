int skip_double (void) {
  if (in_ptr + 2 <= in_end) {
    in_ptr += 2;
    return 0;
  } else {
    return -1;
  }
}
int skip_long (void) {
  if (in_ptr + 2 <= in_end) {
    in_ptr += 2;
    return 0;
  } else {
    return -1;
  }
}
int skip_int (void) {
  if (in_ptr + 1 <= in_end) {
    in_ptr += 1;
    return 0;
  } else {
    return -1;
  }
}
int skip_string (void) {
  if (in_ptr == in_end) { return -1; }
  unsigned len = *(unsigned char *)in_ptr;
  if (len == 0xff) { return -1; }
  if (len < 0xfe) {
    int size = len + 1;
    size += (-size) & 3;
    if (in_ptr + (size / 4) <= in_end) {
      in_ptr += (size / 4);
      return 0;
    } else {
      return -1;
    }
  } else {
    len = (*in_ptr) >> 8;
    int size = len + 4;
    size += (-size) & 3;
    if (in_ptr + (size / 4) <= in_end) {
      in_ptr += (size / 4);
      return 0;
    } else {
      return -1;
    }
  }
}

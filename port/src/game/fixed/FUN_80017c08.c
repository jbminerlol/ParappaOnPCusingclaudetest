/* 80017c08: builds the 512-byte memory card save header and copies it to dst.
 * Hand-fixed: Ghidra expressed the copy loops with stack-relative end addresses and turned
 * the third icon frame's source range (0x80010124..0x800101a4) into string literals. */
int FUN_80017c08(dst, title)
undefined1 *dst;
char *title;
{
  undefined1 hdr[0x200];
  int i;

  FUN_80025c44(hdr, 0x200);
  hdr[0] = 'S';
  hdr[1] = 'C';
  hdr[2] = 0x13; /* icon: 3 frames */
  hdr[3] = 1;    /* blocks used */
  for (i = 0; i < 0x40; i++) hdr[4 + i] = 0;
  psyq_strcpy(hdr + 4, title);
  for (i = 0; i < 0x1c; i++) hdr[0x44 + i] = 0;
  for (i = 0; i < 0x80; i++) hdr[0x80 + i] = *(undefined1 *)(0x80010024u + i);
  if (hdr[2] > 0x11) {
    for (i = 0; i < 0x80; i++) hdr[0x100 + i] = *(undefined1 *)(0x800100a4u + i);
  }
  if (hdr[2] > 0x12) {
    for (i = 0; i < 0x80; i++) hdr[0x180 + i] = *(undefined1 *)(0x80010124u + i);
  }
  for (i = 0; i < 0x200; i++) dst[i] = hdr[i];
  return 0;
}

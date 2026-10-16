/* 80025dbc: hand-fixed (Ghidra mangled the lwl/lwr/swl/swr unaligned copy). */
int FUN_80025dbc()
{
  undefined1 buf[8];
  undefined1 *src = (undefined1 *)DAT_80094400;

  buf[0] = src[0];
  buf[1] = src[1];
  buf[2] = src[2];
  buf[3] = 0x5a;
  buf[4] = src[4];
  buf[5] = src[5];
  FUN_80026ef8(buf);
  FUN_80026ecc();
  return 0;
}

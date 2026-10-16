/* 80020568: stage result/rank screen text setup.
 * Hand-fixed: three adjacent int[6] stack arrays (Ghidra: &stack0xffffff88, &stack0xffffffa0,
 * local_48) indexed with a 1-based stage number, so index -1 must stay addressable (slot[0]). */
int FUN_80020568(param_1)
int *param_1;
{
  short sVar1;
  int iVar2;
  short *psVar3;
  short *psVar4;
  undefined4 *puVar5;
  undefined4 *puVar6;
  undefined4 uVar7;
  int *piVar8;
  undefined4 uVar9;
  int *piVar10;
  undefined *puVar11;
  int *piVar12;
  int iVar13;
  int iVar14;
  int iVar15;
  int iVar16;
  int slots[19];
  int *a = slots + 1;  /* stack0xffffff88 */
  int *b = a + 6;      /* stack0xffffffa0 */
  int *c = a + 12;     /* local_48 */
  undefined **local_30;

  iVar14 = 0;
  iVar16 = (int)DAT_800916d8;
  FUN_8001c5a8(&UNK_80053248 + iVar16 * 8, (&PTR_DAT_80053244)[iVar16 * 2], 0);
  FUN_8001c550(0x20, 0x21, &DAT_80051aa0, 0);
  puVar5 = (undefined4 *)a;
  puVar6 = &DAT_80087630;
  piVar10 = &DAT_80087650;
  piVar8 = param_1;
  do {
    sVar1 = *(short *)((int)piVar8 + 0xe);
    *piVar10 = (int)sVar1;
    if (sVar1 == 0) {
      *puVar6 = 0;
      puVar5[6] = 2;
      puVar5[0xc] = 1;
    }
    else {
      *puVar6 = 1;
      puVar5[6] = 0;
      puVar5[0xc] = 0;
    }
    *puVar5 = 0;
    puVar5 = puVar5 + 1;
    puVar6 = puVar6 + 1;
    piVar10 = piVar10 + 1;
    iVar14 = iVar14 + 1;
    piVar8 = (int *)((int)piVar8 + 2);
  } while (iVar14 < 6);
  DAT_8008764c = 0;
  DAT_80087648 = 0;
  if ((short)param_1[2] == 7) {
    uVar7 = 0x40;
    if (param_1[1] == 0) {
      uVar7 = 0x3f;
      uVar9 = 0x62;
      puVar11 = &DAT_800519e0;
    }
    else {
      puVar11 = &DAT_800519f0;
      uVar9 = 100;
    }
    FUN_8001c550(uVar7, uVar9, puVar11, 0);
    FUN_8001c550(0x42, 0x67, &DAT_80051a00, 0);
  }
  else {
    if (*(short *)((int)param_1 + 0x1a) != 0) {
      FUN_8001c550(0x3f, 0x62, &DAT_80051a10, 0);
      FUN_8001c550(0x42, 0x67, &DAT_80051a00, 0);
    }
    if ((short)param_1[2] < 7) {
      b[(short)param_1[2] - 1] = 1;
      *(undefined4 *)(&DAT_8008762c + (short)param_1[2] * 4) = 2;
      a[(short)param_1[2] - 1] = 1;
    }
  }
  iVar15 = 0;
  local_30 = &PTR_DAT_80053050 + iVar16 * 3;
  iVar13 = 0;
  iVar14 = iVar16 * 0x18;
  do {
    piVar12 = (int *)((int)&DAT_80087630 + iVar13);
    FUN_8001c5a8(&UNK_8005308c + iVar16 * 0x18 + iVar13, local_30[*piVar12], 0);
    psVar3 = (short *)(&DAT_80053104 + iVar14);
    psVar4 = (short *)(&DAT_80053106 + iVar14);
    piVar10 = (int *)((undefined1 *)a + iVar13);
    iVar14 = iVar14 + 4;
    FUN_8001c604((int)*psVar3, (int)*psVar4, &DAT_80051bf0,
                 (int)*(short *)((int)&DAT_800531d0 + iVar13),
                 (int)*(short *)((int)&DAT_800531d2 + iVar13),
                 *(undefined4 *)((undefined1 *)b + iVar13), 0);
    FUN_8001c5a8(&DAT_8005317c + iVar13, (&PTR_DAT_800531c4)[*piVar12], 0);
    iVar2 = iVar15 * 2;
    iVar15 = iVar15 + 1;
    FUN_8001c5a8(&DAT_80053194 + iVar13,
                 (&PTR_DAT_80053214)[iVar2 + *(int *)((undefined1 *)c + iVar13)], 0);
    puVar11 = &DAT_800531ac + iVar13;
    piVar8 = (int *)((int)&DAT_80087650 + iVar13);
    iVar13 = iVar13 + 4;
    FUN_8001c5a8(puVar11, (&PTR_DAT_800531f4)[*piVar8 * 2 + *piVar10], 0);
    FUN_8001c5a8(puVar11, (&PTR_DAT_800531e8)[*piVar12], 0);
  } while (iVar15 < 6);
  if ((short)param_1[2] == 8) {
    if (param_1[1] == 0) {
      FUN_8001c5a8(&UNK_8005300c + iVar16 * 0x10, (&PTR_DAT_80053004)[iVar16 * 4], 0);
      puVar11 = PTR_DAT_80052ff4;
    }
    else {
      FUN_8001c5a8(&UNK_8005300c + iVar16 * 0x10, (&PTR_DAT_80053008)[iVar16 * 4], 0);
      puVar11 = PTR_DAT_80052ff8;
    }
    FUN_8001c5a8(&DAT_80052ffc, puVar11, 0);
    puVar11 = PTR_DAT_8006eb10;
    if (*param_1 != 0) goto LAB_80020a00;
  }
  else {
    FUN_8001c5a8(&UNK_8005300c + iVar16 * 0x10, (&PTR_DAT_80053000)[iVar16 * 4], 0);
    FUN_8001c5a8(&DAT_80052ffc, PTR_DAT_80052ff0, 0);
  }
  puVar11 = PTR_DAT_8006eb14;
LAB_80020a00:
  FUN_8001c550(0xe7, 0xb3, puVar11, 1);
  return 0;
}

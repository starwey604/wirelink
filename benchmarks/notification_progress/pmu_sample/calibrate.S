/* SPDX-License-Identifier: Apache-2.0 */
/* Known CALL8 depth and iterative division, only for pre-USB calibration. */
.section .text.pmu_cal_depth,"ax",@progbits
.balign 4
.global pmu_cal_depth
.type pmu_cal_depth,@function
pmu_cal_depth:
  entry a1, 32
  beqz a2, 1f
  addi a10, a2, -1
  call8 pmu_cal_depth
  addi a2, a10, 1
  retw.n
1:
  movi a2, 0
  retw.n
.size pmu_cal_depth, .-pmu_cal_depth

.section .text.pmu_cal_divide,"ax",@progbits
.balign 4
.global pmu_cal_divide
.type pmu_cal_divide,@function
pmu_cal_divide:
  entry a1, 32
  movi a3, -1
  movi a4, 3
1:
  quou a5, a3, a4
  addi a2, a2, -1
  bnez a2, 1b
  mov.n a2, a5
  retw.n
.size pmu_cal_divide, .-pmu_cal_divide

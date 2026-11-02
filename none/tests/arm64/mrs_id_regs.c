/* Linux emulates EL0 reads of the ID registers in Op0=3, Op1=0, CRn=0,
   CRm=2..7, so these reads must not raise SIGILL under Valgrind.  Valgrind
   reports the features as absent, so each read returns 0. */

#include <stdio.h>

#define READ_ID_REG(name, sysreg)                                     \
   do {                                                               \
      unsigned long value;                                            \
      __asm__ volatile("mrs %0, " sysreg : "=r"(value));              \
      printf("%-17s 0x%016lx\n", name, value);                        \
   } while (0)

int main(void)
{
   READ_ID_REG("ID_ISAR0_EL1", "S3_0_C0_C2_0");
   READ_ID_REG("MVFR0_EL1", "S3_0_C0_C3_0");
   READ_ID_REG("ID_AA64PFR2_EL1", "S3_0_C0_C4_2");
   READ_ID_REG("ID_AA64ZFR0_EL1", "S3_0_C0_C4_4");
   READ_ID_REG("ID_AA64SMFR0_EL1", "S3_0_C0_C4_5");
   READ_ID_REG("ID_AA64FPFR0_EL1", "S3_0_C0_C4_7");
   READ_ID_REG("ID_AA64DFR2_EL1", "S3_0_C0_C5_2");
   READ_ID_REG("ID_AA64ISAR2_EL1", "S3_0_C0_C6_2");
   READ_ID_REG("ID_AA64ISAR3_EL1", "S3_0_C0_C6_3");
   READ_ID_REG("ID_AA64MMFR3_EL1", "S3_0_C0_C7_3");
   READ_ID_REG("ID_AA64MMFR4_EL1", "S3_0_C0_C7_4");
   READ_ID_REG("S3_0_C0_C7_7", "S3_0_C0_C7_7");
   return 0;
}

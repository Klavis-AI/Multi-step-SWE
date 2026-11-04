/******************************************************************************************************************/
/*                                                                                                                */
/*                                                                                                                */
/*  Licenses and Notices                                                                                          */
/*                                                                                                                */
/*  1.  Copyright Licenses:                                                                                       */
/*     Trusted Computing Group (TCG) grants to the user of the source code in this specification (the             */
/*     "Source Code") a worldwide, irrevocable, nonexclusive, royalty free, copyright license to                  */
/*     reproduce, create derivative works, distribute, display and perform the Source Code and                    */
/*     derivative works thereof, and to grant others the rights granted herein.                                   */
/*     The TCG grants to the user of the other parts of the specification (other than the Source Code)            */
/*     the rights to reproduce, distribute, display, and perform the specification solely for the purpose         */
/*     of developing products based on such documents.                                                            */
/*                                                                                                                */
/*  2.  Source Code Distribution Conditions:                                                                      */
/*     Redistributions of Source Code must retain the above copyright licenses, this list of conditions           */
/*     and the following disclaimers.                                                                             */
/*     Redistributions in binary form must reproduce the above copyright licenses, this list of                   */
/*     conditions and the following disclaimers in the documentation and/or other materials provided              */
/*     with the distribution.                                                                                     */
/*                                                                                                                */
/*  3.  Disclaimers:                                                                                              */
/*     THE COPYRIGHT LICENSES SET FORTH ABOVE DO NOT REPRESENT ANY FORM OF LICENSE OR                             */
/*     WAIVER, EXPRESS OR IMPLIED, BY ESTOPPEL OR OTHERWISE, WITH RESPECT TO PATENT RIGHTS                        */
/*     HELD BY TCG MEMBERS (OR OTHER THIRD PARTIES) THAT MAY BE NECESSARY TO IMPLEMENT                            */
/*     THIS SPECIFICATION OR OTHERWISE. Contact TCG Administration                                                */
/*     (admin@trustedcomputinggroup.org) for information on specification licensing rights available              */
/*     through TCG membership agreements.                                                                         */
/*     THIS SPECIFICATION IS PROVIDED "AS IS" WITH NO EXPRESS OR IMPLIED WARRANTIES                               */
/*     WHATSOEVER, INCLUDING ANY WARRANTY OF MERCHANTABILITY OR FITNESS FOR A                                     */
/*     PARTICULAR PURPOSE, ACCURACY, COMPLETENESS, OR NONINFRINGEMENT OF INTELLECTUAL                             */
/*     PROPERTY RIGHTS, OR ANY WARRANTY OTHERWISE ARISING OUT OF ANY PROPOSAL,                                    */
/*     SPECIFICATION OR SAMPLE.                                                                                   */
/*     Without limitation, TCG and its members and licensors disclaim all liability, including liability for      */
/*     infringement of any proprietary rights, relating to use of information in this specification and to        */
/*     the implementation of this specification, and TCG disclaims all liability for cost of procurement          */
/*     of substitute goods or services, lost profits, loss of use, loss of data or any incidental,                */
/*     consequential, direct, indirect, or special damages, whether under contract, tort, warranty or             */
/*     otherwise, arising in any way out of use or reliance upon this specification or any information            */
/*     herein.                                                                                                    */
/*     Any marks and brands contained herein are the property of their respective owner.                          */
/*                                                                                                                */
/******************************************************************************************************************/


// C.4.1.     Includes
#define       _CRT_RAND_S
#include       <stdlib.h>
#include       <stdint.h>
#include       <memory.h>
#include       "TpmBuildSwitches.h"
 
#if defined(_Win32) || defined(WIN32)
#else
// Quick and dirty
#include <stdio.h>
int32_t rand_s(uint32_t* rndNum) {
    FILE *fp;
    if (fp = fopen("/dev/urandom", "r")) {
        fread(rndNum, sizeof(char), 1, fp);
        fclose(fp);
        return 0;
    }
    return -1;
}
#endif


// C.4.2.     Local values

// This is the last 32-bits of hardware entropy produced. We have to check to see that two consecutive 32-

// bit values are not the same because (according to FIPS 140-2, annex C

// "If each call to a RNG produces blocks of n bits (where n > 15), the first n-bit block generated after

// power-up, initialization, or reset shall not be used, but shall be saved for comparison with the next n-

// bit block to be generated. Each subsequent generation of an n-bit block shall be compared with the

// previously generated block. The test shall fail if any two compared n-bit blocks are equal."
extern     uint32_t                    lastEntropy;
extern     int                         firstValue;

// C.4.3.     _plat__GetEntropy()

// This function is used to get available hardware entropy. In a hardware implementation of this function,

// there would be no call to the system to get entropy. If the caller does not ask for any entropy, then this is

// a startup indication and firstValue should be reset.

// Return Value                                 Meaning

// <0                                           hardware failure of the entropy generator, this is sticky

// >= 0                                         the returned amount of entropy (bytes)
LIB_EXPORT       int32_t
_plat__GetEntropy(
      unsigned       char                 *entropy,              //    output        buffer
      uint32_t                            amount                 //    amount        requested
)
{
      uint32_t                            rndNum;
      int                          OK     =   1;

      if(amount         ==  0)
      {
              firstValue        =  1;
              return    0;
      }

      //   Only     provide        entropy      32  bits  at  a  time  to  test      the  ability
      //   of   the     caller     to     deal    with  partial  results.
      OK   =   rand_s(&rndNum)            ==    0;
      if(OK)
      {
              if(firstValue)
                    firstValue         =  0;
              else
                    OK  =   (rndNum       !=    lastEntropy);
      }
   if(OK)
   {
      lastEntropy          =  rndNum;
      if(amount      >     sizeof(rndNum))
           amount       =     sizeof(rndNum);
      memcpy(entropy,         &rndNum,  amount);
   }
   return  (OK)   ?  (int32_t)amount    :      -1;
}

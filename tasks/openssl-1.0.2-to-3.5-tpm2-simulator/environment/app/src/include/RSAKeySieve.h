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


// This header file is used to for parameterization of the Sieve and RNG used by the RSA module
#ifndef            RSA_H
#define            RSA_H

// This value is used to set the size of the table that is searched by the prime iterator. This is used during

// the generation of different primes. The smaller tables are used when generating smaller primes.
extern   const        UINT16        primeTableBytes;

// The following define determines how large the prime number difference table will be defined. The value of

// 13 will allocate the maximum size table which allows generation of the first 6542 primes which is all the

// primes less than 2^16.
#define  PRIME_DIFF_TABLE_512_BYTE_PAGES                         13

// This set of macros used the value above to set the table size.
#ifndef  PRIME_DIFF_TABLE_512_BYTE_PAGES
#  define      PRIME_DIFF_TABLE_512_BYTE_PAGES                       4
#endif
#ifdef   PRIME_DIFF_TABLE_512_BYTE_PAGES
#  if    PRIME_DIFF_TABLE_512_BYTE_PAGES              >    12
#        define  PRIME_DIFF_TABLE_BYTES               6542
#  else
#        if     PRIME_DIFF_TABLE_512_BYTE_PAGES                  <=        0
#               define  PRIME_DIFF_TABLE_BYTES                512
#        else
#               define  PRIME_DIFF_TABLE_BYTES                (PRIME_DIFF_TABLE_512_BYTE_PAGES                     *   512)
#        endif
#  endif
#endif
extern   const   BYTE   primeDiffTable    [PRIME_DIFF_TABLE_BYTES];

// This determines the number of bits in the sieve field This must be a power of two.
#define  FIELD_POWER         14       //  This  is    the        only         value  in     this  group    that  should  be
                                      //  changed
#define  FIELD_BITS          (1  <<       FIELD_POWER)
#define  MAX_FIELD_SIZE               ((FIELD_BITS         /     8)     +     1)

// This is the pre-sieved table. It already has the bits for multiples of 3, 5, and 7 cleared.
#define  SEED_VALUES_SIZE                 105
const   extern   BYTE                     seedValues[SEED_VALUES_SIZE];

// This allows determination    of  the  number of bits that are set                    in  a  byte  without  having  to
// count  them

// individually.
const   extern   BYTE                     bitsInByte[256];

// This is the iterator structure for accessing the compressed prime number table. The
// expectation is that

// values will need to be accesses sequentially. This tries to save some data access.
typedef  struct  {
   UINT32           lastPrime;
   UINT32           index;
   UINT32           final;
}  PRIME_ITERATOR;
#ifdef   RSA_INSTRUMENT
#  define      INSTRUMENT_SET(a,      b)  ((a)     =  (b))
#  define      INSTRUMENT_ADD(a,      b)  (a)   =     (a)     +  (b)
#  define      INSTRUMENT_INC(a)          (a)   =     (a)     +  1
extern   UINT32  failedAtIteration[10];
extern   UINT32  MillerRabinTrials;
extern   UINT32  totalFieldsSieved;
extern   UINT32  emptyFieldsSieved;
extern   UINT32  noPrimeFields;
extern   UINT32  primesChecked;
extern   UINT16     lastSievePrime;
#else
#  define      INSTRUMENT_SET(a,      b)
#  define      INSTRUMENT_ADD(a,      b)
#  define      INSTRUMENT_INC(a)
#endif
#ifdef   RSA_DEBUG
extern   UINT16     defaultFieldSize;
#define  NUM_PRIMES              2047
extern   const  __int16          primes[NUM_PRIMES];
#else
#define  defaultFieldSize        MAX_FIELD_SIZE
#endif
#endif

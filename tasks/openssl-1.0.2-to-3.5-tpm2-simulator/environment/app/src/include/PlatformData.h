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


// This file contains the instance data for  the  Platform  module.  It  is  collected  in  this  file  so  that  the
// state  of

// the module is easier to manage.
#ifndef   _PLATFORM_DATA_H_
#define   _PLATFORM_DATA_H_
#include        "TpmBuildSwitches.h"
#include        "Implementation.h"
#include        "bool.h"

// From Cancel.c Cancel flag.    It is initialized as FALSE, which indicate the command is not being canceled
extern   BOOL      s_isCanceled;

// From Clock.c This variable records the time when _plat__ClockReset() is called.                This mechanism allow

// us to subtract the time when TPM is power off from the total time reported by clock() function
extern   unsigned  long     long  s_initClock;
extern   unsigned  int            s_adjustRate;

// From LocalityPlat.c Locality of current command
extern   unsigned  char     s_locality;

// From NVMem.c Choose if the NV memory should be backed by RAM or by file. If this macro is defined,

// then a file is used as NV.  If it is not defined, then RAM is used to back NV memory. Comment out to use

// RAM.
#define   FILE_BACKED_NV
#if  defined    FILE_BACKED_NV
#include     <stdio.h>

// A file to emulate NV storage
extern   FILE*                    s_NVFile;
#endif
extern   unsigned  char           s_NV[NV_MEMORY_SIZE];
extern   BOOL                     s_NvIsAvailable;
extern   BOOL                     s_NV_unrecoverable;
extern   BOOL                     s_NV_recoverable;

// From PPPlat.c Physical presence.  It is initialized to FALSE
extern   BOOL      s_physicalPresence;

// From Power
extern   BOOL           s_powerLost;

// From Entropy.c
extern   uint32_t             lastEntropy;
extern   int                  firstValue;
#endif   //   _PLATFORM_DATA_H_

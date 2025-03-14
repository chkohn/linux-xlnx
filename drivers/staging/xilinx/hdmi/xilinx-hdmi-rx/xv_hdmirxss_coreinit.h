/******************************************************************************
*
 *
 * Copyright (C) 2015, 2016, 2017 Xilinx, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xv_hdmirxss_coreinit.h
* @addtogroup v_hdmirxss
* @{
* @details
*
* This header file contains the hdmi rx subsystem sub-cores
* initialization routines and helper functions.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who    Date     Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00         10/07/15 Initial release.
* 1.1   yh     20/01/16 Added remapper support
* 1.2   MG     03/02/16 Added HDCP support
* 1.3   MH     08/08/16 Updates to optimize out HDCP when excluded.
* </pre>
*
******************************************************************************/
#ifndef XV_HDMIRXSS_COREINIT_H__  /* prevent circular inclusions */
#define XV_HDMIRXSS_COREINIT_H__  /* by using protection macros */

#ifdef __cplusplus
extern "C" {
#endif
//#include <stdio.h>
#include <linux/string.h>
#include "xv_hdmirxss.h"
#include "xv_hdmirx.h"
#ifdef XPAR_XHDCP_NUM_INSTANCES
#include "xtmrctr.h"
#endif
#include "xgpio.h"
#include "xv_axi4s_remap.h"
/************************** Constant Definitions *****************************/

/************************** Function Prototypes ******************************/
int XV_HdmiRxSs_SubcoreInitHdmiRx(XV_HdmiRxSs *HdmiRxSsPtr);
#ifdef XPAR_XHDCP_NUM_INSTANCES
int XV_HdmiRxSs_SubcoreInitHdcpTimer(XV_HdmiRxSs *HdmiRxSsPtr);
int XV_HdmiRxSs_SubcoreInitHdcp14(XV_HdmiRxSs *HdmiRxSsPtr);
#endif
#ifdef XPAR_XHDCP22_RX_NUM_INSTANCES
int XV_HdmiRxSs_SubcoreInitHdcp22(XV_HdmiRxSs *HdmiRxSsPtr);
#endif
int XV_HdmiRxSs_SubcoreInitRemapperReset(XV_HdmiRxSs *HdmiRxSsPtr);
int XV_HdmiRxSs_SubcoreInitRemapper(XV_HdmiRxSs *HdmiRxSsPtr);

#ifdef __cplusplus
}
#endif

#endif
/** @} */

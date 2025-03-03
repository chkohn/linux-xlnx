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
* @file xtmrctr_g.c
* @addtogroup tmrctr_v4_0
* @{
*
* This file contains a configuration table that specifies the configuration of
* timer/counter devices in the system. Each timer/counter device should have
* an entry in this table.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a ecm  08/16/01 First release
* 1.00b jhl  02/21/02 Repartitioned the driver for smaller files
* 1.10b mta  03/21/07 Updated to new coding style
* </pre>
*
******************************************************************************/

/***************************** Include Files *********************************/

#include "xtmrctr.h"
#include "xparameters.h"

/************************** Constant Definitions *****************************/


/**************************** Type Definitions *******************************/


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Function Prototypes ******************************/


/************************** Variable Prototypes ******************************/

/**
 * The timer/counter configuration table, sized by the number of instances
 * defined in xparameters.h.
 */
XTmrCtr_Config XTmrCtr_ConfigTable[] = {
#if defined(XPAR_XTMRCTR_NUM_INSTANCES) && (XPAR_XTMRCTR_NUM_INSTANCES > 0)
	{
		XPAR_TMRCTR_0_DEVICE_ID,
		XPAR_TMRCTR_0_BASEADDR,
		XPAR_TMRCTR_0_CLOCK_FREQ_HZ,
	}
#endif
};
/** @} */

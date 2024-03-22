/*
 * Copyright (C) 2013 Realtek Semiconductor Corp.
 * All Rights Reserved.
 *
 * This program is the proprietary software of Realtek Semiconductor
 * Corporation and/or its licensors, and only be used, duplicated,
 * modified or distributed under the authorized license from Realtek.
 *
 * ANY USE OF THE SOFTWARE OTHER THAN AS AUTHORIZED UNDER
 * THIS LICENSE OR COPYRIGHT LAW IS PROHIBITED.
 *
 * $Revision: 50338 $
 * $Date: 2014-08-19 14:00:41 +0800 (週二, 19 八月 2014) $
 *
 * Purpose : RTL8367C switch high-level API for RTL8367C
 * Feature :
 *
 */

#include <rtl8367c_asicdrv.h>
#include <rtl8367c_asicdrv_phy.h>
#include <rtk_switch.h>
#include <rtk_types.h>
#include <port.h>
#include <linux/module.h>
#include <mdi.h>

EXPORT_SYMBOL(rtk_port_phyMdxStatus_get);
EXPORT_SYMBOL(rtk_port_phyMdx_get);
EXPORT_SYMBOL(rtk_port_phyMdx_set);

rtk_api_ret_t rtk_port_phyMdxStatus_get(rtk_port_t port, rtk_port_phy_mdix_status_t *pStatus)
{
    rtk_uint32 regData;
    rtk_api_ret_t retVal;

    if ((retVal = rtl8367c_getAsicPHYReg(rtk_switch_port_L2P_get(port), 24, &regData))!=RT_ERR_OK)
        return retVal;

    if (regData & (0x0001 << 9))
    {
        if (regData & (0x0001 << 8))
            *pStatus = PHY_STATUS_FORCE_MDI_MODE;
        else
            *pStatus = PHY_STATUS_FORCE_MDIX_MODE;
    }
    else
    {
        if ((retVal = rtl8367c_getAsicPHYReg(rtk_switch_port_L2P_get(port), 26, &regData))!=RT_ERR_OK)
            return retVal;

        if (regData & (0x0001 << 1))
            *pStatus = PHY_STATUS_AUTO_MDI_MODE;
        else
            *pStatus = PHY_STATUS_AUTO_MDIX_MODE;
    }

    return RT_ERR_OK;
}
EXPORT_SYMBOL(rtk_port_phyMdxStatus_get);

rtk_api_ret_t rtk_port_phyMdx_get(rtk_port_t port, rtk_port_phy_mdix_mode_t *pMode)
{
    rtk_uint32 regData;
    rtk_api_ret_t retVal;

    if ((retVal = rtl8367c_getAsicPHYReg(rtk_switch_port_L2P_get(port), 24, &regData))!=RT_ERR_OK)
        return retVal;

    if(regData & (0x0001 << 9))
    {
        if(regData & (0x0001 << 8))
            *pMode = PHY_FORCE_MDI_MODE;
        else
            *pMode = PHY_FORCE_MDIX_MODE;
    }
    else
        *pMode = PHY_AUTO_CROSSOVER_MODE;

    return RT_ERR_OK;
}
EXPORT_SYMBOL(rtk_port_phyMdx_get);

rtk_api_ret_t rtk_port_phyMdx_set(rtk_port_t port, rtk_port_phy_mdix_mode_t mode)
{
    rtk_uint32 regData;
    rtk_api_ret_t retVal;

    /* Check initialization state */
    RTK_CHK_INIT_STATE();

    /* Check Port Valid */
    RTK_CHK_PORT_IS_UTP(port);

    switch (mode)
    {
        case PHY_AUTO_CROSSOVER_MODE:
            if ((retVal = rtl8367c_getAsicPHYReg(rtk_switch_port_L2P_get(port), 24, &regData))!=RT_ERR_OK)
                return retVal;

            regData &= ~(0x0001 << 9);

            if ((retVal = rtl8367c_setAsicPHYReg(rtk_switch_port_L2P_get(port), 24, regData))!=RT_ERR_OK)
                return retVal;
            break;
        case PHY_FORCE_MDI_MODE:
            if ((retVal = rtl8367c_getAsicPHYReg(rtk_switch_port_L2P_get(port), 24, &regData))!=RT_ERR_OK)
                return retVal;

            regData |= (0x0001 << 9);
            regData |= (0x0001 << 8);

            if ((retVal = rtl8367c_setAsicPHYReg(rtk_switch_port_L2P_get(port), 24, regData))!=RT_ERR_OK)
                return retVal;
            break;
        case PHY_FORCE_MDIX_MODE:
            if ((retVal = rtl8367c_getAsicPHYReg(rtk_switch_port_L2P_get(port), 24, &regData))!=RT_ERR_OK)
                return retVal;

            regData |= (0x0001 << 9);
            regData &= ~(0x0001 << 8);

            if ((retVal = rtl8367c_setAsicPHYReg(rtk_switch_port_L2P_get(port), 24, regData))!=RT_ERR_OK)
                return retVal;
            break;
        default:
            return RT_ERR_INPUT;
            break;
    }

    /* Restart N-way */
    if ((retVal = rtl8367c_getAsicPHYReg(rtk_switch_port_L2P_get(port), 0, &regData))!=RT_ERR_OK)
        return retVal;

    regData |= (0x0001 << 9);

    if ((retVal = rtl8367c_setAsicPHYReg(rtk_switch_port_L2P_get(port), 0, regData))!=RT_ERR_OK)
        return retVal;

    return RT_ERR_OK;
}
EXPORT_SYMBOL(rtk_port_phyMdx_set);

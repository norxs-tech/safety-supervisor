/**
 * =====================================================================================
 * @file        Platform_Types.h
 * @brief       AUTOSAR R25-11 Platform Type Definitions for NXP S32G (ARM Cortex-M7)
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11, UN R155
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

#ifndef PLATFORM_TYPES_H
#define PLATFORM_TYPES_H

/*=====================================================================================
 * AUTOSAR Platform-Independent Type Definitions
 *====================================================================================*/

/* Boolean */
#ifndef TRUE
#define TRUE  (1U)
#endif
#ifndef FALSE
#define FALSE (0U)
#endif

typedef unsigned char         boolean;

/* Unsigned integers */
typedef unsigned char         uint8;
typedef unsigned short        uint16;
typedef unsigned int          uint32;
typedef unsigned long long    uint64;

/* Signed integers */
typedef signed char           sint8;
typedef signed short          sint16;
typedef signed int            sint32;
typedef signed long long      sint64;

/* Floating-point */
typedef float                 float32;
typedef double                float64;

/* AUTOSAR Standard Return Type */
typedef uint8                 Std_ReturnType;
#define E_OK                  ((Std_ReturnType)0x00U)
#define E_NOT_OK              ((Std_ReturnType)0x01U)

/* AUTOSAR Version Info Type */
typedef struct
{
    uint16 vendorID;
    uint16 moduleID;
    uint8  sw_major_version;
    uint8  sw_minor_version;
    uint8  sw_patch_version;
} Std_VersionInfoType;

/* Bit-width safety assertions (MISRA C:2023 Dir 4.6 compliance) */
#define PLATFORM_UINT8_MAX    (0xFFU)
#define PLATFORM_UINT16_MAX   (0xFFFFU)
#define PLATFORM_UINT32_MAX   (0xFFFFFFFFUL)
#define PLATFORM_SINT8_MIN    (-128)
#define PLATFORM_SINT8_MAX    (127)
#define PLATFORM_SINT16_MIN   (-32768)
#define PLATFORM_SINT16_MAX   (32767)
#define PLATFORM_SINT32_MIN   (-2147483648)
#define PLATFORM_SINT32_MAX   (2147483647)

/* NULL pointer */
#ifndef NULL_PTR
#define NULL_PTR              ((void *)0)
#endif

/* Inline / static inline compatibility */
#define INLINE                __inline
#define LOCAL_INLINE          static __inline

/*=====================================================================================
 * NXP S32G Specific Memory Attribute Qualifiers
 *====================================================================================*/
#define SHARED_SRAM_ATTR      __attribute__((section(".shared_sram")))
#define SAFETY_RAM_ATTR       __attribute__((section(".safety_ram")))
#define CALIB_ATTR            __attribute__((section(".calib_data")))

/*=====================================================================================
 * ASIL-D Bitwise Redundancy Macro (ISO 26262-6 Table 9)
 *  Usage: ASIL_D_REDUNDANT_VAR(uint32, myVar)
 *         ASIL_D_CHECK(myVar)  -- returns TRUE if consistent
 *====================================================================================*/
#define ASIL_D_REDUNDANT_VAR(type, name) \
    type name;                           \
    type name##_inv

#define ASIL_D_SET(name, value)          \
    do {                                 \
        (name)       = (value);          \
        (name##_inv) = ~(name);          \
    } while (0)

#define ASIL_D_CHECK(name)               \
    (((name) ^ (name##_inv)) == (uint32)(~(uint32)0U))

#endif /* PLATFORM_TYPES_H */

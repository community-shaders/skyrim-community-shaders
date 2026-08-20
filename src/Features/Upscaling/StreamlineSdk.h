#pragma once

#include <Windows.h>
#include <vulkan/vulkan.h>

// Streamline's SDK headers expose the dynamically resolved function types.
#define NV_WINDOWS
#pragma warning(push)
#pragma warning(disable: 4471 5103)
#include <sl.h>
#include <sl_consts.h>
#include <sl_core_api.h>
#include <sl_device_wrappers.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_fsr.h>
#include <sl_fsr_g.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_xess.h>
#pragma warning(pop)

// SDK declarations shared by the session's behavior components. Module
// ownership and resolved exports live in StreamlineSession.

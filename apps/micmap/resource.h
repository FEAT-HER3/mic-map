/**
 * @file resource.h
 * @brief Resource definitions for MicMap system tray application
 */

#pragma once

// Icon resources
#define IDI_MICMAP_ICON         101
#define IDI_MICMAP_ICON_ACTIVE  102
#define IDI_MICMAP_ICON_ERROR   103

// Menu resources
#define IDM_TRAY_MENU           200
#define IDM_SHOW                201
#define IDM_TRAIN               202
#define IDM_EXIT                203

// Custom window messages
#define WM_TRAYICON             (WM_USER + 1)
#define WM_STEAMVR_QUIT         (WM_USER + 2)
#pragma once

// Version.h
// Last Modified: 7/1/2026

/**
 * Engine version identity — the single source of truth for version numbers.
 *
 * KEEP IN SYNC (grep for COSMIC_VERSION when bumping):
 *   - Runtime/CosmicApp.rc          (VERSIONINFO block — Explorer's file properties)
 *   - installer/CosmicSetup.iss     (read at compile time via package_installer.bat)
 *
 * Semantic versioning: MAJOR.MINOR.PATCH. Bump MINOR for new engine features,
 * PATCH for fix-only releases, MAJOR at the first stable/breaking milestone.
 */

#define COSMIC_VERSION_MAJOR 0
#define COSMIC_VERSION_MINOR 9
#define COSMIC_VERSION_PATCH 0
#define COSMIC_VERSION_STRING "0.9.0"

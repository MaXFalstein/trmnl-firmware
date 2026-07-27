#pragma once

/**
 * First-time device registration: /api/setup + setup logo download.
 *
 * Extracted from bl.cpp (architecture-modules step 1). bl_init calls
 * getDeviceCredentials() when API key / friendly ID are missing from NVS.
 *
 * @see docs/architecture-modules.md (when present on branch)
 */

/**
 * Obtain API key and friendly ID from the server; download and show setup image
 * when the setup response indicates success.
 */
void getDeviceCredentials(void);

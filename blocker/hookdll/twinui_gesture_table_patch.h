#pragma once

namespace touchrev {

bool InstallTwinuiGestureTablePatch();
bool UninstallTwinuiGestureTablePatch();
void UpdateRegistryStatus(unsigned long pdbStatus, unsigned long hookStatus);

}  // namespace touchrev


#include "../global_define.h"
#include "patches.h"

#include "titanium.h"

void RegisterAllPatches(EQStreamIdentifier &into) {
	Titanium::Register(into);
}

void ReloadAllPatches() {
	Titanium::Reload();
}

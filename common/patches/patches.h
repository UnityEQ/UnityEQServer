#ifndef PATCHES_H_
#define PATCHES_H_

/*enum {
	Patch_062,
	Patch_Titanium,
	Patch_Live
};*/

class EQStreamIdentifier;
class EQWebStreamIdentifier;

void RegisterAllPatches(EQStreamIdentifier &into);
void RegisterAllPatches(EQWebStreamIdentifier &into);
void ReloadAllPatches();

#endif /*PATCHES_H_*/

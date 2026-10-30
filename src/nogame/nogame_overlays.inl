// Stand-in for RecompiledFuncs/recomp_overlays.inl in a build without the ROM (NMR_NO_GAME, see nogame.cpp):
// a single empty code section and no overlays.
#include "librecomp/sections.h"

static SectionTableEntry section_table[] = {
    { .rom_addr = 0, .ram_addr = 0x80000400, .size = 0, .funcs = nullptr, .num_funcs = 0, .relocs = nullptr, .num_relocs = 0, .index = 0 },
};

static const size_t num_sections = 1;

static int overlay_sections_by_index[] = { -1 };

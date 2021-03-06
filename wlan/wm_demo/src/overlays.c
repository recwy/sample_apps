
/** This is a file auto-generated by ov_utl */
#include <overlays.h>

/** Overlay: ovl */
extern char _ol_ovl_text_start;

extern char _ol_ovl0_bss_start, _ol_ovl0_bss_end,
	__load_start_ol_ovl0, __load_stop_ol_ovl0;
extern char _ol_ovl1_bss_start, _ol_ovl1_bss_end,
	__load_start_ol_ovl1, __load_stop_ol_ovl1;

struct overlay_ranges o_range_ovl[] = {
	{ &__load_start_ol_ovl0, &__load_stop_ol_ovl0,
	  &_ol_ovl0_bss_start, &_ol_ovl0_bss_end},
	{ &__load_start_ol_ovl1, &__load_stop_ol_ovl1,
	  &_ol_ovl1_bss_start, &_ol_ovl1_bss_end},
};
struct overlay ovl = {
	.o_name = "ovl",
	.o_ram_start = &_ol_ovl_text_start,
	.o_no_parts = 2,
	.o_range = o_range_ovl
};

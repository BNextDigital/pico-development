SECTION_PROLOGUE(.pico_fhn_size_probe,,)
{
    KEEP(*(.pico_fhn_size_probe))
} GROUP_LINK_IN(ROMABLE_REGION)

#include "stack_probe.h"

#include "hardware/sync.h"
#include "pico/platform.h"

extern uint32_t __StackBottom, __StackTop;       // core 0 (top of RAM)
extern uint32_t __StackOneBottom, __StackOneTop; // core 1 (SCRATCH_X)

#define PAINT 0xA5C3F00Du

static void bounds(int core, uint32_t **bottom, uint32_t **top) {
    *bottom = core ? &__StackOneBottom : &__StackBottom;
    *top = core ? &__StackOneTop : &__StackTop;
}

void stack_probe_paint(void) {
    uint32_t *bottom, *top;
    bounds(get_core_num(), &bottom, &top);
    uint32_t marker;
    uint32_t *sp = &marker - 32; // stay well clear of this frame
    uint32_t irq = save_and_disable_interrupts();
    for (uint32_t *p = bottom; p < sp; p++) *p = PAINT;
    restore_interrupts(irq);
}

uint32_t stack_probe_size(int core) {
    uint32_t *bottom, *top;
    bounds(core, &bottom, &top);
    return (uint32_t)((uint8_t *)top - (uint8_t *)bottom);
}

uint32_t stack_probe_free_min(int core) {
    uint32_t *bottom, *top;
    bounds(core, &bottom, &top);
    uint32_t *p = bottom;
    while (p < top && *p == PAINT) p++;
    return (uint32_t)((uint8_t *)p - (uint8_t *)bottom);
}

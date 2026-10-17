// Native, dependency-free regression tests; no game files needed.
#include <assert.h>
#include <stdio.h>
#include "../src/hud_policy.h"

int main(void)
{
    // Title, loading, language selection, additional loading screens, pause.
    const uint32_t menus[] = {0, 1, 2, 100, 101, UINT32_MAX};
    for (unsigned i = 0; i < sizeof menus / sizeof menus[0]; i++)
        assert(!bb_hud_gameplay(menus[i], 0));
    assert(bb_hud_gameplay(3, 0));
    assert(bb_hud_gameplay(56, 0));
    assert(bb_hud_gameplay(99, 0));
    assert(!bb_hud_gameplay(4, 1));
    assert(!bb_hud_gameplay(4, 2));

    // Short/long text-only labels must stay centred, at either screen edge.
    assert(!bb_hud_edge(-0.9f, -0.6f, 0.7f, 0.9f, 0));
    assert(!bb_hud_edge(0.3f, 0.9f, 0.7f, 0.9f, 0));
    assert(!bb_hud_edge(-0.9f, 0.9f, -0.9f, -0.7f, 0));
    // Actual HUD icon + number groups still anchor together.
    assert(bb_hud_edge(-0.9f, -0.6f, 0.7f, 0.9f, 1) == 1);
    assert(bb_hud_edge(0.6f, 0.9f, -0.9f, -0.7f, 1) == 2);
    // Central controls and groups spanning the middle are never anchored.
    assert(!bb_hud_edge(-0.9f, -0.6f, -0.1f, 0.1f, 1));
    assert(!bb_hud_edge(-0.9f, 0.9f, 0.7f, 0.9f, 1));
    puts("HUD policy: all regression cases passed");
    return 0;
}

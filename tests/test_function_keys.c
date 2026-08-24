#include "function_keys.h"

#include <assert.h>
#include <stdio.h>

typedef struct {
    unsigned vk;
    int down;
} Event;

static Event events[8];
static unsigned event_count;

void hostwin_inject_key(unsigned vk, int down)
{
    assert(event_count < sizeof(events) / sizeof(events[0]));
    events[event_count++] = (Event){vk, down};
}

int main(void)
{
    assert(!function_key_request(0x6f));
    assert(!function_key_request(0x7c));
    assert(function_key_request(0x75));
    assert(event_count == 1 && events[0].vk == 0x75 && events[0].down);

    assert(function_key_request(0x76));
    function_keys_tick();
    assert(event_count == 1);
    function_keys_tick();
    assert(event_count == 3);
    assert(events[1].vk == 0x75 && !events[1].down);
    assert(events[2].vk == 0x76 && events[2].down);
    function_keys_tick();
    function_keys_tick();
    assert(event_count == 4 && events[3].vk == 0x76 && !events[3].down);

    puts("function-key pulses: ok");
    return 0;
}

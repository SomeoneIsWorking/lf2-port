#include "function_keys.h"

#include "hostwin.h"

enum { VK_F1 = 0x70, VK_F12 = 0x7b, HOLD_BOUNDARIES = 2, QUEUE_CAPACITY = 4 };

static uint32_t queue[QUEUE_CAPACITY];
static unsigned queue_head;
static unsigned queue_count;
static uint32_t active;
static int boundaries;

static void start(uint32_t vk)
{
    active = vk;
    boundaries = HOLD_BOUNDARIES;
    hostwin_inject_key(vk, 1);
}

int function_key_request(uint32_t vk)
{
    if (vk < VK_F1 || vk > VK_F12) return 0;
    if (!active) {
        start(vk);
        return 1;
    }
    if (queue_count == QUEUE_CAPACITY) return 0;
    queue[(queue_head + queue_count) % QUEUE_CAPACITY] = vk;
    ++queue_count;
    return 1;
}

void function_keys_tick(void)
{
    if (active && --boundaries == 0) {
        hostwin_inject_key(active, 0);
        active = 0;
    }
    if (active || !queue_count) return;
    const uint32_t vk = queue[queue_head];
    queue_head = (queue_head + 1) % QUEUE_CAPACITY;
    --queue_count;
    start(vk);
}

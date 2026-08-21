#include <assert.h>
#include <stdio.h>

#include "texture_lru.h"

int main(void)
{
    TextureLruEntry entries[] = {{4}, {9}, {2}, {9}};

    assert(texture_lru_choose(entries, 4, 9) == 2);
    entries[2].last_frame = 9;
    assert(texture_lru_choose(entries, 4, 9) == 0);
    entries[0].last_frame = 9;
    assert(texture_lru_choose(entries, 4, 9) == -1);
    assert(texture_lru_choose(entries, 0, 9) == -1);

    puts("texture LRU: frame-live entries are protected and the oldest reusable entry wins");
    return 0;
}

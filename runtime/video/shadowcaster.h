/* Which LF2 world-object draws own a projected silhouette.
 *
 * The game's ordinary ellipse branch remains authoritative. A held physical item may suppress
 * that flat ellipse while it is carried, though, and then the old renderer lost the item's
 * silhouette entirely. data.txt's object type supplies the missing semantic fact: types 1, 2,
 * 4 and 6 are physical weapons/items; type 3 is an effect/projectile family and is not promoted
 * merely because it was drawn inside the world pass. This is data policy, not an asset list. */
#ifndef LF2_SHADOWCASTER_H
#define LF2_SHADOWCASTER_H

enum {
    SHADOWCASTER_LIGHT_WEAPON = 1,
    SHADOWCASTER_HEAVY_WEAPON = 2,
    SHADOWCASTER_THROWN_ITEM = 4,
    SHADOWCASTER_DRINK = 6,
};

static inline int shadowcaster_physical_type(int type)
{
    return type == SHADOWCASTER_LIGHT_WEAPON || type == SHADOWCASTER_HEAVY_WEAPON ||
           type == SHADOWCASTER_THROWN_ITEM || type == SHADOWCASTER_DRINK;
}

static inline int shadowcaster_should_cast(int draws_ellipse, int type)
{
    return draws_ellipse || shadowcaster_physical_type(type);
}

#endif

#ifndef __OBJECT_MODEL_H
#define __OBJECT_MODEL_H

#include <glib.h>

typedef struct _object_model ObjectWorldModel;

#include <lcm/lcm.h>
#include <lcmtypes/bot_core_pose_t.h>
#include <bot_param/param_client.h>
#include <lcmtypes/bot2_param.h>
#include <bot_param/param_util.h>

#include <lcmtypes/om_object_list_t.h>
#include <lcmtypes/om_object_t.h>

#define OM_POS_CHANNEL         "POSE"
#define OM_OL_CHANNEL          "OBJECT_LIST"

// NOTE: dynamic objects subscribes to "(OBJECTS|PALLETS)_UPDATE.*"
//   So we can send on multiple channels, have it function correctly, and
//   allow humans to differentiate by message intent.
#define OBJECT_ADD_CHANNEL     "OBJECTS_UPDATE_ADD"
#define OBJECT_UPDATE_CHANNEL  "OBJECTS_UPDATE"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * om_add_object:
     * @om The object model object.
     * @obj The object to add to the object model.
     * Returns: < 0 on error
     *
     * Adds a object to the object model.
     */

    int om_add_object(ObjectWorldModel *om, om_object_t *obj);
    
    /**
     * om_new:
     * Returns: The newly-allocated ObjectWorldModel object.
     *
     * Creates a new ObjectWorldModel object.
     */
    
    /**
     * om_update_object_pos:
     * @om The ObjectWorldModel object.
     * @obj The object you would like to move.
     * @x The new X-coordinate.
     * @y The new Y-coordinate.
     * @z The new Z-coordinate.
     *
     * Request that you update the position of a specific object in the world.
     */
    void om_update_object_pos(ObjectWorldModel *om, om_object_t *obj,
                              double x, double y, double z);

     
    /**
     * om_update_object:
     * @om The ObjectWorldModel object.
     * @obj The object you would like to move.
     * Request that you update the position of a specific object in the world.
     */

    void om_update_object(ObjectWorldModel *om, om_object_t *obj);

    /**
     * om_update_object_pos_by_id:
     * @om The ObjectWorldModel object.
     * @id The ID of the object you would like to move.
     * @x The new X-coordinate.
     * @y The new Y-coordinate.
     * @z The new Z-coordinate.
     *
     * Request that you update the position of a specific object in the world.
     */
    void om_update_object_pos_by_id(ObjectWorldModel *om, int64_t id,
                                    double x, double y, double z);

    /**
     * om_move_object_by_by_id:
     * @om The ObjectWorldModel object.
     * @id The ID of the object you would like to move.
     * @dx The change in the X-coordinate.
     * @dy The change in the Y-coordinate.
     * @dz The change in the Z-coordinate.
     *
     * Request that you update the position of a specific object in the world.
     */
    void om_move_object_by_by_id(ObjectWorldModel *om, int64_t id,
                                 double dx, double dy, double dz);

    /**
     * om_get_object_by_id:
     * @om The ObjectWorldModel object.
     * @id The ID of the object to retrieve.
     * Returns: The most-recently received list of objects.
     *
     * Gets the object by its ID with a sequential search.
     */
    om_object_t *om_get_object_by_id(ObjectWorldModel *om, int64_t id);


    /**
     * om_get_object_id_by_pos:
     * @om The ObjectWorldModel object.
     * @x The X-coordinate in the local frame
     * @y The Y-coordinate in the local frame
     * @z The Z-coordinate in the local frame
     * @dist (returned) The distance to the closest object
     *
     * Returns: The ID of the object nearest to specified (x,y,z) or -1
     *
     * Finds the id of the object nearest to specified (x,y,z) in the local frame.
     */
    int64_t om_get_object_id_by_pos(ObjectWorldModel *om, double x, double y, 
                                    double z, double *dist);


    /**
     * om_get_truck_id_by_pos:
     * @om The ObjectWorldModel object.
     * @x The X-coordinate in the local frame
     * @y The Y-coordinate in the local frame
     * @z The Z-coordinate in the local frame
     * @dist (returned) The distance to the closest truck
     *
     * Returns: The ID of the truck nearest to specified (x,y,z) or -1
     *
     * Finds the id of the truck nearest to specified (x,y,z) in the local frame.
     */

    ObjectWorldModel *om_new();

    /**
     * om_destroy:
     * @om The ObjectWorldModel object to destroy.
     *
     * Completely destroys (deallocates) this object and everything it holds in a
     * deep, clean way
     */

    void om_destroy(ObjectWorldModel *om);
    
    struct _object_model
    {
        GStaticRecMutex mutex;
        
        lcm_t *lcm;
        om_object_list_t *ol;                     // last seen object list.
        om_object_list_t_subscription_t *ol_sub;  // object list subscription.
        bot_core_pose_t *pose;                         // position of the bot.
        bot_core_pose_t_subscription_t *pose_sub;      // position subscription.
        BotParam   *param;
        
        GHashTable *hash;
    };

#ifdef __cplusplus
}
#endif

#endif

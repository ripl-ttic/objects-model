q#include "object_client.h"

#define ERR(fmt, ...) \
do { \
    fprintf(stderr, "["__FILE__":%d] Error: ", __LINE__); \
    fprintf(stderr, fmt, ##__VA_ARGS__); \
    fflush(stderr); \
} while (0)

#define wrn(fmt, ...) \
do { \
    printf("["__FILE__":%d] Warning: ", __LINE__); \
    printf(fmt, ##__VA_ARGS__); \
    fflush(stdout); \
} while (0)


#if 0
#define DBG(fmt, ...) \
    do { \
        printf(fmt, ##__VA_ARGS__); \
        fflush(stdout); \
    } while (0)
#define dbg_mat(m) \
    do { \
        DBG("%+5.3f %+5.3f %+5.3f %+5.3f\n", m[0], m[1], m[2], m[3]); \
        DBG("%+5.3f %+5.3f %+5.3f %+5.3f\n", m[4], m[5], m[6], m[7]); \
        DBG("%+5.3f %+5.3f %+5.3f %+5.3f\n", m[8], m[9], m[10], m[11]); \
        DBG("%+5.3f %+5.3f %+5.3f %+5.3f\n", m[12], m[13], m[14], m[15]); \
    } while (0)
#define dbg_m4v3v3(m,u,v) \
    do { \
        DBG("%+5.3f %+5.3f %+5.3f %+5.3f   %+5.3f   %+5.3f\n", \
                m[0], m[1], m[2], m[3], u[0], v[0]); \
        DBG("%+5.3f %+5.3f %+5.3f %+5.3f   %+5.3f   %+5.3f\n", \
                m[4], m[5], m[6], m[7], u[1], v[1]); \
        DBG("%+5.3f %+5.3f %+5.3f %+5.3f x %+5.3f = %+5.3f\n", \
                m[8], m[9], m[10], m[11], u[2], v[2]); \
        DBG("%+5.3f %+5.3f %+5.3f %+5.3f   %+5.3f   %+5.3f\n", \
                m[12], m[13], m[14], m[15], 1.0, 1.0); \
    } while (0)
#else
#define DBG(fmt, ...)
#define dbg_mat(m)
#define dbg_m4v3v3(m,u,v)
#endif

uint64_t get_unique_id() 
{
    return (0xefffffffffffffff&(bot_timestamp_now()<<8)) + 256*rand()/RAND_MAX;
}

int om_add_object(ObjectWorldModel *om, om_object_t *obj)
{
    // set the object id if not set by the calling process
    // (object ids required to be > 1 (or is it 0?))
    if (obj->id <= 0)
        obj->id = get_unique_id();

    om_object_list_t list =
    {
        .utime = obj->utime,
        .num_objects = 1,
        .objects = obj
    };
    return om_object_list_t_publish(om->lcm, OBJECT_ADD_CHANNEL, &list);
}
void om_update_object(ObjectWorldModel *om, om_object_t *obj)
{
    om_object_list_t list =
    {
      .utime = bot_timestamp_now(),
      .num_objects = 1,
      .objects = obj
    };
    om_object_list_t_publish(om->lcm, OBJECT_UPDATE_CHANNEL, &list);
}


void om_update_object_pos(ObjectWorldModel *om, om_object_t *obj,
        double x, double y, double z)
{
    obj->pos[0] = x;
    obj->pos[1] = y;
    obj->pos[2] = z;
    
    om_object_list_t list =
    {
      .utime = bot_timestamp_now(),
      .num_objects = 1,
      .objects = obj
    };
    om_object_list_t_publish(om->lcm, OBJECT_UPDATE_CHANNEL, &list);
}

void om_update_object_pos_by_id(ObjectWorldModel *om, int64_t id,
        double x, double y, double z)
{
    om_object_t *obj = om_get_object_by_id(om, id);
    om_update_object_pos(om, obj, x, y, z);
    om_object_t_destroy(obj);
}

void om_move_object_by_by_id(ObjectWorldModel *om, int64_t id,
        double dx, double dy, double dz)
{
    om_object_t *obj = om_get_object_by_id(om, id);
    om_update_object_pos(om, obj,
            obj->pos[0] + dx,
            obj->pos[1] + dy,
            obj->pos[2] + dz);
    om_object_t_destroy(obj);
}

// XXX Get a guarantee that it's sorted and use binary search?
om_object_t *om_get_object_by_id(ObjectWorldModel *om, int64_t id)
{
    g_static_rec_mutex_lock(&om->mutex);
    if (NULL != om->ol)
    {
        for (int i = 0; i < om->ol->num_objects; i++)
        {
            om_object_t *obj = &om->ol->objects[i];
            if (obj->id == id)
            {
                om_object_t *rtn = (obj ? om_object_t_copy(obj) : NULL);
                g_static_rec_mutex_unlock(&om->mutex);
                return rtn;
            }
        }
    }
    g_static_rec_mutex_unlock(&om->mutex);
    //ERR("No object with ID %"PRId64" exists!\n", id);

    return NULL;
}


int64_t om_get_object_id_by_pos(ObjectWorldModel *om, double x, double y,
                                double z, double *dist)
{
    g_static_rec_mutex_lock(&om->mutex);

    if (NULL != om->ol) 
    {
        double min_dist = 20.0; // no point in considering objects farther than 10m
        int64_t closest_id = -1;

        for (int i = 0; i < om->ol->num_objects; i++) 
        {
            om_object_t *obj = &om->ol->objects[i];
            double dist = sqrt (bot_sq(obj->pos[0]-x) + bot_sq(obj->pos[1]-y) 
                                + bot_sq(obj->pos[2]-z));

            if (dist < min_dist) {
                min_dist = dist;
                closest_id = obj->id;
            }
        }

        *dist = min_dist;
        g_static_rec_mutex_unlock(&om->mutex);
        return closest_id;
    }
    else
    {
        *dist = DBL_MAX;
        g_static_rec_mutex_unlock(&om->mutex);
        return -1;
    }   
}

/**
 * Handles the LCM message that publishes all known objects.
 */
void _om_on_object_list(const lcm_recv_buf_t *rbuf, const char *channel,
                        const om_object_list_t *msg, void *user)
{
    //fprintf(stderr,"Received\n");
    ObjectWorldModel *om = (ObjectWorldModel*)user;
    g_static_rec_mutex_lock(&om->mutex);
    if (om->ol) om_object_list_t_destroy(om->ol);
    om->ol = om_object_list_t_copy(msg);
    g_static_rec_mutex_unlock(&om->mutex);
}

/**
 * Handles the LCM message that publishes the position of the forklift.
 */
void _om_on_pose(const lcm_recv_buf_t *rbuf, const char *channel,
                 const bot_core_pose_t *msg, void *user)
{
    ObjectWorldModel *om = (ObjectWorldModel*)user;
    g_static_rec_mutex_lock(&om->mutex);
    if (om->pose) bot_core_pose_t_destroy(om->pose);
    om->pose = bot_core_pose_t_copy(msg);
    g_static_rec_mutex_unlock(&om->mutex);
}



ObjectWorldModel *om_new()
{
    ObjectWorldModel *om = (ObjectWorldModel*)calloc(1, sizeof(ObjectWorldModel));

    if (!(om->lcm = bot_lcm_get_global(NULL))){
        //add lcm to mainloop 
        free(om);

        ERR("Could not get LCM!\n");
        return NULL;
    }
    bot_glib_mainloop_attach_lcm (om->lcm);

    // Set up param
    if (!(om->param = bot_param_new_from_server(om->lcm, 1)))
    {
        free(om);

        ERR("Could not get BotConf!\n");
        return NULL;
    }

    // Create a hash table for me to quickly receive BotCamTrans objects.
    /*if (!(om->hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                           free, (GDestroyNotify)bot_camtrans_destroy)))
    {
        om_destroy(om);
        ERR("Could not make hash table!\n");
        return NULL;
        }*/
    
    /*char **cam_names = arconf_get_all_camera_names(om->config);
    for (int i = 0; cam_names && cam_names[i]; i++)
    {
        BotCamTrans *camtrans = arconf_get_new_camtrans(om->config, cam_names[i]);
        if (camtrans)
            g_hash_table_insert(om->hash, g_strdup(cam_names[i]), camtrans);
        else
            ERR("Could not get BotCamTrans for %s.\n", cam_names[i]);
    }
    if (cam_names) bot_conf_str_array_free(cam_names);

    if (!g_hash_table_size(om->hash))
    ERR("No valid camera parameter sets found in config system\n");*/

    // Blocking to resolve concurrent modifications.
    g_static_rec_mutex_init(&om->mutex);

    om->ol_sub = om_object_list_t_subscribe(om->lcm,
              OM_OL_CHANNEL, &_om_on_object_list, om);
    
    om->pose_sub = bot_core_pose_t_subscribe(om->lcm,
               OM_POS_CHANNEL, &_om_on_pose, om);
    
    if (!om->ol_sub || !om->pose_sub)
    {
        om_destroy(om);
        ERR("Could not get subscribe to LCM messages!\n");
        return NULL;
    }

    // Add some default (empty) lists to prevent future segfaults.
    om->ol = calloc(1, sizeof(om_object_list_t));

    return om;
}

void om_destroy(ObjectWorldModel *om)
{
    if (!om) return;

    DBG("Freeing pose\n");
    if (om->pose) bot_core_pose_t_destroy(om->pose);
    DBG("Freeing object list\n");
    if (om->ol) om_object_list_t_destroy(om->ol);

    if (om->lcm)
    {
        DBG("Freeing pose subscription\n");
        if (om->pose_sub) bot_core_pose_t_unsubscribe(om->lcm, om->pose_sub);
        DBG("Freeing object list subscription\n");
        if (om->ol_sub) om_object_list_t_unsubscribe(om->lcm, om->ol_sub);
        DBG("Freeing lcm\n");
    }
    DBG("Freeing om\n");
    free(om);
}

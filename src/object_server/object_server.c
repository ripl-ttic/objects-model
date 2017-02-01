
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#define _GNU_SOURCE
#include <getopt.h>

#include <bot_core/bot_core.h>
#include <lcm/lcm.h>

#include <lcmtypes/om_object_list_t.h>
#include <lcmtypes/om_object_t.h>

#if 1
#define ERR(...) do { fprintf(stderr, "[%s:%d] ", __FILE__, __LINE__); \
                      fprintf(stderr, __VA_ARGS__); fflush(stderr); } while(0)
#else
#define ERR(...) 
#endif

#if 1
#define DBG(...) do { fprintf(stdout, __VA_ARGS__); fflush(stdout); } while(0)
#else
#define DBG(...) 
#endif


#define OBJECTS_PUBLISH_HZ 20

#define LOCAL_FRAME_ID 0
#define FORKLIFT_OBJECT_ID 1             
#define GLOBAL_FRAME_ID 2             

typedef struct _dynamic_objects_t {
    lcm_t     *lcm;
    GMainLoop *main_loop;
    guint      timer_id;

    GHashTable *objects;
    om_object_list_t object_list;
    GMutex *mutex;

    gboolean use_global_pose;

    int verbose;

    int simulation;

} dynamic_objects_t;

static gboolean
_g_int64_t_equal (gconstpointer v1,gconstpointer v2) {
    return (*(int64_t*)v1)==(*(int64_t*)v2);
}

static guint
_g_int64_t_hash (gconstpointer v) {
    // use bottom 4 bytes (should be most unique)
    return 0x00ffffffff&(*(int64_t *)v);
}


void
my_transform(double pos1[3], double quat1[4], 
             double pos0[3],  double quat0[4],
             double pos_offset[3], double quat_offset[4]) 
{
    double offset[3] = {pos_offset[0],pos_offset[1],pos_offset[2]};
    bot_quat_rotate(quat0, offset);
    pos1[0] = pos0[0]+offset[0];
    pos1[1] = pos0[1]+offset[1];
    pos1[2] = pos0[2]+offset[2];
    
    bot_quat_mult(quat1,quat0,quat_offset);
}


void
my_transform_rev(double pos1[3], double quat1[4],
                 double pos0[3],  double quat0[4],
                 double pos_offset[3], double quat_offset[4]) 
{
        pos1[0]=pos0[0]-pos_offset[0];
        pos1[1]=pos0[1]-pos_offset[1];
        pos1[2]=pos0[2]-pos_offset[2];
        bot_quat_rotate_rev(quat_offset, pos1);
        double quat_rev[4] = { quat_offset[0], -quat_offset[1],
                               -quat_offset[2], -quat_offset[3]};
        bot_quat_mult(quat1, quat0, quat_rev);
}


static void
on_objects_update(const lcm_recv_buf_t *rbuf, const char *channel,
               const om_object_list_t *msg, void *user)
{
    dynamic_objects_t *self = (dynamic_objects_t*)user;
    g_mutex_lock(self->mutex);
    for (int i = 0; i < msg->num_objects; i++) {
        om_object_t *object = &msg->objects[i]; 
        om_object_t *my_object = g_hash_table_lookup(self->objects, &object->id);

        if (self->verbose)
            fprintf (stdout,"Got request to update object with id = %"PRId64" : \n", object->id);

        if (!my_object) {
            // add object to hash table.
            my_object = om_object_t_copy(object);
            g_hash_table_insert(self->objects, &my_object->id, my_object);                        
            if (self->verbose)
                fprintf (stdout,"... Doesn't exist, adding object with id = %"PRId64" \n", my_object->id);
        }
        else {
            // update object if the update time is newer than the last access
            if (my_object->utime < object->utime) {
                memcpy(my_object,object,sizeof(om_object_t));
                
                if (self->verbose)
                    fprintf (stdout, "... Exists, updating object id = %"PRId64" \n", my_object->id);
            }
            else if (self->verbose)
                fprintf (stdout, "... Exists but utime is old. Ignoring update for object id = %"PRId64" \n", my_object->id);
        }
    }
    g_mutex_unlock(self->mutex);
}


int
_matrix_to_quat_pos(const double mat[16], double quat[4], double pos[3]) 
{
    // assuming no scaling or skew.
    if ((fabs(mat[15]-1.0)>0.001)||(fabs(mat[12])>0.001)||(fabs(mat[13])>0.001)||(fabs(mat[14])>0.001)) { 
        fprintf(stderr,"ERROR:_matrix_to_quat_pos: not handing: scale:%le or skew:[%le,%le,%le]\n",
                mat[15],mat[12],mat[13],mat[14]);
        fprintf(stderr,"m:\t%lf,\t%lf,\t%lf,\t%lf\n",mat[0],mat[1],mat[2],mat[3]);
        fprintf(stderr,"m:\t%lf,\t%lf,\t%lf,\t%lf\n",mat[4],mat[5],mat[6],mat[7]);
        fprintf(stderr,"m:\t%lf,\t%lf,\t%lf,\t%lf\n",mat[8],mat[9],mat[10],mat[11]);
        fprintf(stderr,"m:\t%lf,\t%lf,\t%lf,\t%lf\n",mat[12],mat[13],mat[14],mat[15]);
   }
    double rot[9]={mat[0],mat[1],mat[2],mat[4],mat[5],mat[6],mat[8],mat[9],mat[10]};
    if (pos) {
        pos[0]=mat[3]; pos[1]=mat[7]; pos[2]=mat[11];
    }
    return bot_matrix_to_quat(rot,quat);
}

static void
dynamic_objects_publish_object_list(dynamic_objects_t *self)
{
    g_mutex_lock(self->mutex);

    GList *objects = g_hash_table_get_values (self->objects);
    
    int nobjects = g_list_length(objects);
    if (nobjects!=self->object_list.num_objects) {
        if (self->object_list.objects)
            free(self->object_list.objects);
        self->object_list.num_objects=nobjects;
        self->object_list.objects=calloc(nobjects,sizeof(om_object_t));
    }

    int64_t now = bot_timestamp_now();
    self->object_list.utime = now;
    int idx=0;
    for (GList *iter = objects; iter; iter=iter->next) {
        memcpy(&self->object_list.objects[idx++],iter->data,sizeof(om_object_t));
    }
    om_object_list_t_publish(self->lcm, "OBJECT_LIST", &self->object_list);
    g_list_free(objects);
    g_mutex_unlock(self->mutex);
}

/*
static void
dynamic_objects_publish_rects(dynamic_objects_t *self)
{
    if (!self->publish_rects)
        return;

    g_mutex_lock(self->mutex);
    GList *pallets = g_hash_table_get_values (self->pallets);
    GList *objects = g_hash_table_get_values (self->objects);
    
    int npallets = g_list_length(pallets);
    int nobjects = g_list_length(objects);
    int nrects = npallets+nobjects;
    if (self->obstacle_list_num_alloc_rects<nrects) {
        if (self->obstacle_list.rects.rects)
            free(self->obstacle_list.rects.rects);
        self->obstacle_list_num_alloc_rects=nrects;
        self->obstacle_list.rects.rects=calloc(nrects,sizeof(arlcm_rect_t));
    }
    arlcm_rect_t *rect = self->obstacle_list.rects.rects;
    int64_t now = bot_timestamp_now();
    self->obstacle_list.rects.utime = now;
    self->obstacle_list.tracks.utime = now;
    //double pos_pose[3]={0.0, 0.0, 0.0};
    botlcm_pose_t local_pose;
    atrans_get_local_pose(self->atrans, &local_pose);
    //self->obstacle_list.rects.xy[0]=pos_pose[0];
    //self->obstacle_list.rects.xy[1]=pos_pose[1];
    self->obstacle_list.rects.xy[0]=local_pose.pos[0];
    self->obstacle_list.rects.xy[1]=local_pose.pos[1];
    // make rects for pallets
    int count=0;
    int64_t ignore_object_id=-1;
    for (GList *iter = pallets; iter; iter=iter->next) {
        arlcm_pallet_t *pallet = iter->data;
        if (self->last_man_status.pallet_id==pallet->id) {
            // we are trying to pick up this pallet so don't put a rect under it.
            if (pallet->relative_to_id)
                ignore_object_id = pallet->relative_to_id;
            continue;
        }
        // if relative to an object instead of world frame we don't want to put an obstacle rect there
        // because this would mean the pallet is on a truck or on the tines
        // either way we don't need to put an obstacle rect there.
        if ((pallet->relative_to_id != GLOBAL_FRAME_ID && pallet->relative_to_id != LOCAL_FRAME_ID) 
            || (pallet->pallet_type==ARLCM_PALLET_ENUM_T_PHANTOM))
            continue;

        
        // If the pallet is relative to the global frame, grab pose in local frame
        double pallet_pos_local[3];
        double pallet_quat_local[4];
        if (pallet->relative_to_id == GLOBAL_FRAME_ID) {
            BotTrans global_to_local;
            if (!atrans_get_trans (self->atrans, "global", "local",
                                   &global_to_local)) {
                fprintf (stderr, "dynamic_objects_publish_rects: Error converting from global to local for pallet pose\n");
                continue;
            }
                
            // Create and apply the transformation for my pallet as
            BotTrans pallet_trans;
            bot_trans_set_from_quat_trans (&pallet_trans, pallet->orientation,
                                           pallet->pos);
            bot_trans_apply_trans ( &pallet_trans, &global_to_local);
            
            memcpy (pallet_quat_local, pallet_trans.rot_quat, 4*sizeof(double));
            memcpy (pallet_pos_local, pallet_trans.trans_vec, 3*sizeof(double));
        }
        else {
            memcpy (pallet_quat_local, pallet->orientation, 4*sizeof(double));
            memcpy (pallet_pos_local, pallet->pos, 3*sizeof(double));
        }


        rect->size[0]= pallet->bbox_max[0]-pallet->bbox_min[0];
        rect->size[1]= pallet->bbox_max[1]-pallet->bbox_min[1];
        double rpy_pallet[3];
        bot_quat_to_roll_pitch_yaw(pallet_quat_local, rpy_pallet);
        rect->theta=rpy_pallet[2];
        double offset[2] = {pallet->bbox_min[0]+rect->size[0]/2.0,
                            pallet->bbox_min[1]+rect->size[1]/2.0};
        double sintheta,costheta;
        bot_fasttrig_sincos(rect->theta,&sintheta,&costheta);
        //rect->dxy[0]= pallet->pos[0]-pos_pose[0] + (costheta*offset[0]-sintheta*offset[1]);
        //rect->dxy[1]= pallet->pos[1]-pos_pose[1] + (sintheta*offset[0]+costheta*offset[1]);
        rect->dxy[0]= pallet_pos_local[0]-local_pose.pos[0] + (costheta*offset[0]-sintheta*offset[1]);
        rect->dxy[1]= pallet_pos_local[1]-local_pose.pos[1] + (sintheta*offset[0]+costheta*offset[1]);
        //fprintf (stdout, "Adding pallet rect at dxy[0] = %.2f and dxy[1] = %.2f\n", rect->dxy[0], rect->dxy[1]);
        rect++;
        count++;
    }
    g_list_free(pallets);
    // make rects for objects
    for (GList *iter = objects; iter; iter=iter->next) {
        arlcm_object_t *object = iter->data;
        // we are trying to approach this object to pick up a pallet so don't make an obstacle rect for it.
        if (object->id==ignore_object_id) 
            continue;

        rect->size[0]= object->bbox_max[0]-object->bbox_min[0];
        rect->size[1]= object->bbox_max[1]-object->bbox_min[1];
        double rpy_object[3];
        bot_quat_to_roll_pitch_yaw(object->orientation,rpy_object);
        rect->theta=rpy_object[2];
        double offset[2] = {object->bbox_min[0]+rect->size[0]/2.0,
                            object->bbox_min[1]+rect->size[1]/2.0};
        double sintheta,costheta;
        bot_fasttrig_sincos(rect->theta,&sintheta,&costheta);
        //rect->dxy[0]= object->pos[0]-pos_pose[0] + (costheta*offset[0]-sintheta*offset[1]);
        //rect->dxy[1]= object->pos[1]-pos_pose[1] + (sintheta*offset[0]+costheta*offset[1]);
        rect->dxy[0]= object->pos[0]-local_pose.pos[0] + (costheta*offset[0]-sintheta*offset[1]);
        rect->dxy[1]= object->pos[1]-local_pose.pos[1] + (sintheta*offset[0]+costheta*offset[1]);
        rect++;
        count++;
    }
    g_list_free(objects);
    self->obstacle_list.rects.num_rects=count;
    
    arlcm_obstacle_list_t_publish(self->lcm, "OBSTACLES", &self->obstacle_list);
    g_mutex_unlock(self->mutex);
}
*/


static gboolean
on_timer(gpointer data)
{
    dynamic_objects_t *self = (dynamic_objects_t*)data;
    g_assert(self);
    dynamic_objects_publish_object_list(self);
    return TRUE;
}

static void
dynamic_objects_destroy(dynamic_objects_t *self)
{
    if (!self)
        return;

    if (self->objects)
        g_hash_table_destroy(self->objects);

    if (self->main_loop)
        g_main_loop_unref(self->main_loop);

    free(self);
}

static dynamic_objects_t*
dynamic_objects_create()
{
    dynamic_objects_t *self = (dynamic_objects_t*)calloc(1, sizeof(dynamic_objects_t));
    if (!self) {
        ERR("Error: dynamic_objects_create() failed to allocate self\n");
        goto fail;
    }

    /* Mutex */
    g_thread_init(NULL);
    self->mutex = g_mutex_new();

    /* main loop */
    self->main_loop = g_main_loop_new(NULL, FALSE);
    if (!self->main_loop) {
        ERR("Error: dynamic_objects_create() failed to create the main loop\n");
        goto fail;
    }

    /* LCM */
    self->lcm = bot_lcm_get_global (NULL);

    bot_glib_mainloop_attach_lcm (self->lcm);

    /* object list periodic timeout */
    self->timer_id = g_timeout_add(1000.0/OBJECTS_PUBLISH_HZ, on_timer, self);
    if (!self->timer_id) {
        ERR("Error: dynamic_objects_create() failed to create the glib timer");
        goto fail;
    }
    
    /* create hash tables */
    self->objects = g_hash_table_new(_g_int64_t_hash,_g_int64_t_equal);
    if (!self->objects) {
        ERR("Error: dynamic_objects_create() failed to create the object array\n");
        goto fail;
    }

    /* subscribe to update channels */
    om_object_list_t_subscribe(self->lcm, "OBJECTS_UPDATE.*", on_objects_update, self);

    return self;
 fail:
    dynamic_objects_destroy(self);
    return NULL;
}



static void usage(int argc, char ** argv)
{
    fprintf (stderr, "Usage: %s [options]\n"
             "Object Server: Publishes objects from world model.\n"
             "\n"
             "  -v, --verbose          verbose output\n"
             "  -h, --help             shows this help text and exits\n"
             "  -r, --rects            publish rects\n"             
             "  -g, --global           maintain pose estimates in GLOBAL frame\n"
             "\n",
             argv[0]);
}


int main(int argc, char *argv[])
{
    setlinebuf (stdout);
    fprintf (stdout, "Starting....\n");

    dynamic_objects_t *self = (dynamic_objects_t*)dynamic_objects_create();
    if (!self)
        return 1;
    
    char *optstring = "hrgv";
    char c;
    struct option long_opts[] =
    {
        { "help",      no_argument,       0, 'h' },
        { "rects",     no_argument,       0, 'r' },
        { "global",    no_argument,       0, 'g' },
        { "verbose",   no_argument,       0, 'v' },
        { 0, 0, 0, 0}
    };
    
    while ((c = getopt_long (argc, argv, optstring, long_opts, 0)) >= 0)
    {
        switch (c) 
        {
            case 'g': 
                self->use_global_pose = TRUE; 
                break;
            case 'v': 
                self->verbose = TRUE; 
                break;
            case 'h':
            default:
                usage(argc, argv); 
                return 1;
        }
    }
    
    int return_code = 0;

    if (bot_signal_pipe_glib_quit_on_kill(self->main_loop)) {
        ERR("Error: Failed to set signal handler to quit main loop upon "
            "terminating signals\n");
        return_code = -1;
    }
    else
        g_main_loop_run(self->main_loop);

    dynamic_objects_destroy(self);

    return 0;
}

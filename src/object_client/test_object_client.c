#include "object_client.h"
#include <unistd.h>
int main(){
    
    ObjectWorldModel *om = om_new();

    om_object_t new_obj;
    new_obj.utime = bot_timestamp_now();
    new_obj.id = 10; 
    new_obj.pos[0] = 1.0;
    new_obj.pos[1] = .0;
    new_obj.pos[2] = .0;
    
    double rpy[3] = {0};
    bot_roll_pitch_yaw_to_quat(rpy, new_obj.orientation);
    new_obj.bbox_min[0] = -1.0;
    new_obj.bbox_min[1] = -1.0;
    new_obj.bbox_min[2] = -1.0;

    new_obj.bbox_max[0] = 1.0;
    new_obj.bbox_max[1] = 1.0;
    new_obj.bbox_max[2] = 1.0;

    new_obj.object_type = OM_OBJECT_T_TABLE;
    new_obj.label = "Table";

    om_add_object(om, &new_obj);

    fprintf(stderr, "Added object to server\n");

    om_object_t *obj_by_id = NULL;

    while (!obj_by_id){
        sleep(1);
        lcm_handle(om->lcm);
        obj_by_id = om_get_object_by_id(om, 10);        
    }

    fprintf(stderr, "Got object from server\n");
    
    return 0;
} 

/*
 * object server renderer
 */

#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <GL/gl.h>
#include <gdk/gdkkeysyms.h>

#include <lcm/lcm.h>
#include <bot_core/bot_core.h>
#include <bot_vis/bot_vis.h>
#include <bot_frames/bot_frames.h>

#include <geom_utils/geometry.h>
#include <path_util/path_util.h>

#include <lcmtypes/om_object_list_t.h>
#include <lcmtypes/om_object_t.h>
#include <lcmtypes/om_xml_cmd_t.h>

#define RENDERER_NAME "Object Model"
#define PARAM_TRIADS "Draw Triads"
#define PARAM_BBOX "Draw Bounding Boxes"
#define PARAM_OBJECT_IDS "Draw Object IDs"

#if 1
#define ERR(...) do { fprintf(stderr, "[%s:%d] ", __FILE__, __LINE__); \
                      fprintf(stderr, __VA_ARGS__); fflush(stderr); } while(0)
#else
#define ERR(...) 
#endif

#if 1
#define ERR_ONCE(...) do { print_error_once(__VA_ARGS__); } while(0)
static void
print_error_once(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    char buf_stack[512];
    char *buf = buf_stack;
    int len = vsnprintf(buf, sizeof(buf_stack), format, ap);
    if (len >= sizeof(buf_stack)) {
        buf = (char*)malloc(len+1);
        vsnprintf(buf, len+1, format, ap);
    }
    if (0 == g_quark_try_string(buf)) {
        g_quark_from_string(buf);
        ERR("%s", buf);
    }
    if (buf != buf_stack)
        free(buf);
    va_end(ap);
}
#else
#define ERR_ONCE(...) 
#endif

#if 0
#define DBG(...) do { fprintf(stdout, __VA_ARGS__); fflush(stdout); } while(0)
#else
#define DBG(...) 
#endif

#ifndef DRAW_UNIT_TRIADS_DEFAULT
#define DRAW_UNIT_TRIADS_DEFAULT FALSE
#endif
#ifndef DRAW_BBOX_DEFAULT
#define DRAW_BBOX_DEFAULT FALSE
#endif
#ifndef DRAW_OBJECT_IDS_DEFAULT
#define DRAW_OBJECT_IDS_DEFAULT FALSE
#endif


typedef struct _object_wavefront_model {
    int16_t id;
    GLuint gl_list; 
} object_wavefront_model;

typedef struct _renderer_object_model_t {
    BotRenderer renderer;
    BotEventHandler ehandler;
    BotViewer   *viewer;
    BotParam * param;
    lcm_t    *lcm;
    om_object_list_t_subscription_t *object_lcm_hid;

    BotGtkParamWidget *pw;
    gboolean draw_unit_triads;
    gboolean draw_bbox;
    gboolean draw_object_ids;

    GMutex *mutex; /* protect self */

    om_object_list_t *object_list;
    
    int num_of_models;
    GHashTable *model_hash;

    gchar *last_save_filename;

    // for teleport object
    int             did_teleport; // have we done our initial teleport?
    int             teleport_request;
    uint64_t        hover_id;
    om_object_t *teleport_object;
} renderer_om_object_t;

//adding a mapping from type to string (name of object to object id
char *get_char_from_id(int16_t id){    
    switch(id){
    case OM_OBJECT_T_TABLE:
        return "table";
        break;
    case OM_OBJECT_T_CHAIR:
        return "chair";
        break;
    case OM_OBJECT_T_TRASHCAN:
        return "trashcan";
        break;
    case OM_OBJECT_T_BED:
        return "bed";
        break;
    case OM_OBJECT_T_FRIDGE:
        return "fridge";
        break;
    case OM_OBJECT_T_MICROWAVE:
        return "mircowave";
        break;
    case OM_OBJECT_T_TV:
        return "tv";
        break;
    case OM_OBJECT_T_ELEVATOR_DOOR:
        return "elevator_door";
        break;
    case OM_OBJECT_T_LAPTOP:
        return "laptop";
        break;
    case OM_OBJECT_T_WATER_FOUNTAIN:
        return "water_fountain";
        break;
    default:
        return "default";
        break;
    }
}

static GLuint
compile_display_list (renderer_om_object_t* self, char *object_name, BotWavefrontModel * model)//, double span_x, double span_y, double span_z)
{
    GLuint dl = glGenLists (1);
    glNewList (dl, GL_COMPILE);
    
    char key[1024];

    glPushMatrix();

    sprintf(key, "models.%s.translate", object_name);
    double trans[3];
    if (bot_param_get_double_array(self->param, key, trans, 3) == 3)
        glTranslated(trans[0], trans[1], trans[2]);

    sprintf(key, "models.%s.scale", object_name);
    double scale;
    if (bot_param_get_double(self->param, key, &scale) == 0)
        glScalef(scale, scale, scale);

    sprintf(key, "models.%s.rotate_xyz", object_name);
    double rot[3];
    if (bot_param_get_double_array(self->param, key, rot, 3) == 3) {
        glRotatef(rot[2], 0, 0, 1);
        glRotatef(rot[1], 0, 1, 0);
        glRotatef(rot[0], 1, 0, 0);
    }

    glEnable(GL_LIGHTING);
    bot_wavefront_model_gl_draw(model);
    glDisable(GL_LIGHTING);

    glPopMatrix();

    glEndList ();
    return dl;


    /* 
     * GLuint dl = glGenLists (1);
     * glNewList (dl, GL_COMPILE);
     * 
     * glPushMatrix();
     * 
     * sprintf(key, "models.%s.translate", object_name);
     * //double trans[3];
     * //if (bot_param_get_double_array(self->param, key, trans, 3) == 3)
     * //glTranslated(trans[0], trans[1], trans[2]);
     * 
     * //this might be wrong - since we are scaling differntly 
     * //glTranslated(-span_x/2, -span_y/2, -span_z/2);
     * 
     * sprintf(key, "models.%s.scale", object_name);
     * //double scale;
     * //if (bot_param_get_double(self->param, key, &scale) == 0)
     * //glScalef(scale, scale, scale);
     * //double scale = 0.01;
     * //glScalef(scale, scale, scale);
     * glScalef(1/span_x, 1/span_y, 1/span_z);
     * //glScalef(0.1, 0.1, 0.1);
     * 
     * //glTranslated(-span_x/2 * scale, -span_y/2 * scale, -span_z/2 * scale);
     * glTranslated(-0.5, -0.5, -0.5);
     * 
     * sprintf(key, "models.%s.rotate_xyz", object_name);
     * double rot[3];
     * if (bot_param_get_double_array(self->param, key, rot, 3) == 3) {
     *   glRotatef(rot[2], 0, 0, 1);
     *   glRotatef(rot[1], 0, 1, 0);
     *   glRotatef(rot[0], 1, 0, 0);
     * }
     * 
     * glEnable(GL_LIGHTING);
     * bot_wavefront_model_gl_draw(model);
     * glDisable(GL_LIGHTING);
     * 
     * glPopMatrix();
     * 
     * glEndList ();
     * 
     * return dl;
     */
}

static GLuint
get_triad(double x, double y, double z)
{  
    GLuint dl = glGenLists (1);
    glNewList (dl, GL_COMPILE);

    /* draw a set of lines denoting current triad (helpful for debug) */
    glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT);
    glDisable(GL_LIGHTING);
    glBegin(GL_LINES);
    glColor4f(1,0,0,1);
    glVertex3f(0., 0., 0.);
    glVertex3f(x, 0., 0.);
    glColor3f(0,1,0);
    glVertex3f(0., 0., 0.);
    glVertex3f(0., y, 0.);
    glColor3f(0,0,1);
    glVertex3f(0., 0., 0.);
    glVertex3f(0., 0., z);
    glEnd();
    glPopAttrib();
    
    glEndList();

    
    return dl;
}

static GLuint 
get_wavefront_model(char *object_name, renderer_om_object_t *self){
    
    const char * models_dir = getModelsPath();

    char *model_name;
    char model_full_path[256];

    BotWavefrontModel *object_model;
    char param_key[1024];
    snprintf(param_key, sizeof(param_key), "models.%s.wavefront_model", object_name);
    
    if (bot_param_get_str(self->param, param_key, &model_name) == 0) {
        snprintf(model_full_path, sizeof(model_full_path), "%s/%s", models_dir, model_name);
        object_model = bot_wavefront_model_create(model_full_path);

        GLuint model_dl = compile_display_list(self, object_name, object_model);//, span_x, span_y, span_z);

        bot_wavefront_model_destroy(object_model);
        return model_dl; 
    }
    else {
        fprintf(stderr, "Warning: Model for object of type %s not found in config, drawing as wireframe\n", param_key);
        return get_triad(1.0, 1.0, 1.0);
    }
}


static void
draw_wavefront_model (GLuint wavefront_dl)
{
    glEnable (GL_BLEND);
    glEnable (GL_RESCALE_NORMAL);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glShadeModel (GL_SMOOTH);
    glEnable (GL_LIGHTING);

    /* 
     * glPushMatrix();
     * ///x,y,z
     * glTranslated(x, y, 0);
     * 
     * //orientation
     * glRotatef(heading, 0, 0, 1);
     * 
     * glScalef(scale_x, scale_y, scale_z);
     */
    
    glCallList (wavefront_dl);
    //undo 
    //glPopMatrix();    
}

/**
 * draw a set of lines denoting current triad (helpful for debug)
 */
static void
draw_unit_triad()
{  
    static GLuint dl = 0;
    if (!dl) {
        dl = glGenLists(1);
        if (dl) {
            glNewList(dl, GL_COMPILE);
            DBG("Compiling unit triad display list\n");
        }

        /* draw a set of lines denoting current triad (helpful for debug) */
        glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT);
        glDisable(GL_LIGHTING);
        glBegin(GL_LINES);
        glColor4f(1,0,0,1);
        glVertex3f(0., 0., 0.);
        glVertex3f(1., 0., 0.);
        glColor3f(0,1,0);
        glVertex3f(0., 0., 0.);
        glVertex3f(0., 1., 0.);
        glColor3f(0,0,1);
        glVertex3f(0., 0., 0.);
        glVertex3f(0., 0., 1.);
        glEnd();
        glPopAttrib();

        if (dl) {
            glEndList();
        }
    }
    if (dl) {
        glCallList(dl);
    }
}

/**
 * 
 */
static void
draw_unit_cube(double red, double green, double blue)
{
    /* Attributes */
    glPushAttrib(GL_CURRENT_BIT | GL_LIGHTING_BIT);
    glEnable(GL_LIGHTING);

    float ambient = 0.4;
    float diffuse = 0.6;
    float specular =  0.;
    float opacity = 1;
    int shininess = 20;
    float temp_color[4];
    /* ambient */
    temp_color[0] = red * ambient;
    temp_color[1] = green * ambient;
    temp_color[2] = blue * ambient;
    temp_color[3] = opacity;
    glMaterialfv(GL_FRONT, GL_AMBIENT, temp_color);
    /* diffuse */
    temp_color[0] = red * diffuse;
    temp_color[1] = green * diffuse;
    temp_color[2] = blue * diffuse;
    temp_color[3] = opacity;
    glMaterialfv(GL_FRONT, GL_DIFFUSE, temp_color );
    /* specular */
    temp_color[0] = red * specular;
    temp_color[1] = green * specular;
    temp_color[2] = blue * specular;
    temp_color[3] = opacity;
    glMaterialfv(GL_FRONT, GL_SPECULAR, temp_color);
    /* emission */
    temp_color[0] = temp_color[1] = temp_color[2] = 0;
    temp_color[3] = opacity;
    glMaterialfv(GL_FRONT, GL_EMISSION, temp_color );
    /* shininess */
    glMateriali(GL_FRONT, GL_SHININESS, shininess);

    static GLuint dl = 0;
    if (!dl) {
        dl = glGenLists(1);
        if (dl) {
            glNewList(dl, GL_COMPILE);
            DBG("Compiling unit cube display list\n");
        }

        glBegin(GL_QUADS);

        glNormal3f(0., 0., -1.);
        glVertex3f(0., 1., 0.);
        glVertex3f(0., 0., 0.);
        glVertex3f(1., 0., 0.);
        glVertex3f(1., 1., 0.);

        glNormal3f(0., -1., 0.);
        glVertex3f(0., 0., 0.);
        glVertex3f(0., 0., 1.);
        glVertex3f(1., 0., 1.);
        glVertex3f(1., 0., 0.);

        glNormal3f(1., 0., 0.);
        glVertex3f(1., 1., 0.);
        glVertex3f(1., 0., 0.);
        glVertex3f(1., 0., 1.);
        glVertex3f(1., 1., 1.);

        glNormal3f(0., 0., 1.);
        glVertex3f(1., 1., 1.);
        glVertex3f(1., 0., 1.);
        glVertex3f(0., 0., 1.);
        glVertex3f(0., 1., 1.);

        glNormal3f(-1., 0., 0.);
        glVertex3f(0., 1., 1.);
        glVertex3f(0., 0., 1.);
        glVertex3f(0., 0., 0.);
        glVertex3f(0., 1., 0.);

        glNormal3f(0., 1., 0.);
        glVertex3f(1., 1., 1.);
        glVertex3f(0., 1., 1.);
        glVertex3f(0., 1., 0.);
        glVertex3f(1., 1., 0.);

        glEnd();

        if (dl) {
            glEndList();
        }
    }
    if (dl) {
        glCallList(dl);
    }

    glPopAttrib();
}

/**
 * 
 */
static void
draw_wireframe_unit_cube(double red, double green, double blue)
{
    /* draw a set of lines denoting current triad (helpful for debug) */
    glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT);
    glDisable(GL_LIGHTING);
    glColor4d(red,green,blue,1);

    static GLuint dl = 0;
    if (!dl) {
        dl = glGenLists(1);
        if (dl) {
            glNewList(dl, GL_COMPILE);
            DBG("Compiling unit wireframe object display list\n");
        }

        glBegin(GL_LINES);
        /* bottom 4 */
        glVertex3f(0., 0., 0.);
        glVertex3f(1., 0., 0.);
        glVertex3f(1., 0., 0.);
        glVertex3f(1., 1., 0.);
        glVertex3f(1., 1., 0.);
        glVertex3f(0., 1., 0.);
        glVertex3f(0., 1., 0.);
        glVertex3f(0., 0., 0.);
        /* top 4 */
        glVertex3f(0., 0., 1.);
        glVertex3f(1., 0., 1.);
        glVertex3f(1., 0., 1.);
        glVertex3f(1., 1., 1.);
        glVertex3f(1., 1., 1.);
        glVertex3f(0., 1., 1.);
        glVertex3f(0., 1., 1.);
        glVertex3f(0., 0., 1.);
        /* side 4 */
        glVertex3f(0., 0., 0.);
        glVertex3f(0., 0., 1.);
        glVertex3f(1., 0., 0.);
        glVertex3f(1., 0., 1.);
        glVertex3f(1., 1., 0.);
        glVertex3f(1., 1., 1.);
        glVertex3f(0., 1., 0.);
        glVertex3f(0., 1., 1.);
        glEnd();

        if (dl) {
            glEndList();
        }
    }
    if (dl) {
        glCallList(dl);
    }

    glPopAttrib();
}

/*
 * Transform the OpenGL modelview matrix such that a unit cube maps to
 * the bounding box specified by min_v and max_v.
 */
static void
transform_bounding_box(const double min_v[3], const double max_v[3])
{
    glTranslated(min_v[0], min_v[1], min_v[2]);
    glScaled(max_v[0] - min_v[0], 
             max_v[1] - min_v[1], 
             max_v[2] - min_v[2]);
}


static void
draw_object(renderer_om_object_t *self, const om_object_t *object)
{
    /* get the model */
    GString *config_prefix = g_string_new("");
    g_string_printf(config_prefix, "object_wavefront.%d", object->object_type);
    char *name = get_char_from_id(object->object_type);

    gpointer found_model = NULL;

    GLuint *model_dl = NULL;

    // Look to see whether we've already added the model to the hash
    if(g_hash_table_lookup_extended(self->model_hash, name, NULL, &found_model))
        model_dl = (GLuint *)found_model;
    else{
        GLuint *new_model_dl = calloc(1,sizeof(GLuint));
        *new_model_dl = get_wavefront_model(name, self);        
        char *conf_path_copy = g_strdup(name);
        g_hash_table_insert(self->model_hash, conf_path_copy, new_model_dl);        
        model_dl = new_model_dl;
    }

    /* compute the rotation matrix, then transpose to get OpenGL
     * column-major matrix */
    double m[16], m_opengl[16];
    bot_quat_pos_to_matrix(object->orientation, object->pos, m);
    bot_matrix_transpose_4x4d(m, m_opengl);

    /* is the bounding box valid? */
    gboolean bbox_valid = (object->bbox_max[0] != object->bbox_min[0] &&
                           object->bbox_max[1] != object->bbox_min[1] &&
                           object->bbox_max[2] != object->bbox_min[2]);

    //our call doesnt have valid bbox 

    if (model_dl) {
        glPushMatrix();
        glMultMatrixd(m_opengl);

        if (bbox_valid && (self->draw_bbox || object->id==self->hover_id)) {
            glPushMatrix();
            transform_bounding_box(object->bbox_min, object->bbox_max);
            if (object->id==self->hover_id)
                draw_wireframe_unit_cube(1.0, 0.5, 0.83); /* ???? */
            else
                draw_wireframe_unit_cube(1, 0.5, 0); /* orange */
            glPopMatrix();
        }
        if (self->draw_unit_triads)
            draw_unit_triad();

        // draw the object 3D model
        draw_wavefront_model (*model_dl);
        glPopMatrix();
    }
    else if (bbox_valid) {
        /* no model but the bounding box is valid */
        glPushMatrix();
        glMultMatrixd(m_opengl);

        if (self->draw_unit_triads) {// && model_dl) 
            draw_wavefront_model(model_dl);///*self->gl_list[1]*/, 0,0,0,1,1,1);
            //draw_unit_triad();
        }

        //get_wavefront_model(self); 
        transform_bounding_box(object->bbox_min, object->bbox_max);
        //draw_unit_cube(1, 0.5, 0); /* orange */
        glPopMatrix();
    }
    else {
        /* let someone know that we are not rendering a object */
        ERR_ONCE("Error: unable to draw object of type '%s', object has no rwx "
                 "model or valid bounding box\n", config_prefix->str);
    }

    if (self->draw_object_ids) {
        char id[256];
        sprintf(id, "%"PRId64, object->id);
        glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT);
        glDisable(GL_LIGHTING);
        glColor4f(1,1,1,1);
        bot_gl_draw_text(object->pos, GLUT_BITMAP_HELVETICA_12, id,
                         0/*BOT_GL_DRAW_TEXT_DROP_SHADOW*/);
        glPopAttrib();
    }

    g_string_free(config_prefix, TRUE);
}

static void
on_object_list(const lcm_recv_buf_t *rbuf, const char *channel,
               const om_object_list_t *msg, void *user)
{
    renderer_om_object_t *self = (renderer_om_object_t*)user;

    /* copy lcm message data to local buffer and let draw update the display */
    g_mutex_lock(self->mutex);
    if (self->object_list)
        om_object_list_t_destroy(self->object_list);
    self->object_list = om_object_list_t_copy(msg);
    BotViewer *viewer = self->viewer; /* copy viewer to stack in case self is 
                                    * free'd between the unlock and call to 
                                    * viewer_request_redraw */
    g_mutex_unlock(self->mutex); /* not sure whether viewer_request_redraw 
                                  * calls this renderer's draw function before 
                                  * returning. therefore, I am releaseing the 
                                  * mutex here to avoid possibility of a 
                                  * deadlock */

    if (viewer)
        bot_viewer_request_redraw(viewer);
}

char *
objects_snapshot (renderer_om_object_t *self)
{
    GString *result = 
        g_string_new ("<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n");

    //convert this to bot trans
    double lat_lon_el[3];
    double theta;
    double rpy[3];
    
    g_string_append (result, "<objects>\n");

    int count = 0;
    
    // loop through the object list
    count = 0;
    if (self->object_list && self->object_list->num_objects) {
        for (int i=0; i<self->object_list->num_objects; i++) {
            om_object_t *o = self->object_list->objects + i;
            if (o->id == -1)
                continue;

            g_string_append (result,"    <item type=\"object\">\n");
            g_string_append_printf (result, 
                                    "        <id>%"PRId64"</id>\n", o->id);
            g_string_append_printf (result, 
                                    "        <type id=\"object_enum_t\">%d</type>\n", o->object_type);
            g_string_append_printf (result, 
                                    "        <position id=\"local-frame\">%lf %lf %lf</position>\n", 
                                    o->pos[0], o->pos[1], o->pos[2]);

            bot_quat_to_roll_pitch_yaw (o->orientation, rpy);
            theta = 0;// rpy[2] - gps_to_local.lat_lon_el_theta[3];
 
            //atrans_local_to_gps (atrans, o->pos, lat_lon_el, NULL);
            lat_lon_el[2] = 0.0;

            g_string_append_printf (result, 
                                    "        <position id=\"lat_lon_theta\">%lf %lf %lf</position>\n", 
                                    lat_lon_el[0], lat_lon_el[1], theta);

            g_string_append_printf (result, 
                                    "        <orientation id=\"local-frame\">%lf %lf %lf %lf</orientation>\n", 
                                    o->orientation[0], o->orientation[1], o->orientation[2], o->orientation[3]);
        
            g_string_append (result, "    </item>\n");
            count += 1;
        }
    }
    fprintf(stdout,"Writing %d generic objects\n", count);
    //globals_release_atrans (atrans);        
    g_string_append (result, "</objects>\n");
    return g_string_free (result, FALSE);
}

static void
on_load_button (GtkWidget *button, renderer_om_object_t *self)
{
    GtkWidget *dialog;
    dialog = gtk_file_chooser_dialog_new("Save Objects", NULL,
                                         GTK_FILE_CHOOSER_ACTION_SAVE,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
                                         NULL);
    
    if (self->last_save_filename)
        gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(dialog),
                                       self->last_save_filename);

    if (gtk_dialog_run (GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
        if (filename != NULL) {
            if (self->last_save_filename)
                g_free (self->last_save_filename);
            self->last_save_filename = g_strdup (filename);

            //send xml write command to the object simulator 

            om_xml_cmd_t msg; 
            msg.utime = bot_timestamp_now();
            msg.cmd_type = OM_XML_CMD_T_LOAD_FILE; 
            msg.path = strdup(filename);

            om_xml_cmd_t_publish(self->lcm, "XML_COMMAND", &msg);

            free (filename);
            free(msg.path);
        }
    }
   
    gtk_widget_destroy (dialog);
}


static void
on_save_button (GtkWidget *button, renderer_om_object_t *self)
{
    GtkWidget *dialog;
    dialog = gtk_file_chooser_dialog_new("Save Objects", NULL,
                                         GTK_FILE_CHOOSER_ACTION_SAVE,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
                                         NULL);
    
    if (self->last_save_filename)
        gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(dialog),
                                       self->last_save_filename);

    if (gtk_dialog_run (GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
        if (filename != NULL) {
            if (self->last_save_filename)
                g_free (self->last_save_filename);
            self->last_save_filename = g_strdup (filename);

            //send xml write command to the object simulator 

            om_xml_cmd_t msg; 
            msg.utime = bot_timestamp_now();
            msg.cmd_type = OM_XML_CMD_T_WRITE_FILE; 
            msg.path = strdup(filename);

            om_xml_cmd_t_publish(self->lcm, "XML_COMMAND", &msg);
           
            free (filename);
            free(msg.path);
        }
    }
   
    gtk_widget_destroy (dialog);
}

static void
renderer_om_object_draw(BotViewer *viewer, BotRenderer *renderer)
{
    // Needed for N810 viewer so the textures get rendered correctly.
    if (!viewer) glDisable(GL_COLOR_MATERIAL);

    renderer_om_object_t *self = renderer->user;

    g_mutex_lock(self->mutex);
    if (self->object_list && self->object_list->num_objects){

        /* set desired DL attributes */
        glPushAttrib(GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | 
                     GL_DEPTH_BUFFER_BIT | GL_ENABLE_BIT | 
                     GL_LIGHTING_BIT | GL_TRANSFORM_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_RESCALE_NORMAL);
        glShadeModel(GL_SMOOTH);
        glEnable(GL_LIGHTING);

        /* iterate through object lists, drawing each */
     
        if (self->object_list && self->object_list->num_objects) {
            for (int i = 0; i < self->object_list->num_objects; i++) {         
                om_object_t *object = self->object_list->objects + i;
                
                draw_object(self, self->object_list->objects + i);
                
                //add the drawing  call here 
		/*if ((object->object_type ==  OM_OBJECT_ENUM_T_OPERATOR && self->draw_people_detections) ||
		    object->object_type != ARLCM_OBJECT_ENUM_T_OPERATOR)
		    draw_object(self, self->object_list->objects + i);*/
            }
        }

        /* return to original attributes */
        glPopAttrib();
    }
    g_mutex_unlock(self->mutex);

    if (!viewer) glEnable(GL_COLOR_MATERIAL);
}

static void
renderer_om_object_destroy(BotRenderer *renderer)
{
    if (!renderer)
        return;

    renderer_om_object_t *self = renderer->user;
    if (!self)
        return;

    if (self->mutex)
        g_mutex_lock(self->mutex);
    
    /* stop listening to lcm */
    if (self->lcm) {
        if (self->object_lcm_hid)
            om_object_list_t_unsubscribe(self->lcm, 
                                            self->object_lcm_hid);
    }
    
    /* destory local copy of lcm data objects */
    if (self->object_list)
        om_object_list_t_destroy(self->object_list);
    
    if (self->last_save_filename)
        g_free (self->last_save_filename);

    /* release mutex */
    if (self->mutex) {
        g_mutex_unlock(self->mutex);
        g_mutex_free(self->mutex);
    }
    
    free(self);
}

static void 
on_param_widget_changed(BotGtkParamWidget *pw, const char *name, void *user)
{
    renderer_om_object_t *self = (renderer_om_object_t*)user;

    g_mutex_lock(self->mutex);
    self->draw_unit_triads = 
        bot_gtk_param_widget_get_bool(self->pw, PARAM_TRIADS);
    self->draw_bbox = 
        bot_gtk_param_widget_get_bool(self->pw, PARAM_BBOX);
    self->draw_object_ids = 
        bot_gtk_param_widget_get_bool(self->pw, PARAM_OBJECT_IDS);
    g_mutex_unlock(self->mutex);

    bot_viewer_request_redraw(self->viewer);
}

static void 
on_load_preferences(BotViewer *viewer, GKeyFile *keyfile, void *user)
{
    renderer_om_object_t *self = (renderer_om_object_t*)user;
    bot_gtk_param_widget_load_from_key_file(self->pw, keyfile, RENDERER_NAME);
}

static void 
on_save_preferences(BotViewer *viewer, GKeyFile *keyfile, void *user)
{
    renderer_om_object_t *self = (renderer_om_object_t*)user;
    bot_gtk_param_widget_save_to_key_file(self->pw, keyfile, RENDERER_NAME);
}


static om_object_t * 
find_closest_object(renderer_om_object_t *self, const double pos[3])

{
    double closest_dist=HUGE;
    om_object_t *closest_object=NULL;
    /* iterate through object list to find closest object to pos */
    if (self->object_list && self->object_list->num_objects) {
        for (int i = 0; i < self->object_list->num_objects; i++) {
            om_object_t *p = self->object_list->objects + i;  
            double dist = sqrt(bot_sq(p->pos[0] - pos[0]) + bot_sq(p->pos[1] - pos[1]));        
            if (closest_dist>dist) {
                closest_dist=dist;
                closest_object=p;
            }
        }
        
    }
    return closest_object;
}


static double pick_query(BotViewer *viewer, BotEventHandler *ehandler, 
                         const double ray_start[3], const double ray_dir[3])
{
    renderer_om_object_t *self = (renderer_om_object_t*) ehandler->user;
    if (!self->teleport_request)
       return -1;

    double pos[3]={0,0,0};
    geom_ray_z_plane_intersect_3d(POINT3D(ray_start), 
            POINT3D(ray_dir), 0, POINT2D(pos));

    g_mutex_lock(self->mutex);

    double closest_dist =HUGE;
    om_object_t *closest_object = find_closest_object(self,pos);
    if (closest_object) {
        double dist = sqrt(bot_sq(closest_object->pos[0] - pos[0]) + bot_sq(closest_object->pos[1] - pos[1]));
        if (dist<closest_dist) {
            self->hover_id = closest_object->id;
            closest_dist=dist;
        }
    }
    g_mutex_unlock(self->mutex);
    if (closest_dist !=HUGE) 
        return closest_dist;

    self->ehandler.hovering = 0;
    return -1;
}

static int mouse_press (BotViewer *viewer, BotEventHandler *ehandler,
                        const double ray_start[3], const double ray_dir[3], 
                        const GdkEventButton *event)
{
    renderer_om_object_t *self = (renderer_om_object_t*) ehandler->user;

    

    // only handle mouse button 1.
    if (self->teleport_request && (event->button == 1)) {   
        // find teleport object
        double pos[3]={0,0,0};
        geom_ray_z_plane_intersect_3d(POINT3D(ray_start), 
                                      POINT3D(ray_dir), 0, POINT2D(pos));

        g_mutex_lock(self->mutex);
        double closest_object_dist =HUGE;
 
        om_object_t *closest_object = find_closest_object(self,pos);
        if (closest_object) {
            closest_object_dist = sqrt(bot_sq(closest_object->pos[0] - pos[0]) + bot_sq(closest_object->pos[1] - pos[1]));
        }
        if (closest_object) {
            self->hover_id = closest_object->id;
            if (self->teleport_object)
                om_object_t_destroy(self->teleport_object);
            self->teleport_object = om_object_t_copy(closest_object);
        }
        g_mutex_unlock(self->mutex);
        //self->teleport_object = 1;
    }
    return 0;
}

static int mouse_release(BotViewer *viewer, BotEventHandler *ehandler,
                         const double ray_start[3], const double ray_dir[3], 
                         const GdkEventButton *event)
{
    renderer_om_object_t *self = (renderer_om_object_t*) ehandler->user;

    self->hover_id=0;
    self->teleport_request = 0;
    self->ehandler.picking = 0;
    if (self->teleport_object) {
        om_object_t_destroy(self->teleport_object);
        self->teleport_object=NULL;
    }

    return 0;
}

static int mouse_motion (BotViewer *viewer, BotEventHandler *ehandler,
                         const double ray_start[3], const double ray_dir[3], 
                         const GdkEventMotion *event)
{
    renderer_om_object_t *self = (renderer_om_object_t*) ehandler->user;
    
    /*const ViewerAuxData * aux_data = get_viewer_aux_data (viewer);
    if (!aux_data->simulation_flag||!self->teleport_request)
    return 0;*/
    if (!self->teleport_request)
        return 0;

    if (!self->teleport_object) {
        pick_query(viewer, ehandler, ray_start, ray_dir);
        return 0;
    }

    double pos[3];
    if (self->teleport_object) 
        memcpy(pos,self->teleport_object->pos, 3 * sizeof(double));

    //fprintf(stderr,"Pos : %f,%f,%f\n", pos[0], pos[1], pos[2]);

    double v = (pos[2] - ray_start[2]) / ray_dir[2];
    double xy[2];
    xy[0] = ray_start[0] + v * ray_dir[0];
    xy[1] = ray_start[1] + v * ray_dir[1];
    
    int shift = event->state & GDK_SHIFT_MASK;

    if (shift) {
        // rotate!
        double theta1 = atan2(xy[1] - pos[1], xy[0] - pos[0]);

        // local
        double dq[4] = { cos (theta1/2), 0, 0, sin(theta1/2) };
        if (self->teleport_object) 
            memcpy(self->teleport_object->orientation, dq, 4 * sizeof(double));

    } else {
        // translate
        if (self->teleport_object) {
            self->teleport_object->pos[0] = xy[0];
            self->teleport_object->pos[1] = xy[1];
        }
    }
    if (self->teleport_object) {
        om_object_list_t ol;
        ol.num_objects=1;
        ol.utime = bot_timestamp_now();
        self->teleport_object->utime=ol.utime;
        ol.objects = self->teleport_object;
        om_object_list_t_publish(self->lcm, "OBJECTS_UPDATE_RENDERER", &ol);
    }

    return 1;
}


static int key_press (BotViewer *viewer, BotEventHandler *ehandler, 
        const GdkEventKey *event)
{
    renderer_om_object_t *self = (renderer_om_object_t*) ehandler->user;

    if (event->keyval == 's' || event->keyval == 'S') {
        bot_viewer_request_pick(viewer, ehandler);
        self->teleport_request = 1;
        return 1;
    }
    if (event->keyval == GDK_Escape) {
        ehandler->picking = 0;
        self->teleport_request = 0;
        self->hover_id=0;
        if (self->teleport_object) {
            om_object_t_destroy(self->teleport_object);
            self->teleport_object=NULL;
        }
    }

    return 0;
}

BotRenderer*
renderer_om_object_new(BotViewer *viewer, BotParam * param)
{
    renderer_om_object_t *self = 
        (renderer_om_object_t*)calloc(1,sizeof(renderer_om_object_t));
    if (!self) {
        ERR("Error: renderer_om_object_new() failed to allocate self\n");
        goto fail;
    }

    self->renderer.draw = renderer_om_object_draw;
    self->renderer.destroy = renderer_om_object_destroy;
    self->renderer.user = self;
    self->pw = NULL;
    self->renderer.widget = NULL;
    self->renderer.name = RENDERER_NAME;
    self->renderer.enabled = 1;


    BotEventHandler *ehandler = &self->ehandler;
    ehandler->name = RENDERER_NAME;
    ehandler->enabled = 1;
    ehandler->pick_query = pick_query;
    ehandler->key_press = key_press;
    ehandler->hover_query = pick_query;
    ehandler->mouse_press = mouse_press;
    ehandler->mouse_release = mouse_release;
    ehandler->mouse_motion = mouse_motion;
    ehandler->user = self;


    /* global objects */
    self->viewer = viewer;
    //add bot param 
    self->param = param;

    self->lcm = bot_lcm_get_global (NULL);//globals_get_lcm();
    if (!self->lcm) {
        ERR("Error: renderer_om_object_new() failed to get global lcm "
            "object\n");
        goto fail;
    }

    /* mutex:
     * lock the mutex within the following functions:
     *   on_object_list
     *   on_param_widget_changed
     *   renderer_om_object_draw
     *   renderer_om_object_destroy
     */
    self->mutex = g_mutex_new();
    if (!self->mutex) {
        ERR("Error: renderer_om_object_new() failed to create the "
            "generic object renderer mutex\n");
        goto fail;
    }

    /* listen to object list */

    self->object_lcm_hid = om_object_list_t_subscribe(self->lcm, 
        "OBJECT_LIST", on_object_list, self);
    if (!self->object_lcm_hid) {
        ERR("Error: renderer_om_object_new() failed to subscribe to the "
            "'OBJECT_LIST' LCM channel\n");
        goto fail;
    }

    /* renderer options defaults */
    self->draw_unit_triads = DRAW_UNIT_TRIADS_DEFAULT;
    self->draw_bbox = DRAW_BBOX_DEFAULT;
    self->draw_object_ids = DRAW_OBJECT_IDS_DEFAULT;
    
    /*self->num_of_models = 5;
    self->gl_list = (GLuint *)calloc(self->num_of_models, sizeof(GLuint));

    

    for(int i=0; i < self->num_of_models; i++){        
        self->gl_list[i] = get_wavefront_model("chair", self);
        }*/

    self->model_hash = g_hash_table_new_full(g_str_hash, g_str_equal, 
                                             free, free);//bot_wavefront_model_destroy);
    //_rwx_conf_model_destroy);

    
    if (viewer) {
        self->pw = BOT_GTK_PARAM_WIDGET(bot_gtk_param_widget_new());
        self->renderer.widget = GTK_WIDGET(self->pw);

        /* setup parameter widget */
        bot_gtk_param_widget_add_booleans(self->pw, 0, PARAM_TRIADS, 
                                          self->draw_unit_triads, NULL);
        bot_gtk_param_widget_add_booleans(self->pw, 0, PARAM_BBOX, 
                                          self->draw_bbox, NULL);
        bot_gtk_param_widget_add_booleans(self->pw, 0, PARAM_OBJECT_IDS, 
                                          self->draw_object_ids, NULL);

        GtkWidget *save_button = gtk_button_new_with_label("Save to xml");
        gtk_box_pack_start (GTK_BOX(self->renderer.widget), save_button,
                            FALSE, FALSE, 0);
        g_signal_connect (G_OBJECT(save_button), "clicked",
                          G_CALLBACK(on_save_button), self);

        GtkWidget *load_button = gtk_button_new_with_label("Load from xml");
        gtk_box_pack_start (GTK_BOX(self->renderer.widget), load_button,
                            FALSE, FALSE, 0);
        g_signal_connect (G_OBJECT(load_button), "clicked",
                          G_CALLBACK(on_load_button), self);

        
        gtk_widget_show_all (self->renderer.widget);

        /* setup viewer signal callbacks */
        g_signal_connect(G_OBJECT(self->pw), "changed", 
                         G_CALLBACK(on_param_widget_changed), self);
        g_signal_connect(G_OBJECT(viewer), "load-preferences", 
                         G_CALLBACK(on_load_preferences), self);
        g_signal_connect(G_OBJECT(viewer), "save-preferences",
                         G_CALLBACK(on_save_preferences), self);
    }

    return &self->renderer;
 fail:
    renderer_om_object_destroy(&self->renderer);
    return NULL;
}




void
setup_renderer_generic_object(BotViewer *viewer, int render_priority, BotParam * param)
{
    BotRenderer *renderer = renderer_om_object_new(viewer, param);
    renderer_om_object_t *self = renderer->user;

    if (viewer && renderer &&self) {
        /* add renderer iff viewer exists */
        bot_viewer_add_renderer(viewer, renderer, render_priority);
        bot_viewer_add_event_handler(viewer, &self->ehandler, render_priority);
    }
}

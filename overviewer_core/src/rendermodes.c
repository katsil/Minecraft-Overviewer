/* 
 * This file is part of the Minecraft Overviewer.
 *
 * Minecraft Overviewer is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * Minecraft Overviewer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with the Overviewer.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "overviewer.h"
#include <string.h>
#include <stdarg.h>

/* this file defines render_primitives,
   a list of all render primitives, ending in NULL
   all of these will be available to the user, so DON'T include primitives
   that are only useful as a base for other primitives.
   
   this file is auto-generated by setup.py */
#include "primitives.h"

/* rendermode encapsulation */

/* helper to create a single primitive */
RenderPrimitive *render_primitive_create(PyObject *prim, RenderState *state) {
    RenderPrimitive *ret = NULL;
    RenderPrimitiveInterface *iface = NULL;
    unsigned int i;
    PyObject *pyname;
    const char* name;

    pyname = PyObject_GetAttrString(prim, "name");
    if (!pyname)
        return NULL;
    name = PyString_AsString(pyname);
    
    for (i = 0; render_primitives[i] != NULL; i++) {
        if (strcmp(render_primitives[i]->name, name) == 0) {
            iface = render_primitives[i];
            break;
        }
    }
    Py_DECREF(pyname);

    if (iface == NULL)
        return (RenderPrimitive *)PyErr_Format(PyExc_RuntimeError, "invalid primitive name: %s", name);
    
    ret = calloc(1, sizeof(RenderPrimitive));
    if (ret == NULL) {
        return (RenderPrimitive *)PyErr_Format(PyExc_RuntimeError, "Failed to alloc a render primitive");
    }
    
    if (iface->data_size > 0) {
        ret->primitive = calloc(1, iface->data_size);
        if (ret->primitive == NULL) {
            free(ret);
            return (RenderPrimitive *)PyErr_Format(PyExc_RuntimeError, "Failed to alloc render primitive data");
        }
    }
    
    ret->iface = iface;
    
    if (iface->start) {
        if (iface->start(ret->primitive, state, prim)) {
            free(ret->primitive);
            free(ret);
            return NULL;
        }
    }
    
    return ret;
}

RenderMode *render_mode_create(PyObject *mode, RenderState *state) {
    RenderMode *ret = NULL;
    PyObject *mode_fast = NULL;
    unsigned int i;
    
    mode_fast = PySequence_Fast(mode, "Mode is not a sequence type");
    if (!mode_fast)
        return NULL;

    ret = calloc(1, sizeof(RenderMode));
    ret->state = state;
    ret->num_primitives = PySequence_Length(mode);
    ret->primitives = calloc(ret->num_primitives, sizeof(RenderPrimitive*));
    for (i = 0; i < ret->num_primitives; i++) {
        PyObject *pyprim = PySequence_Fast_GET_ITEM(mode_fast, i);
        RenderPrimitive *prim = render_primitive_create(pyprim, state);
        
        if (!prim) {
            render_mode_destroy(ret);
            Py_DECREF(mode_fast);
            return NULL;
        }
        
        ret->primitives[i] = prim;
    }
    
    return ret;
}

void render_mode_destroy(RenderMode *self) {
    unsigned int i;
    
    for (i = 0; i < self->num_primitives; i++) {
        RenderPrimitive *prim = self->primitives[i];
        /* we may be destroying a half-constructed mode, so we need this
           check */
        if (prim) {
            if (prim->iface->finish) {
                prim->iface->finish(prim->primitive, self->state);
            }
            if (prim->primitive) {
                free(prim->primitive);
            }
            free(prim);
        }
    }
    free(self->primitives);
    free(self);
}

int render_mode_occluded(RenderMode *self, int x, int y, int z) {
    unsigned int i;
    int occluded = 0;
    for (i = 0; i < self->num_primitives; i++) {
        RenderPrimitive *prim = self->primitives[i];
        if (prim->iface->occluded) {
            occluded |= prim->iface->occluded(prim->primitive, self->state, x, y, z);
        }
        
        if (occluded)
            return occluded;
    }
    return occluded;
}

int render_mode_hidden(RenderMode *self, int x, int y, int z) {
    unsigned int i;
    int hidden = 0;
    for (i = 0; i < self->num_primitives; i++) {
        RenderPrimitive *prim = self->primitives[i];
        if (prim->iface->hidden) {
            hidden |= prim->iface->hidden(prim->primitive, self->state, x, y, z);
        }
        
        if (hidden)
            return hidden;
    }
    return hidden;
}

void render_mode_draw(RenderMode *self, PyObject *img, PyObject *mask, PyObject *mask_light) {
    unsigned int i;
    for (i = 0; i < self->num_primitives; i++) {
        RenderPrimitive *prim = self->primitives[i];
        if (prim->iface->draw) {
            prim->iface->draw(prim->primitive, self->state, img, mask, mask_light);
        }
    }
}

/* options parse helper */
int render_mode_parse_option(PyObject *support, const char *name, const char *format, ...) {
    va_list ap;
    PyObject *item, *dict;
    int ret;
    
    if (support == NULL || name == NULL)
        return 1;
    
    dict = PyObject_GetAttrString(support, "option_values");
    if (!dict)
        return 1;
    
    item = PyDict_GetItemString(dict, name);
    if (item == NULL) {
        Py_DECREF(dict);
        return 1;
    };
    
    /* make sure the item we're parsing is a tuple
       for VaParse to work correctly */
    if (!PyTuple_Check(item)) {
        item = PyTuple_Pack(1, item);
    } else {
        Py_INCREF(item);
    }
    
    va_start(ap, format);
    ret = PyArg_VaParse(item, format, ap);
    va_end(ap);
    
    Py_DECREF(item);
    Py_DECREF(dict);
    
    if (!ret) {
        PyObject *errtype, *errvalue, *errtraceback;
        const char *errstring;
        
        PyErr_Fetch(&errtype, &errvalue, &errtraceback);
        errstring = PyString_AsString(errvalue);
        
        PyErr_Format(PyExc_TypeError, "rendermode option \"%s\" has incorrect type (%s)", name, errstring);
        
        Py_DECREF(errtype);
        Py_DECREF(errvalue);
        Py_XDECREF(errtraceback);
    }
    
    return ret;
}

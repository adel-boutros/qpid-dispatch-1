/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <qpid/dispatch/python_embedded.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <qpid/dispatch.h>
#include "dispatch_private.h"
#include "router_private.h"
#include "entity_cache.h"

static qd_log_source_t *log_source = 0;
static PyObject        *pyRouter   = 0;
static PyObject        *pyTick     = 0;
static PyObject        *pyAdded    = 0;
static PyObject        *pyRemoved  = 0;
static PyObject        *pyLinkLost = 0;

typedef struct {
    PyObject_HEAD
    qd_router_t *router;
} RouterAdapter;


static PyObject *qd_add_router(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    const char    *address;
    int            router_maskbit;

    if (!PyArg_ParseTuple(args, "si", &address, &router_maskbit))
        return 0;

    qdr_core_add_router(router->router_core, address, router_maskbit);
    qd_tracemask_add_router(router->tracemask, address, router_maskbit);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_del_router(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int router_maskbit;

    if (!PyArg_ParseTuple(args, "i", &router_maskbit))
        return 0;

    qdr_core_del_router(router->router_core, router_maskbit);
    qd_tracemask_del_router(router->tracemask, router_maskbit);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_set_link(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;
    int            link_maskbit;

    if (!PyArg_ParseTuple(args, "ii", &router_maskbit, &link_maskbit))
        return 0;

    qdr_core_set_link(router->router_core, router_maskbit, link_maskbit);
    qd_tracemask_set_link(router->tracemask, router_maskbit, link_maskbit);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_remove_link(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;

    if (!PyArg_ParseTuple(args, "i", &router_maskbit))
        return 0;

    qdr_core_remove_link(router->router_core, router_maskbit);
    qd_tracemask_remove_link(router->tracemask, router_maskbit);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_set_next_hop(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;
    int            next_hop_maskbit;

    if (!PyArg_ParseTuple(args, "ii", &router_maskbit, &next_hop_maskbit))
        return 0;

    qdr_core_set_next_hop(router->router_core, router_maskbit, next_hop_maskbit);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_remove_next_hop(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;

    if (!PyArg_ParseTuple(args, "i", &router_maskbit))
        return 0;

    qdr_core_remove_next_hop(router->router_core, router_maskbit);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_set_cost(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;
    int            cost;

    if (!PyArg_ParseTuple(args, "ii", &router_maskbit, &cost))
        return 0;

    qdr_core_set_cost(router->router_core, router_maskbit, cost);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_set_valid_origins(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    int            router_maskbit;
    PyObject      *origin_list;
    Py_ssize_t     idx;
    char          *error = 0;

    if (!PyArg_ParseTuple(args, "iO", &router_maskbit, &origin_list))
        return 0;

    do {
        if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
            error = "Router bit mask out of range";
            break;
        }

        if (!PyList_Check(origin_list)) {
            error = "Expected List as argument 2";
            break;
        }

        Py_ssize_t    origin_count = PyList_Size(origin_list);
        qd_bitmask_t *core_bitmask = qd_bitmask(0);
        int           maskbit;

        for (idx = 0; idx < origin_count; idx++) {
            maskbit = PyInt_AS_LONG(PyList_GetItem(origin_list, idx));
            if (maskbit >= qd_bitmask_width() || maskbit < 0) {
                error = "Origin bit mask out of range";
                break;
            }
        }

        if (error == 0) {
            qd_bitmask_set_bit(core_bitmask, 0);  // This router is a valid origin for all destinations
            for (idx = 0; idx < origin_count; idx++) {
                maskbit = PyInt_AS_LONG(PyList_GetItem(origin_list, idx));
                qd_bitmask_set_bit(core_bitmask, maskbit);
            }
        } else {
            qd_bitmask_free(core_bitmask);
            break;
        }

        qdr_core_set_valid_origins(router->router_core, router_maskbit, core_bitmask);
    } while (0);

    if (error) {
        PyErr_SetString(PyExc_Exception, error);
        return 0;
    }

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_map_destination(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    const char    *addr_string;
    int            maskbit;

    if (!PyArg_ParseTuple(args, "si", &addr_string, &maskbit))
        return 0;

    if (maskbit >= qd_bitmask_width() || maskbit < 0) {
        PyErr_SetString(PyExc_Exception, "Router bit mask out of range");
        return 0;
    }

    qdr_core_map_destination(router->router_core, maskbit, addr_string);

    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject* qd_unmap_destination(PyObject *self, PyObject *args)
{
    RouterAdapter *adapter = (RouterAdapter*) self;
    qd_router_t   *router  = adapter->router;
    const char    *addr_string;
    int            maskbit;

    if (!PyArg_ParseTuple(args, "si", &addr_string, &maskbit))
        return 0;

    if (maskbit >= qd_bitmask_width() || maskbit < 0) {
        PyErr_SetString(PyExc_Exception, "Router bit mask out of range");
        return 0;
    }

    qdr_core_unmap_destination(router->router_core, maskbit, addr_string);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject* qd_get_agent(PyObject *self, PyObject *args) {
    RouterAdapter *adapter = (RouterAdapter*) self;
    PyObject *agent = adapter->router->qd->agent;
    if (agent) {
        Py_INCREF(agent);
        return agent;
    }
    Py_RETURN_NONE;
}

static PyMethodDef RouterAdapter_methods[] = {
    {"add_router",          qd_add_router,        METH_VARARGS, "A new remote/reachable router has been discovered"},
    {"del_router",          qd_del_router,        METH_VARARGS, "We've lost reachability to a remote router"},
    {"set_link",            qd_set_link,          METH_VARARGS, "Set the link for a neighbor router"},
    {"remove_link",         qd_remove_link,       METH_VARARGS, "Remove the link for a neighbor router"},
    {"set_next_hop",        qd_set_next_hop,      METH_VARARGS, "Set the next hop for a remote router"},
    {"remove_next_hop",     qd_remove_next_hop,   METH_VARARGS, "Remove the next hop for a remote router"},
    {"set_cost",            qd_set_cost,          METH_VARARGS, "Set the cost to reach a remote router"},
    {"set_valid_origins",   qd_set_valid_origins, METH_VARARGS, "Set the valid origins for a remote router"},
    {"map_destination",     qd_map_destination,   METH_VARARGS, "Add a newly discovered destination mapping"},
    {"unmap_destination",   qd_unmap_destination, METH_VARARGS, "Delete a destination mapping"},
    {"get_agent",           qd_get_agent,         METH_VARARGS, "Get the management agent"},
    {0, 0, 0, 0}
};

static PyTypeObject RouterAdapterType = {
    PyObject_HEAD_INIT(0)
    0,                         /* ob_size*/
    "dispatch.RouterAdapter",  /* tp_name*/
    sizeof(RouterAdapter),     /* tp_basicsize*/
    0,                         /* tp_itemsize*/
    0,                         /* tp_dealloc*/
    0,                         /* tp_print*/
    0,                         /* tp_getattr*/
    0,                         /* tp_setattr*/
    0,                         /* tp_compare*/
    0,                         /* tp_repr*/
    0,                         /* tp_as_number*/
    0,                         /* tp_as_sequence*/
    0,                         /* tp_as_mapping*/
    0,                         /* tp_hash */
    0,                         /* tp_call*/
    0,                         /* tp_str*/
    0,                         /* tp_getattro*/
    0,                         /* tp_setattro*/
    0,                         /* tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /* tp_flags*/
    "Dispatch Router Adapter", /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    RouterAdapter_methods,     /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                         /* tp_init */
    0,                         /* tp_alloc */
    0,                         /* tp_new */
    0,                         /* tp_free */
    0,                         /* tp_is_gc */
    0,                         /* tp_bases */
    0,                         /* tp_mro */
    0,                         /* tp_cache */
    0,                         /* tp_subclasses */
    0,                         /* tp_weaklist */
    0,                         /* tp_del */
    0                          /* tp_version_tag */
};


static void qd_router_mobile_added(void *context, const char *address_hash)
{
    qd_router_t *router = (qd_router_t*) context;
    PyObject    *pArgs;
    PyObject    *pValue;

    if (pyAdded && router->router_mode == QD_ROUTER_MODE_INTERIOR) {
        qd_python_lock_state_t lock_state = qd_python_lock();
        pArgs = PyTuple_New(1);
        PyTuple_SetItem(pArgs, 0, PyString_FromString(address_hash));
        pValue = PyObject_CallObject(pyAdded, pArgs);
        qd_error_py();
        Py_DECREF(pArgs);
        Py_XDECREF(pValue);
        qd_python_unlock(lock_state);
    }
}


static void qd_router_mobile_removed(void *context, const char *address_hash)
{
    qd_router_t *router = (qd_router_t*) context;
    PyObject    *pArgs;
    PyObject    *pValue;

    if (pyRemoved && router->router_mode == QD_ROUTER_MODE_INTERIOR) {
        qd_python_lock_state_t lock_state = qd_python_lock();
        pArgs = PyTuple_New(1);
        PyTuple_SetItem(pArgs, 0, PyString_FromString(address_hash));
        pValue = PyObject_CallObject(pyRemoved, pArgs);
        qd_error_py();
        Py_DECREF(pArgs);
        Py_XDECREF(pValue);
        qd_python_unlock(lock_state);
    }
}


static void qd_router_link_lost(void *context, int link_mask_bit)
{
    qd_router_t *router = (qd_router_t*) context;
    PyObject    *pArgs;
    PyObject    *pValue;

    if (pyRemoved && router->router_mode == QD_ROUTER_MODE_INTERIOR) {
        qd_python_lock_state_t lock_state = qd_python_lock();
        pArgs = PyTuple_New(1);
        PyTuple_SetItem(pArgs, 0, PyInt_FromLong((long) link_mask_bit));
        pValue = PyObject_CallObject(pyLinkLost, pArgs);
        qd_error_py();
        Py_DECREF(pArgs);
        Py_XDECREF(pValue);
        qd_python_unlock(lock_state);
    }
}


qd_error_t qd_router_python_setup(qd_router_t *router)
{
    qd_error_clear();
    log_source = qd_log_source("ROUTER");

    qdr_core_route_table_handlers(router->router_core,
                                  router,
                                  qd_router_mobile_added,
                                  qd_router_mobile_removed,
                                  qd_router_link_lost);

    //
    // If we are not operating as an interior router, don't start the
    // router module.
    //
    if (router->router_mode != QD_ROUTER_MODE_INTERIOR)
        return QD_ERROR_NONE;

    PyObject *pDispatchModule = qd_python_module();
    RouterAdapterType.tp_new = PyType_GenericNew;
    PyType_Ready(&RouterAdapterType);
    QD_ERROR_PY_RET();

    PyTypeObject *raType = &RouterAdapterType;
    Py_INCREF(raType);
    PyModule_AddObject(pDispatchModule, "RouterAdapter", (PyObject*) &RouterAdapterType);

    //
    // Attempt to import the Python Router module
    //
    PyObject* pId;
    PyObject* pArea;
    PyObject* pMaxRouters;
    PyObject* pModule;
    PyObject* pClass;
    PyObject* pArgs;

    pModule = PyImport_ImportModule("qpid_dispatch_internal.router"); QD_ERROR_PY_RET();
    pClass = PyObject_GetAttrString(pModule, "RouterEngine");
    Py_DECREF(pModule);
    QD_ERROR_PY_RET();

    PyObject *adapterType     = PyObject_GetAttrString(pDispatchModule, "RouterAdapter");  QD_ERROR_PY_RET();
    PyObject *adapterInstance = PyObject_CallObject(adapterType, 0); QD_ERROR_PY_RET();

    ((RouterAdapter*) adapterInstance)->router = router;

    //
    // Constructor Arguments for RouterEngine
    //
    pArgs = PyTuple_New(4);

    // arg 0: adapter instance
    PyTuple_SetItem(pArgs, 0, adapterInstance);

    // arg 1: router_id
    pId = PyString_FromString(router->router_id);
    PyTuple_SetItem(pArgs, 1, pId);

    // arg 2: area_id
    pArea = PyString_FromString(router->router_area);
    PyTuple_SetItem(pArgs, 2, pArea);

    // arg 3: max_routers
    pMaxRouters = PyInt_FromLong((long) qd_bitmask_width());
    PyTuple_SetItem(pArgs, 3, pMaxRouters);

    //
    // Instantiate the router
    //
    pyRouter = PyInstance_New(pClass, pArgs, 0);
    Py_DECREF(pArgs);
    Py_DECREF(adapterType);
    QD_ERROR_PY_RET();

    pyTick = PyObject_GetAttrString(pyRouter, "handleTimerTick"); QD_ERROR_PY_RET();
    pyAdded = PyObject_GetAttrString(pyRouter, "addressAdded"); QD_ERROR_PY_RET();
    pyRemoved = PyObject_GetAttrString(pyRouter, "addressRemoved"); QD_ERROR_PY_RET();
    pyLinkLost = PyObject_GetAttrString(pyRouter, "linkLost"); QD_ERROR_PY_RET();
    return qd_error_code();
}

void qd_router_python_free(qd_router_t *router) {
    // empty
}


qd_error_t qd_pyrouter_tick(qd_router_t *router)
{
    qd_error_clear();
    qd_error_t err = QD_ERROR_NONE;

    PyObject *pArgs;
    PyObject *pValue;

    if (pyTick && router->router_mode == QD_ROUTER_MODE_INTERIOR) {
        qd_python_lock_state_t lock_state = qd_python_lock();
        pArgs  = PyTuple_New(0);
        pValue = PyObject_CallObject(pyTick, pArgs);
        Py_DECREF(pArgs);
        Py_XDECREF(pValue);
        err = qd_error_py();
        qd_python_unlock(lock_state);
    }
    return err;
}


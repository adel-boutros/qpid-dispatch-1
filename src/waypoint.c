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

#include "waypoint_private.h"
#include "dispatch_private.h"
#include "router_private.h"
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/connection_manager.h>
#include <memory.h>
#include <stdio.h>

typedef struct qd_waypoint_ref_t     qd_waypoint_ref_t;
typedef struct qd_waypoint_context_t qd_waypoint_context_t;

struct qd_waypoint_ref_t {
    DEQ_LINKS(qd_waypoint_ref_t);
    qd_waypoint_t *wp;
};

DEQ_DECLARE(qd_waypoint_ref_t, qd_waypoint_ref_list_t);

struct qd_waypoint_context_t {
    qd_waypoint_ref_list_t refs;
};


static void qd_waypoint_visit_sink_LH(qd_dispatch_t *qd, qd_waypoint_t *wp)
{
    qd_router_t  *router = qd->router;
    qd_address_t *addr   = wp->in_address;
    char          unused;

    //
    // If the waypoint has no in-address, look it up in the hash table or create
    // a new one and put it in the hash table.
    //
    if (!addr) {
        //
        // Compose the phased-address and search the routing table for the address.
        // If it's not found, add it to the table but leave the link/router linkages empty.
        //
        qd_field_iterator_t *iter = qd_field_iterator_string(wp->name, ITER_VIEW_ADDRESS_HASH);
        qd_field_iterator_set_phase(iter, wp->in_phase);
        qd_hash_retrieve(router->addr_hash, iter, (void*) &addr);

        if (!addr) {
            addr = new_qd_address_t();
            memset(addr, 0, sizeof(qd_address_t));
            DEQ_ITEM_INIT(addr);
            DEQ_INIT(addr->rlinks);
            DEQ_INIT(addr->rnodes);
            qd_hash_insert(router->addr_hash, iter, addr, &addr->hash_handle);
            DEQ_INSERT_TAIL(router->addrs, addr);
            addr->waypoint  = true;
            addr->semantics = router_semantics_for_addr(router, iter, wp->in_phase, &unused);
        }

        wp->in_address = addr;
        qd_field_iterator_free(iter);
    }

    if (!wp->connected)
        qd_connection_manager_start_on_demand(qd, wp->connector);
    else if (!wp->out_link) {
        wp->out_link = qd_link(router->node, wp->connection, QD_OUTGOING, wp->name);
        pn_terminus_set_address(qd_link_target(wp->out_link), wp->name);

        qd_router_link_t *rlink = new_qd_router_link_t();
        DEQ_ITEM_INIT(rlink);
        rlink->link_type      = QD_LINK_WAYPOINT;
        rlink->link_direction = QD_OUTGOING;
        rlink->owning_addr    = addr;
        rlink->waypoint       = wp;
        rlink->link           = wp->out_link;
        rlink->connected_link = 0;
        rlink->peer_link      = 0;
        rlink->ref            = 0;
        rlink->target         = 0;
        DEQ_INIT(rlink->event_fifo);
        DEQ_INIT(rlink->msg_fifo);
        DEQ_INSERT_TAIL(router->links, rlink);
        qd_link_set_context(wp->out_link, rlink);
        qd_router_add_link_ref_LH(&addr->rlinks, rlink);

        if (DEQ_SIZE(addr->rlinks) == 1) {
            qd_field_iterator_t *iter = qd_field_iterator_string(wp->name, ITER_VIEW_ADDRESS_HASH);
            qd_field_iterator_set_phase(iter, wp->in_phase);
            qd_router_mobile_added(router, iter);
            qd_field_iterator_free(iter);
        }

        pn_link_open(qd_link_pn(wp->out_link));
        qd_link_activate(wp->out_link);

        qd_log(router->log_source, QD_LOG_DEBUG, "Added outgoing link for waypoint: %s", wp->name);
    }
}


static void qd_waypoint_visit_source_LH(qd_dispatch_t *qd, qd_waypoint_t *wp)
{
    qd_router_t  *router = qd->router;
    qd_address_t *addr   = wp->out_address;
    char          unused;

    //
    // If the waypoint has no out-address, look it up in the hash table or create
    // a new one and put it in the hash table.
    //
    if (!addr) {
        //
        // Compose the phased-address and search the routing table for the address.
        // If it's not found, add it to the table but leave the link/router linkages empty.
        //
        qd_field_iterator_t *iter = qd_field_iterator_string(wp->name, ITER_VIEW_ADDRESS_HASH);
        qd_field_iterator_set_phase(iter, wp->out_phase);
        qd_hash_retrieve(router->addr_hash, iter, (void*) &addr);

        if (!addr) {
            addr = new_qd_address_t();
            memset(addr, 0, sizeof(qd_address_t));
            DEQ_ITEM_INIT(addr);
            DEQ_INIT(addr->rlinks);
            DEQ_INIT(addr->rnodes);
            qd_hash_insert(router->addr_hash, iter, addr, &addr->hash_handle);
            DEQ_INSERT_TAIL(router->addrs, addr);
            addr->waypoint  = true;
            addr->semantics = router_semantics_for_addr(router, iter, wp->out_phase, &unused);
        }

        wp->out_address = addr;
        qd_field_iterator_free(iter);
    }

    if (!wp->connected)
        qd_connection_manager_start_on_demand(qd, wp->connector);
    else if (!wp->in_link) {
        wp->in_link = qd_link(router->node, wp->connection, QD_INCOMING, wp->name);
        pn_terminus_set_address(qd_link_source(wp->in_link), wp->name);

        qd_router_link_t *rlink = new_qd_router_link_t();
        DEQ_ITEM_INIT(rlink);
        rlink->link_type      = QD_LINK_WAYPOINT;
        rlink->link_direction = QD_INCOMING;
        rlink->owning_addr    = addr;
        rlink->waypoint       = wp;
        rlink->link           = wp->in_link;
        rlink->connected_link = 0;
        rlink->peer_link      = 0;
        rlink->ref            = 0;
        rlink->target         = 0;
        DEQ_INIT(rlink->event_fifo);
        DEQ_INIT(rlink->msg_fifo);
        DEQ_INSERT_TAIL(router->links, rlink);
        qd_link_set_context(wp->in_link, rlink);

        pn_link_open(qd_link_pn(wp->in_link));
        qd_link_activate(wp->in_link);

        qd_log(router->log_source, QD_LOG_DEBUG, "Added incoming link for waypoint: %s", wp->name);
    }

    if (DEQ_SIZE(addr->rlinks) + DEQ_SIZE(addr->rnodes) > 0) {
        //
        // CASE: This address has reachable destinations in the network.
        //       If there is no inbound link from the waypoint source,
        //       establish one and issue credit.
        //
        pn_link_flow(qd_link_pn(wp->in_link), 1);
        qd_link_activate(wp->in_link);
        qd_log(router->log_source, QD_LOG_DEBUG, "Added credit for incoming link for waypoint: %s", wp->name);
    } else {
        //
        // CASE: This address has no reachable destinations in the network.
        //
    }
}


static void qd_waypoint_visit_LH(qd_dispatch_t *qd, qd_waypoint_t *wp)
{
    if (wp->in_phase)
        qd_waypoint_visit_sink_LH(qd, wp);
    if (wp->out_phase)
        qd_waypoint_visit_source_LH(qd, wp);
}


static void qd_waypoint_activate_in_phase_LH(qd_dispatch_t *qd, qd_waypoint_t *wp)
{
    //
    // Activate the "Sink" side of this waypoint.
    //

    //
    // If the in-phase is null, then no in-phase was configured for this waypoint.
    //
    if (wp->in_phase == '\0')
        return;

    //
    // Since the waypoint has an in-phase, we wish to advertise the waypoint's address
    // for senders to send to.  We can't do this until a connection and link are set up
    // to the waypoint.  Start the on-demand connector.
    //
    qd_connection_manager_start_on_demand(qd, wp->connector);
}


static void qd_waypoint_activate_out_phase_LH(qd_dispatch_t *qd, qd_waypoint_t *wp)
{
    //
    // Activate the "Source" side of this waypoint.
    //

    // This function intentionally left blank
}


void qd_waypoint_activate_all(qd_dispatch_t *qd)
{
    qd_router_t   *router = qd->router;
    qd_waypoint_t *wp;

    sys_mutex_lock(router->lock);
    for (wp = DEQ_HEAD(router->waypoints); wp; wp = DEQ_NEXT(wp)) {
        //
        // Associate the waypoint with its named on-demand connector and for every
        // on-demand connector, create a list of associated waypoints.
        //
        if (!wp->connector) {
            wp->connector = qd_connection_manager_find_on_demand(qd, wp->connector_name);
            if (!wp->connector) {
                qd_log(qd->router->log_source, QD_LOG_ERROR,
                       "In waypoint '%s', on-demand connector '%s' not found",
                       wp->name, wp->connector_name);
                continue;
            }

            qd_waypoint_context_t *context =
                (qd_waypoint_context_t*) qd_config_connector_context(wp->connector);
            if (!context) {
                context = NEW(qd_waypoint_context_t);
                DEQ_INIT(context->refs);
                qd_config_connector_set_context(wp->connector, context);
            }
            qd_waypoint_ref_t *ref = NEW(qd_waypoint_ref_t);
            DEQ_ITEM_INIT(ref);
            ref->wp = wp;
            DEQ_INSERT_TAIL(context->refs, ref);
        }
    }

    for (wp = DEQ_HEAD(router->waypoints); wp; wp = DEQ_NEXT(wp)) {
        qd_waypoint_activate_in_phase_LH(qd, wp);
        qd_waypoint_activate_out_phase_LH(qd, wp);
        qd_waypoint_visit_LH(qd, wp);
    }
    sys_mutex_unlock(router->lock);
}


void qd_waypoint_connection_opened(qd_dispatch_t *qd, qd_config_connector_t *cc, qd_connection_t *conn)
{
    qd_waypoint_context_t *context = (qd_waypoint_context_t*) qd_config_connector_context(cc);

    qd_log(qd->router->log_source, QD_LOG_INFO, "On-demand connector opened: %s",
           qd_config_connector_name(cc));

    sys_mutex_lock(qd->router->lock);
    qd_waypoint_ref_t *ref = DEQ_HEAD(context->refs);
    while (ref) {
        ref->wp->connected  = true;
        ref->wp->connection = conn;
        qd_waypoint_visit_LH(qd, ref->wp);
        ref = DEQ_NEXT(ref);
    }
    sys_mutex_unlock(qd->router->lock);
}


void qd_waypoint_new_incoming_link(qd_dispatch_t *qd, qd_waypoint_t *wp, qd_link_t *link)
{
}


void qd_waypoint_new_outgoing_link(qd_dispatch_t *qd, qd_waypoint_t *wp, qd_link_t *link)
{
}


void qd_waypoint_link_closed(qd_dispatch_t *qd, qd_waypoint_t *wp, qd_link_t *link)
{
}


void qd_waypoint_address_updated_LH(qd_dispatch_t *qd, qd_address_t *addr)
{
    qd_waypoint_t *wp = DEQ_HEAD(qd->router->waypoints);
    while (wp) {
        if (wp->out_address == addr)
            qd_waypoint_visit_LH(qd, wp);
        wp = DEQ_NEXT(wp);
    }
}


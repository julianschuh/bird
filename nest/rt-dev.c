/*
 *	BIRD -- Direct Device Routes
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

/**
 * DOC: Direct
 *
 * The Direct protocol works by converting all ifa_notify() events it receives
 * to rte_update() calls for the corresponding network.
 */

#undef LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "nest/rt-dev.h"
#include "conf/conf.h"
#include "lib/resource.h"
#include "lib/string.h"


static void
dev_ifa_notify(struct proto *P, uint flags, struct ifa *ad)
{
  struct rt_dev_proto *p = (void *) P;
  struct rt_dev_config *cf = (void *) P->cf;
  struct channel *c;

  if (!EMPTY_LIST(cf->iface_list) &&
      !iface_patt_find(&cf->iface_list, ad->iface, ad->iface->addr))
    /* Empty list is automatically treated as "*" */
    return;

  if (ad->flags & IA_SECONDARY)
    return;

  if (ad->scope <= SCOPE_LINK)
    return;

  if (ad->prefix.type == NET_IP4)
    c = p->ip4_channel;
  else if (ad->prefix.type == NET_IP6)
    c = p->ip6_channel;
  else
    return;

  if (!c)
    return;

  if (flags & IF_CHANGE_DOWN)
    {
      DBG("dev_if_notify: %s:%I going down\n", ad->iface->name, ad->ip);

      /* Use iface ID as local source ID */
      struct rte_src *src = rt_get_source(P, ad->iface->index);
      rte_update2(c, &ad->prefix, NULL, src);
    }
  else if (flags & IF_CHANGE_UP)
    {
      rta *a;
      rte *e;

      DBG("dev_if_notify: %s:%I going up\n", ad->iface->name, ad->ip);

      if (cf->check_link && !(ad->iface->flags & IF_LINK_UP))
	return;

      /* Use iface ID as local source ID */
      struct rte_src *src = rt_get_source(P, ad->iface->index);

      rta a0 = {
	.src = src,
	.source = RTS_DEVICE,
	.scope = SCOPE_UNIVERSE,
	.dest = RTD_UNICAST,
	.nh.iface = ad->iface,
      };

      a = rta_lookup(&a0);
      e = rte_get_temp(a);
      e->pflags = 0;
      rte_update2(c, &ad->prefix, e, src);
    }
}

static void
dev_if_notify(struct proto *p, uint c, struct iface *iface)
{
  struct rt_dev_config *cf = (void *) p->cf;

  if (c & (IF_CHANGE_UP | IF_CHANGE_DOWN))
    return;

  if ((c & IF_CHANGE_LINK) && cf->check_link)
  {
    uint ac = (iface->flags & IF_LINK_UP) ? IF_CHANGE_UP : IF_CHANGE_DOWN;

    struct ifa *a;
    WALK_LIST(a, iface->addrs)
      dev_ifa_notify(p, ac, a);
  }
}


static struct proto *
dev_init(struct proto_config *CF)
{
  struct proto *P = proto_new(CF);
  struct rt_dev_proto *p = (void *) P;
  // struct rt_dev_config *cf = (void *) CF;

  proto_configure_channel(P, &p->ip4_channel, proto_cf_find_channel(CF, NET_IP4));
  proto_configure_channel(P, &p->ip6_channel, proto_cf_find_channel(CF, NET_IP6));

  P->if_notify = dev_if_notify;
  P->ifa_notify = dev_ifa_notify;

  return P;
}

static int
dev_reconfigure(struct proto *P, struct proto_config *CF)
{
  struct rt_dev_proto *p = (void *) P;
  struct rt_dev_config *o = (void *) P->cf;
  struct rt_dev_config *n = (void *) CF;

  if (!iface_patts_equal(&o->iface_list, &n->iface_list, NULL) ||
      (o->check_link != n->check_link))
    return 0;

  return
    proto_configure_channel(P, &p->ip4_channel, proto_cf_find_channel(CF, NET_IP4)) &&
    proto_configure_channel(P, &p->ip6_channel, proto_cf_find_channel(CF, NET_IP6));

  return 1;
}

static void
dev_copy_config(struct proto_config *dest, struct proto_config *src)
{
  struct rt_dev_config *d = (void *) dest;
  struct rt_dev_config *s = (void *) src;

  /*
   * We copy iface_list as ifaces can be shared by more direct protocols.
   * Copy suffices to be is shallow, because new nodes can be added, but
   * old nodes cannot be modified (although they contain internal lists).
   */
  cfg_copy_list(&d->iface_list, &s->iface_list, sizeof(struct iface_patt));

  d->check_link = s->check_link;
}

struct protocol proto_device = {
  .name = 		"Direct",
  .template = 		"direct%d",
  .preference =		DEF_PREF_DIRECT,
  .channel_mask =	NB_IP,
  .proto_size =		sizeof(struct rt_dev_proto),
  .config_size =	sizeof(struct rt_dev_config),
  .init = 		dev_init,
  .reconfigure = 	dev_reconfigure,
  .copy_config = 	dev_copy_config
};

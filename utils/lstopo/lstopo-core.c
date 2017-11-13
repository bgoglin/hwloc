/*
 * Copyright © 2009-2017 Inria.  All rights reserved.
 * Copyright © 2009-2012, 2015 Université Bordeaux
 * See COPYING in top-level directory.
 */

#include "lstopo.h"

void
lstopo_init(struct lstopo_output *loutput)
{
  unsigned i;

  loutput->methods = NULL;

  loutput->overwrite = 0;

  loutput->logical = -1;
  loutput->verbose_mode = LSTOPO_VERBOSE_MODE_DEFAULT;
  loutput->ignore_pus = 0;
  loutput->ignore_numanodes = 0;
  loutput->collapse = 1;
  loutput->pid_number = -1;
  loutput->pid = 0;
  loutput->need_pci_domain = 0;

  loutput->export_synthetic_flags = 0;
  loutput->export_xml_flags = 0;

  loutput->legend = 1;
  loutput->legend_append = NULL;
  loutput->legend_append_nr = 0;

  loutput->show_distances_only = 0;
  loutput->show_only = HWLOC_OBJ_TYPE_NONE;
  loutput->show_cpuset = 0;
  loutput->show_taskset = 0;

  loutput->backend_data = NULL;
  loutput->methods = NULL;

  loutput->no_half_lines = 0;
  loutput->plain_children_order = 0;
  loutput->fontsize = 10;
  loutput->gridsize = 10;
  for(i=HWLOC_OBJ_TYPE_MIN; i<HWLOC_OBJ_TYPE_MAX; i++)
    loutput->force_orient[i] = LSTOPO_ORIENT_NONE;
  loutput->force_orient[HWLOC_OBJ_PU] = LSTOPO_ORIENT_HORIZ;
  for(i=HWLOC_OBJ_L1CACHE; i<=HWLOC_OBJ_L3ICACHE; i++)
    loutput->force_orient[i] = LSTOPO_ORIENT_HORIZ;
  loutput->force_orient[HWLOC_OBJ_NUMANODE] = LSTOPO_ORIENT_HORIZ;
  for(i=HWLOC_OBJ_TYPE_MIN; i<HWLOC_OBJ_TYPE_MAX; i++) {
    loutput->show_indexes[i] = 1;
    loutput->show_attrs[i] = 1;
    loutput->show_text[i] = 1;
  }

  loutput->drawing_callback = NULL;
}

FILE *open_output(const char *filename, int overwrite)
{
  const char *extn;
  struct stat st;

  if (!filename || !strcmp(filename, "-"))
    return stdout;

  extn = strrchr(filename, '.');
  if (filename[0] == '-' && extn == filename + 1)
    return stdout;

  if (!stat(filename, &st) && !overwrite) {
    errno = EEXIST;
    return NULL;
  }

  return fopen(filename, "w");
}

static void
lstopo_populate_userdata(hwloc_obj_t parent)
{
  hwloc_obj_t child;
  struct lstopo_obj_userdata *save = malloc(sizeof(*save));

  save->common.buffer = NULL; /* so that it is ignored on XML export */
  save->common.next = parent->userdata;
  save->pci_collapsed = 0;
  parent->userdata = save;

  for_each_child(child, parent)
    lstopo_populate_userdata(child);
  for_each_memory_child(child, parent)
    lstopo_populate_userdata(child);
  for_each_io_child(child, parent)
    lstopo_populate_userdata(child);
  for_each_misc_child(child, parent)
    lstopo_populate_userdata(child);
}

static void
lstopo_destroy_userdata(hwloc_obj_t parent)
{
  hwloc_obj_t child;
  struct lstopo_obj_userdata *save = parent->userdata;

  if (save) {
    parent->userdata = save->common.next;
    free(save);
  }

  for_each_child(child, parent)
    lstopo_destroy_userdata(child);
  for_each_memory_child(child, parent)
    lstopo_destroy_userdata(child);
  for_each_io_child(child, parent)
    lstopo_destroy_userdata(child);
  for_each_misc_child(child, parent)
    lstopo_destroy_userdata(child);
}

int
lstopo_check_pci_domains(hwloc_topology_t topology)
{
  hwloc_obj_t obj;

  /* check PCI devices for domains.
   * they are listed by depth-first search, the order doesn't guarantee a domain at the end.
   */
  obj = NULL;
  while ((obj = hwloc_get_next_pcidev(topology, obj)) != NULL) {
    if (obj->attr->pcidev.domain)
      return 1;
  }

  /* check PCI Bridges for domains.
   * they are listed by depth-first search, the order doesn't guarantee a domain at the end.
   */
  obj = NULL;
  while ((obj = hwloc_get_next_bridge(topology, obj)) != NULL) {
    if (obj->attr->bridge.upstream_type != HWLOC_OBJ_BRIDGE_PCI)
      break;
    if (obj->attr->pcidev.domain)
      return 1;
  }

  return 0;
}

static void
lstopo_add_collapse_attributes(hwloc_topology_t topology)
{
  hwloc_obj_t obj, collapser = NULL;
  unsigned collapsed = 0;
  /* collapse identical PCI devs */
  for(obj = hwloc_get_next_pcidev(topology, NULL); obj; obj = hwloc_get_next_pcidev(topology, obj)) {
    if (collapser) {
      if (!obj->io_arity && !obj->misc_arity
	  && obj->parent == collapser->parent
	  && obj->attr->pcidev.vendor_id == collapser->attr->pcidev.vendor_id
	  && obj->attr->pcidev.device_id == collapser->attr->pcidev.device_id
	  && obj->attr->pcidev.subvendor_id == collapser->attr->pcidev.subvendor_id
	  && obj->attr->pcidev.subdevice_id == collapser->attr->pcidev.subdevice_id) {
	/* collapse another one */
	((struct lstopo_obj_userdata *)obj->userdata)->pci_collapsed = -1;
	collapsed++;
	continue;
      } else if (collapsed > 1) {
	/* end this collapsing */
	((struct lstopo_obj_userdata *)collapser->userdata)->pci_collapsed = collapsed;
	collapser = NULL;
	collapsed = 0;
      }
    }
    if (!obj->io_arity && !obj->misc_arity) {
      /* start a new collapsing */
      collapser = obj;
      collapsed = 1;
    }
  }
  if (collapsed > 1) {
    /* end this collapsing */
    ((struct lstopo_obj_userdata *)collapser->userdata)->pci_collapsed = collapsed;
  }
}

void
lstopo_prepare(struct lstopo_output *loutput)
{
  hwloc_obj_t root = hwloc_get_root_obj(loutput->topology);
  lstopo_populate_userdata(root);
  if (loutput->collapse)
    lstopo_add_collapse_attributes(loutput->topology);
}

void
lstopo_destroy(struct lstopo_output *loutput)
{
  hwloc_obj_t root = hwloc_get_root_obj(loutput->topology);
  lstopo_destroy_userdata(root);
}

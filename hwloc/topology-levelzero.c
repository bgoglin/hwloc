/*
 * Copyright © 2020-2021 Inria.  All rights reserved.
 * See COPYING in top-level directory.
 */

#include "private/autogen/config.h"
#include "hwloc.h"
#include "hwloc/plugins.h"

/* private headers allowed for convenience because this plugin is built within hwloc */
#include "private/misc.h"
#include "private/debug.h"

#include <level_zero/ze_api.h>
#include <level_zero/zes_api.h>

struct hwloc_osdev_array {
  unsigned nr, nr_allocated;
  hwloc_obj_t *objs;
};

static void
hwloc__levelzero_osdev_array_init(struct hwloc_osdev_array *array)
{
  array->nr = 0;
  array->nr_allocated = 0;
  array->objs = NULL;
}

static int
hwloc__levelzero_osdev_array_add(struct hwloc_osdev_array *array,
                                 hwloc_obj_t new)
{
  if (array->nr_allocated == array->nr) {
    unsigned new_nr_allocated = array->nr_allocated + 40; /* enough for 8 devices + 4 subdevices each on first allocation */
    hwloc_obj_t *tmp = realloc(array->objs, new_nr_allocated * sizeof(*array->objs));
    if (!tmp)
      /* the object won't be added */
      return -1;
    array->nr_allocated = new_nr_allocated;
    array->objs = tmp;
  }
  array->objs[array->nr++] = new;
  return 0;
}

static void
hwloc__levelzero_osdev_array_free(struct hwloc_osdev_array *array)
{
  free(array->objs);
}

static int
hwloc__levelzero_osdev_array_find(struct hwloc_osdev_array *array,
                                  hwloc_obj_t osdev)
{
  unsigned i;
  for(i=0; i<array->nr; i++)
    if (array->objs[i] == osdev)
      return (int)i;
  return -1;
}

static void
hwloc__levelzero_cqprops_get(zes_device_handle_t h,
                             hwloc_obj_t osdev)
{
  ze_command_queue_group_properties_t *cqprops;
  unsigned nr_cqprops = 0;
  ze_result_t res;

  res = zeDeviceGetCommandQueueGroupProperties(h, &nr_cqprops, NULL);
  if (res != ZE_RESULT_SUCCESS || !nr_cqprops)
    return;

  cqprops = malloc(nr_cqprops * sizeof(*cqprops));
  if (cqprops) {
    res = zeDeviceGetCommandQueueGroupProperties(h, &nr_cqprops, cqprops);
    if (res == ZE_RESULT_SUCCESS) {
      unsigned k;
      char tmp[32];
      snprintf(tmp, sizeof(tmp), "%u", nr_cqprops);
      hwloc_obj_add_info(osdev, "LevelZeroCQGroups", tmp);
      for(k=0; k<nr_cqprops; k++) {
        char name[32];
        snprintf(name, sizeof(name), "LevelZeroCQGroup%u", k);
        snprintf(tmp, sizeof(tmp), "%u*0x%lx", (unsigned) cqprops[k].numQueues, (unsigned long) cqprops[k].flags);
        hwloc_obj_add_info(osdev, name, tmp);
      }
    }
    free(cqprops);
  }
}

static void
hwloc__levelzero_memory_get(zes_device_handle_t h,
                            hwloc_obj_t root_osdev,
                            unsigned nr_osdevs, hwloc_obj_t *sub_osdevs)
{
  zes_mem_handle_t *mh;
  uint32_t nr_mems;
  ze_result_t res;
  unsigned long long totalHBMkB = 0;
  unsigned long long totalDRAMkB = 0;

  res = zesDeviceEnumMemoryModules(h, &nr_mems, NULL);
  if (res != ZE_RESULT_SUCCESS || !nr_mems)
    return;

  mh = malloc(nr_mems * sizeof(*mh));
  if (mh) {
    res = zesDeviceEnumMemoryModules(h, &nr_mems, mh);
    if (res == ZE_RESULT_SUCCESS) {
      unsigned m;
      for(m=0; m<nr_mems; m++) {
        zes_mem_properties_t mprop;
        res = zesMemoryGetProperties(mh[m], &mprop);
        if (res == ZE_RESULT_SUCCESS) {
          const char *type;
          hwloc_obj_t osdev;
          char name[64], value[64];
          if (mprop.onSubdevice) {
            if (mprop.subdeviceId >= nr_osdevs || !nr_osdevs || !sub_osdevs) {
              if (!hwloc_hide_errors())
                fprintf(stderr, "LevelZero: Ignoring memory on unexpected subdeviceId #%u\n", mprop.subdeviceId);
              osdev = NULL; /* we'll ignore it but we'll still agregate its subdevice memories into totalHBM/DRAMkB */
            } else {
              osdev = sub_osdevs[mprop.subdeviceId];
            }
          } else {
            osdev = root_osdev;
          }
          switch (mprop.type) {
          case ZES_MEM_TYPE_HBM:
            type = "HBM";
            totalHBMkB += mprop.physicalSize >> 10;
            break;
          case ZES_MEM_TYPE_DDR:
          case ZES_MEM_TYPE_DDR3:
          case ZES_MEM_TYPE_DDR4:
          case ZES_MEM_TYPE_DDR5:
          case ZES_MEM_TYPE_LPDDR:
          case ZES_MEM_TYPE_LPDDR3:
          case ZES_MEM_TYPE_LPDDR4:
          case ZES_MEM_TYPE_LPDDR5:
            type = "DRAM";
            totalDRAMkB += mprop.physicalSize >> 10;
            break;
          default:
            type = NULL;
          }

          if (!osdev || !type || !mprop.physicalSize)
            continue;

          if (osdev != root_osdev) {
            /* set the subdevice memory immediately */
            snprintf(name, sizeof(name), "LevelZero%sSize", type);
            snprintf(value, sizeof(value), "%llu", (unsigned long long) mprop.physicalSize >> 10);
            hwloc_obj_add_info(osdev, name, value);
          }
        }
      }
    }
    free(mh);
  }

  /* set the root device memory at the end, once subdevice memories were agregated */
  if (totalHBMkB) {
    char value[64];
    snprintf(value, sizeof(value), "%llu", totalHBMkB);
    hwloc_obj_add_info(root_osdev, "LevelZeroHBMSize", value);
  }
  if (totalDRAMkB) {
    char value[64];
    snprintf(value, sizeof(value), "%llu", totalDRAMkB);
    hwloc_obj_add_info(root_osdev, "LevelZeroDRAMSize", value);
  }
}

struct hwloc_levelzero_ports {
  unsigned nr_allocated;
  unsigned nr;
  struct hwloc_levelzero_port {
    hwloc_obj_t osdev;
    zes_fabric_port_properties_t props;
    zes_fabric_port_state_t state;
  } *ports;
};

static void
hwloc__levelzero_ports_init(struct hwloc_levelzero_ports *hports)
{
  hports->nr_allocated = 0;
  hports->nr = 0;
  hports->ports = NULL;

  free(hports->ports);
}

static void
hwloc__levelzero_ports_get(zes_device_handle_t dvh,
                          hwloc_obj_t root_osdev,
                           unsigned nr_sub_osdevs, hwloc_obj_t *sub_osdevs,
                           struct hwloc_levelzero_ports *hports)
{
  zes_fabric_port_handle_t *ports;
  uint32_t nr_new;
  unsigned i;
  ze_result_t res;

  res = zesDeviceEnumFabricPorts(dvh, &nr_new, NULL);
  if (res != ZE_RESULT_SUCCESS || !nr_new)
    return;

  if (hports->nr_allocated - hports->nr < nr_new) {
    /* we must extend the array */
    struct hwloc_levelzero_port *tmp;
    uint32_t new_nr_allocated;
    /* Extend the array by 8x the number of ports in this device.
     * This should mean a single allocation per 8 devices.
     */
    new_nr_allocated = hports->nr_allocated + 8*nr_new;
    tmp = realloc(hports->ports, new_nr_allocated * sizeof(*hports->ports));
    if (!tmp)
      return;
    hports->ports = tmp;
    hports->nr_allocated = new_nr_allocated;
  }

  ports = malloc(nr_new * sizeof(*ports));
  if (!ports)
    return;
  res = zesDeviceEnumFabricPorts(dvh, &nr_new, ports);

  for(i=0; i<nr_new; i++) {
    unsigned id = hports->nr;
    res = zesFabricPortGetProperties(ports[i], &hports->ports[id].props);
    if (res != ZE_RESULT_SUCCESS)
      continue;
    res = zesFabricPortGetState(ports[i], &hports->ports[id].state);
    // returns UNKNOWN_ERROR when state is not healthy?
    // if (res != ZE_RESULT_SUCCESS)
    //   continue;
    if (hports->ports[id].props.onSubdevice) {
      if (hports->ports[id].props.subdeviceId < nr_sub_osdevs)
        hports->ports[id].osdev = sub_osdevs[hports->ports[id].props.subdeviceId];
      else
        continue;
    } else {
      hports->ports[id].osdev = root_osdev;
    }
    hports->nr++;
  }

  free(ports);
}

static int
hwloc__levelzero_ports_add_xelink_bandwidth(struct hwloc_topology *topology,
                                            struct hwloc_osdev_array *oarray,
                                            hwloc_uint64_t *bws)
{
  void *handle;
  int err;

  handle = hwloc_backend_distances_add_create(topology, "XeLinkBandwidth",
                                              HWLOC_DISTANCES_KIND_FROM_OS|HWLOC_DISTANCES_KIND_MEANS_BANDWIDTH,
                                              0);
  if (!handle)
    goto out;

  err = hwloc_backend_distances_add_values(topology, handle, oarray->nr, oarray->objs, bws, 0);
  if (err < 0)
    goto out;
  /* arrays are now attached to the handle */
  oarray->objs = NULL;
  bws = NULL;

  err = hwloc_backend_distances_add_commit(topology, handle, 0 /* don't group GPUs */);
  if (err < 0)
    goto out;

  return 0;

 out:
  free(bws);
  return -1;
}

static int
hwloc__levelzero_ports_connect(struct hwloc_topology *topology,
                               struct hwloc_osdev_array *oarray,
                               struct hwloc_levelzero_ports *hports)
{
  hwloc_uint64_t *bws;
  unsigned i, j;
  int gotbw = 0;

  if (!hports->nr)
    return 0;

  bws = calloc(oarray->nr * oarray->nr, sizeof(*bws));
  if (!bws)
    return -1;

  for(i=0; i<hports->nr; i++) {
    if (hports->ports[i].state.status == ZES_FABRIC_PORT_STATUS_HEALTHY) {
      for(j=0; j<hports->nr; j++) {
        if (i==j)
          continue;
        if (hports->ports[i].state.remotePortId.fabricId == hports->ports[j].props.portId.fabricId
            && hports->ports[i].state.remotePortId.attachId == hports->ports[j].props.portId.attachId
            && hports->ports[i].state.remotePortId.portNumber == hports->ports[j].props.portId.portNumber) {
          int iindex, jindex;
          printf("found link model %s with %llu bit/s TX from port #u (%u-%u-%u osdev %p) to port #u (%u-%u-%u osdev %p)\n",
                      hports->ports[j].props.model,
                      (unsigned long long) hports->ports[i].state.rxSpeed.bitRate,
                      hports->ports[i].state.remotePortId.fabricId,
                      hports->ports[i].state.remotePortId.attachId,
                      hports->ports[i].state.remotePortId.portNumber,
                      hports->ports[i].osdev,
                      hports->ports[j].state.remotePortId.fabricId,
                      hports->ports[j].state.remotePortId.attachId,
                      hports->ports[j].state.remotePortId.portNumber,
                      hports->ports[j].osdev);
          /* only keep XeLink for now */
          if (strcmp(hports->ports[j].props.model, "XeLink"))
            continue;
          iindex = hwloc__levelzero_osdev_array_find(oarray, hports->ports[i].osdev);
          jindex = hwloc__levelzero_osdev_array_find(oarray, hports->ports[j].osdev);
          if (iindex<0 || jindex<0)
            continue;
          bws[iindex*oarray->nr+jindex] += hports->ports[i].state.rxSpeed.bitRate >> 20; /* MB/s */
          // TODO: way to accumulate subdevs bw into rootdevs? tranformation? different matrix?
          gotbw++;
        }
      }
    }
  }
  if (!gotbw) {
    free(bws);
    return 0;
  }

  for(i=0; i<oarray->nr; i++)
    /* add very high artifical values on the diagonal since local is faster than remote.
     * use 1TB/s for local, it somehow matches the HBM.
     */
    bws[i*oarray->nr+i] = 1000000;

  return hwloc__levelzero_ports_add_xelink_bandwidth(topology, oarray, bws);
}

static void
hwloc__levelzero_ports_destroy(struct hwloc_levelzero_ports *hports)
{
  free(hports->ports);
}

static int
hwloc_levelzero_discover(struct hwloc_backend *backend, struct hwloc_disc_status *dstatus)
{
  /*
   * This backend uses the underlying OS.
   * However we don't enforce topology->is_thissystem so that
   * we may still force use this backend when debugging with !thissystem.
   */

  struct hwloc_topology *topology = backend->topology;
  enum hwloc_type_filter_e filter;
  ze_result_t res;
  ze_driver_handle_t *drh;
  uint32_t nbdrivers, i, k, zeidx;
  struct hwloc_osdev_array oarray;
  struct hwloc_levelzero_ports hports;
  char *fabric_env = getenv("HWLOC_L0_FABRIC");
  int sysman_maybe_missing = 0; /* 1 if ZES_ENABLE_SYSMAN=1 was NOT set early, 2 if ZES_ENABLE_SYSMAN=0 */
  char *env;

  hwloc__levelzero_osdev_array_init(&oarray);

  hwloc__levelzero_ports_init(&hports);

  assert(dstatus->phase == HWLOC_DISC_PHASE_IO);

  hwloc_topology_get_type_filter(topology, HWLOC_OBJ_OS_DEVICE, &filter);
  if (filter == HWLOC_TYPE_FILTER_KEEP_NONE)
    return 0;

  /* Tell L0 to create sysman devices.
   * If somebody already initialized L0 without Sysman, zesDeviceGetProperties() will fail below.
   * The lib constructor and Windows DllMain tried to set ZES_ENABLE_SYSMAN=1 early (see topology.c),
   * we try again in case they didn't.
   */
  env = getenv("ZES_ENABLE_SYSMAN");
  if (!env) {
    putenv((char *) "ZES_ENABLE_SYSMAN=1");
    /* we'll warn below if we fail to get zes devices */
    sysman_maybe_missing = 1;
  } else if (!atoi(env)) {
    sysman_maybe_missing = 2;
  }

  res = zeInit(0);
  if (res != ZE_RESULT_SUCCESS) {
    if (!hwloc_hide_errors()) {
      fprintf(stderr, "Failed to initialize LevelZero in ze_init(): %d\n", (int)res);
    }
    return 0;
  }

  nbdrivers = 0;
  res = zeDriverGet(&nbdrivers, NULL);
  if (res != ZE_RESULT_SUCCESS || !nbdrivers)
    return 0;
  drh = malloc(nbdrivers * sizeof(*drh));
  if (!drh)
    return 0;
  res = zeDriverGet(&nbdrivers, drh);
  if (res != ZE_RESULT_SUCCESS) {
    free(drh);
    return 0;
  }

  zeidx = 0;
  for(i=0; i<nbdrivers; i++) {
    uint32_t nbdevices, j;
    ze_device_handle_t *dvh;
    char buffer[13];

    nbdevices = 0;
    res = zeDeviceGet(drh[i], &nbdevices, NULL);
    if (res != ZE_RESULT_SUCCESS || !nbdevices)
      continue;
    dvh = malloc(nbdevices * sizeof(*dvh));
    if (!dvh)
      continue;
    res = zeDeviceGet(drh[i], &nbdevices, dvh);
    if (res != ZE_RESULT_SUCCESS) {
      free(dvh);
      continue;
    }

#if 0
    /* No interesting info attr to get from driver properties for now.
     * Driver UUID is process-specific, it won't help much.
     */
    ze_driver_properties_t drprop;
    res = zeDriverGetProperties(drh[i], &drprop);
#endif

    for(j=0; j<nbdevices; j++) {
      zes_device_properties_t prop;
      zes_pci_properties_t pci;
      zes_device_handle_t sdvh = dvh[j];
      uint32_t nr_subdevices;
      hwloc_obj_t osdev, parent, *subosdevs = NULL;

      res = zesDeviceGetProperties(sdvh, &prop);
      if (res != ZE_RESULT_SUCCESS) {
        /* L0 was likely initialized without sysman, don't bother */
        if (sysman_maybe_missing == 1 && !hwloc_hide_errors())
          fprintf(stderr, "hwloc/levelzero: zesDeviceGetProperties() failed (ZES_ENABLE_SYSMAN=1 set too late?).\n");
        else if (sysman_maybe_missing == 2 && !hwloc_hide_errors())
          fprintf(stderr, "hwloc/levelzero: zesDeviceGetProperties() failed (ZES_ENABLE_SYSMAN=0).\n");
        continue;
      }

      osdev = hwloc_alloc_setup_object(topology, HWLOC_OBJ_OS_DEVICE, HWLOC_UNKNOWN_INDEX);
      snprintf(buffer, sizeof(buffer), "ze%u", zeidx); // ze0d0 ?
      osdev->name = strdup(buffer);
      osdev->depth = HWLOC_TYPE_DEPTH_UNKNOWN;
      osdev->attr->osdev.type = HWLOC_OBJ_OSDEV_COPROC;
      osdev->subtype = strdup("LevelZero");
      hwloc_obj_add_info(osdev, "Backend", "LevelZero");

      snprintf(buffer, sizeof(buffer), "%u", i);
      hwloc_obj_add_info(osdev, "LevelZeroDriverIndex", buffer);
      snprintf(buffer, sizeof(buffer), "%u", j);
      hwloc_obj_add_info(osdev, "LevelZeroDriverDeviceIndex", buffer);

      /* these strings aren't useful with current implementations:
       * prop.vendorName is "Unknown" or "Intel(R) Corporation"
       * prop.modelName is "0x1234" (PCI device id)
       * prop.brandName is "Unknown" (subvendor name)
       * prop.serialNumber is "Unknown"
       * prop.boardNumber is "Unknown"
       */
      if (strcmp((const char *) prop.vendorName, "Unknown"))
        hwloc_obj_add_info(osdev, "LevelZeroVendor", (const char *) prop.vendorName);
      if (strcmp((const char *) prop.vendorName, "Unknown"))
        /* Model is always "0x...." in early implementations */
        hwloc_obj_add_info(osdev, "LevelZeroModel", (const char *) prop.modelName);
      if (strcmp((const char *) prop.brandName, "Unknown"))
        hwloc_obj_add_info(osdev, "LevelZeroBrand", (const char *) prop.brandName);
      if (strcmp((const char *) prop.serialNumber, "Unknown"))
        hwloc_obj_add_info(osdev, "LevelZeroSerialNumber", (const char *) prop.serialNumber);
      if (strcmp((const char *) prop.boardNumber, "Unknown"))
        hwloc_obj_add_info(osdev, "LevelZeroBoardNumber", (const char *) prop.boardNumber);

      hwloc__levelzero_cqprops_get(dvh[j], osdev);

      nr_subdevices = prop.numSubdevices;
      if (nr_subdevices > 0) {
        zes_device_handle_t *subh;
        subh = malloc(nr_subdevices * sizeof(*subh));
        subosdevs = malloc(nr_subdevices * sizeof(*subosdevs));
        if (subosdevs && subh) {
          zeDeviceGetSubDevices(dvh[j], &nr_subdevices, subh);
          for(k=0; k<nr_subdevices; k++) {
            subosdevs[k] = hwloc_alloc_setup_object(topology, HWLOC_OBJ_OS_DEVICE, HWLOC_UNKNOWN_INDEX);
            snprintf(buffer, sizeof(buffer), "ze%u.%u", zeidx, k); // ze0d0.0 ?
            subosdevs[k]->name = strdup(buffer);
            subosdevs[k]->depth = HWLOC_TYPE_DEPTH_UNKNOWN;
            subosdevs[k]->attr->osdev.type = HWLOC_OBJ_OSDEV_COPROC;
            subosdevs[k]->subtype = strdup("LevelZero");
            hwloc_obj_add_info(subosdevs[k], "Backend", "LevelZero");

            hwloc__levelzero_cqprops_get(subh[k], subosdevs[k]);
            /* no need to call hwloc__levelzero_memory_get() on subdevices,
             * they return everything just like the root device.
             */
          }
        } else {
          free(subosdevs);
          subosdevs = NULL;
          nr_subdevices = 0;
        }
        free(subh);
      }

      hwloc__levelzero_memory_get(dvh[j], osdev, nr_subdevices, subosdevs);

      if (fabric_env)
        hwloc__levelzero_ports_get(dvh[j], osdev, nr_subdevices, subosdevs, &hports);

      parent = NULL;
      res = zesDevicePciGetProperties(sdvh, &pci);
      if (res == ZE_RESULT_SUCCESS) {
        parent = hwloc_pci_find_parent_by_busid(topology,
                                                pci.address.domain,
                                                pci.address.bus,
                                                pci.address.device,
                                                pci.address.function);
        if (parent && parent->type == HWLOC_OBJ_PCI_DEVICE) {
          if (pci.maxSpeed.maxBandwidth > 0)
            parent->attr->pcidev.linkspeed = ((float)pci.maxSpeed.maxBandwidth)/1000/1000/1000;
        }
      }
      if (!parent)
        parent = hwloc_get_root_obj(topology);

      hwloc_insert_object_by_parent(topology, parent, osdev);
      hwloc__levelzero_osdev_array_add(&oarray, osdev);
      if (nr_subdevices) {
        for(k=0; k<nr_subdevices; k++)
          if (subosdevs[k]) {
            hwloc_insert_object_by_parent(topology, osdev, subosdevs[k]);
            hwloc__levelzero_osdev_array_add(&oarray, subosdevs[k]);
          }
        free(subosdevs);
      }
      zeidx++;
    }

    free(dvh);
  }

  if (hports.nr > 12 && !strcmp(fabric_env, "fake")) {
    /* connect two links */
    hports.ports[1].state.status = ZES_FABRIC_PORT_STATUS_HEALTHY;
    hports.ports[1].state.rxSpeed = hports.ports[1].props.maxRxSpeed;
    hports.ports[1].state.txSpeed = hports.ports[1].props.maxTxSpeed;
    hports.ports[1].state.remotePortId = hports.ports[10].props.portId;
    hports.ports[10].state.status = ZES_FABRIC_PORT_STATUS_HEALTHY;
    hports.ports[10].state.rxSpeed = hports.ports[10].props.maxRxSpeed;
    hports.ports[10].state.txSpeed = hports.ports[10].props.maxTxSpeed;
    hports.ports[10].state.remotePortId = hports.ports[1].props.portId;
    hports.ports[2].state.status = ZES_FABRIC_PORT_STATUS_HEALTHY;
    hports.ports[2].state.rxSpeed = hports.ports[2].props.maxRxSpeed;
    hports.ports[2].state.txSpeed = hports.ports[2].props.maxTxSpeed;
    hports.ports[2].state.remotePortId = hports.ports[12].props.portId;
    hports.ports[12].state.status = ZES_FABRIC_PORT_STATUS_HEALTHY;
    hports.ports[12].state.rxSpeed = hports.ports[12].props.maxRxSpeed;
    hports.ports[12].state.txSpeed = hports.ports[12].props.maxTxSpeed;
    hports.ports[12].state.remotePortId = hports.ports[2].props.portId;
  }

  hwloc__levelzero_ports_connect(topology, &oarray, &hports);
  hwloc__levelzero_ports_destroy(&hports);

  hwloc__levelzero_osdev_array_free(&oarray);

  free(drh);
  return 0;
}

static struct hwloc_backend *
hwloc_levelzero_component_instantiate(struct hwloc_topology *topology,
                                      struct hwloc_disc_component *component,
                                      unsigned excluded_phases __hwloc_attribute_unused,
                                      const void *_data1 __hwloc_attribute_unused,
                                      const void *_data2 __hwloc_attribute_unused,
                                      const void *_data3 __hwloc_attribute_unused)
{
  struct hwloc_backend *backend;

  backend = hwloc_backend_alloc(topology, component);
  if (!backend)
    return NULL;
  backend->discover = hwloc_levelzero_discover;
  return backend;
}

static struct hwloc_disc_component hwloc_levelzero_disc_component = {
  "levelzero",
  HWLOC_DISC_PHASE_IO,
  HWLOC_DISC_PHASE_GLOBAL,
  hwloc_levelzero_component_instantiate,
  10, /* after pci */
  1,
  NULL
};

static int
hwloc_levelzero_component_init(unsigned long flags)
{
  if (flags)
    return -1;
  if (hwloc_plugin_check_namespace("levelzero", "hwloc_backend_alloc") < 0)
    return -1;
  return 0;
}

#ifdef HWLOC_INSIDE_PLUGIN
HWLOC_DECLSPEC extern const struct hwloc_component hwloc_levelzero_component;
#endif

const struct hwloc_component hwloc_levelzero_component = {
  HWLOC_COMPONENT_ABI,
  hwloc_levelzero_component_init, NULL,
  HWLOC_COMPONENT_TYPE_DISC,
  0,
  &hwloc_levelzero_disc_component
};

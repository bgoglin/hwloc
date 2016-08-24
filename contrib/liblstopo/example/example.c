#include <private/autogen/config.h>
#include "lstopo.h"
#include "hwloc.h"

#include <unistd.h>

#ifdef HWLOC_WIN_SYS
#define hwloc_sleep(x) Sleep(x*1000)
#else
#define hwloc_sleep sleep
#endif

struct lstopo_color black = { 0, 0, 0, 0 };
struct lstopo_color package_color = { 0xff, 0, 0, 0 };
struct lstopo_color core_color = { 0, 0xff, 0, 0 };
struct lstopo_color pu_color = { 0, 0, 0xff, 0 };
struct lstopo_color numa_color = { 0xd2, 0xe7, 0xa4, 0 };
struct lstopo_color cache_color = { 0xff, 0xff, 0, 0 };

static int callback(struct lstopo_output *loutput, hwloc_obj_t obj, unsigned depth, unsigned x, unsigned width, unsigned y, unsigned height)
{
  struct draw_methods *methods = loutput->methods;
  unsigned fontsize = loutput->fontsize;
  unsigned gridsize = loutput->gridsize;

  switch (obj->type) {
  case HWLOC_OBJ_PACKAGE:
    methods->box(loutput, &package_color, depth, x, width, y, height);
    methods->text(loutput, &black, fontsize, depth, x+gridsize, y+gridsize, "toto package");
    return 0;
  case HWLOC_OBJ_CORE:
    methods->box(loutput, &core_color, depth, x, width, y, height);
    methods->text(loutput, &black, fontsize, depth, x+gridsize, y+gridsize, "titi core");
    return 0;
  case HWLOC_OBJ_PU:
    methods->box(loutput, &pu_color, depth, x, width, y, height);
    methods->text(loutput, &black, fontsize, depth, x+gridsize, y+gridsize, "tutu pu");
    return 0;
  case HWLOC_OBJ_NUMANODE:
    methods->box(loutput, &numa_color, depth, x, width, y, height);
    methods->text(loutput, &black, fontsize, depth, x+gridsize, y+gridsize, "numanuma");
    return 0;
  case HWLOC_OBJ_L1CACHE:
  case HWLOC_OBJ_L2CACHE:
  case HWLOC_OBJ_L3CACHE:
  case HWLOC_OBJ_L4CACHE:
  case HWLOC_OBJ_L5CACHE:
  case HWLOC_OBJ_L1ICACHE:
  case HWLOC_OBJ_L2ICACHE:
  case HWLOC_OBJ_L3ICACHE:
    methods->box(loutput, &cache_color, depth, x, width, y, height);
    methods->text(loutput, &black, fontsize, depth, x+gridsize, y+gridsize, "$$$$$");
    return 0;
  default:
    return -1;
  }
}

int main(int argc, char *argv[])
{
  hwloc_topology_t topology;
  struct lstopo_output loutput;
  int block = 1;
  int err;

  if (argc > 1) {
    if (!strcmp(argv[1], "--sleep")) {
      block = 0;
    } else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
      fprintf(stderr, "%s [options]\n", argv[0]);
      fprintf(stderr, "  --sleep    Sleep 1 second between non-blocking calls instead of blocking on X11 events\n");
      exit(EXIT_FAILURE);
    }
  }

  hwloc_topology_init(&topology);
  hwloc_topology_load(topology);

  lstopo_init(&loutput);
  loutput.logical = 0;
  loutput.topology = topology;
  loutput.drawing_callback = callback;
  lstopo_prepare(&loutput);

  err = output_x11(&loutput, NULL);
  if (err)
    goto failed;

  /* we're supposed to call declare_colors(), and declare_color() on our own-declared colors above,
   * but that's not strictly required for the cairo backend (whose declare_color() method is NULL).
   */
  assert(loutput.methods && !loutput.methods->declare_color);

  err = loutput.methods->draw(&loutput);
  if (err)
    goto failed;

  if (block) {
    /* let iloop() do the blocking wait for us */
    if (loutput.methods && loutput.methods->iloop)
      loutput.methods->iloop(&loutput, 1);
  } else {
    /* use multiple non-locking iloops() with 1s delay between them */
    if (loutput.methods && loutput.methods->iloop) {
      while (loutput.methods->iloop(&loutput, 0) >= 0) {
	printf("sleeping 1s\n");
	hwloc_sleep(1);
      }
    }
  }

  if (loutput.methods && loutput.methods->end)
    loutput.methods->end(&loutput);

 failed:
  lstopo_destroy(&loutput);

  hwloc_topology_destroy (topology);
  return EXIT_SUCCESS;
}

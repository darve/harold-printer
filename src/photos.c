#include "photos.h"

#include "log.h"
#include "pico/rand.h"
#include "printer.h"

bool photos_print(int index) {
  static int last = -1;

  if (photo_count == 0) {
    log_printf("photos: none built in. Add images to photos/ and rebuild.\r\n");
    return false;
  }
  if (index >= (int) photo_count) {
    log_printf("photos: no photo %d (have %u)\r\n", index, photo_count);
    return false;
  }
  if (index < 0) {
    index = (int) (get_rand_32() % photo_count);
    if (photo_count > 1 && index == last) index = (index + 1) % (int) photo_count;
  }
  last = index;

  const photo_t *p = &photos[index];
  log_printf("photos: printing %d/%u \"%s\" (%ux%u)\r\n",
             index, photo_count, p->name, p->width, p->height);

  bool ok = printer_image(p->bits, p->width, p->height);

  log_printf("photos: %s\r\n", ok ? "done" : "FAILED");
  return ok;
}

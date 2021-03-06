/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * This code is licensed under the GNU GPLv2 and later.
 */

#include "hw/misc/bcm2835_property.h"
#include "hw/misc/bcm2835_mbox_defs.h"

/* https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface */

static void bcm2835_property_mbox_push(BCM2835PropertyState *s, uint32_t value)
{
    uint32_t tag;
    uint32_t bufsize;
    uint32_t tot_len;
    int n;
    size_t resplen;
    uint32_t offset, length, color;
    uint32_t tmp;
    uint32_t xres, yres, xoffset, yoffset, bpp, pixo, alpha;
    uint32_t *newxres = NULL, *newyres = NULL, *newxoffset = NULL,
        *newyoffset = NULL, *newbpp = NULL, *newpixo = NULL, *newalpha = NULL;

    value &= ~0xf;

    s->addr = value;

    tot_len = ldl_phys(&s->dma_as, value);

    /* @(addr + 4) : Buffer response code */
    value = s->addr + 8;
    while (value + 8 <= s->addr + tot_len) {
        tag = ldl_phys(&s->dma_as, value);
        bufsize = ldl_phys(&s->dma_as, value + 4);
        /* @(value + 8) : Request/response indicator */
        resplen = 0;
        switch (tag) {
        case 0x00000000: /* End tag */
            break;
        case 0x00000001: /* Get firmware revision */
            stl_phys(&s->dma_as, value + 12, 346337);
            resplen = 4;
            break;

        case 0x00010001: /* Get board model */
            resplen = 4;
            break;
        case 0x00010002: /* Get board revision */
            resplen = 4;
            break;
        case 0x00010003: /* Get board MAC address */
            /* write the first four bytes of the 6-byte MAC */
            stl_phys(&s->dma_as, value + 12, 0xB827EBD0);
            /* write the last two bytes, avoid any write past the buffer end */
            stb_phys(&s->dma_as, value + 16, 0xEE);
            stb_phys(&s->dma_as, value + 17, 0xDF);
            resplen = 6;
            break;
        case 0x00010004: /* Get board serial */
            resplen = 8;
            break;
        case 0x00010005: /* Get ARM memory */
            /* base */
            stl_phys(&s->dma_as, value + 12, 0);
            /* size */
            stl_phys(&s->dma_as, value + 16, s->fbdev->vcram_base);
            resplen = 8;
            break;
        case 0x00010006: /* Get VC memory */
            /* base */
            stl_phys(&s->dma_as, value + 12, s->fbdev->vcram_base);
            /* size */
            stl_phys(&s->dma_as, value + 16, s->fbdev->vcram_size);
            resplen = 8;
            break;
        case 0x00028001: /* Set power state */
            /* Assume that whatever device they asked for exists,
             * and we'll just claim we set it to the desired state */
            tmp = ldl_phys(&s->dma_as, value + 16);
            stl_phys(&s->dma_as, value + 16, (tmp & 1));
            resplen = 8;
            break;

        /* Clocks */

        case 0x00030001: /* Get clock state */
            stl_phys(&s->dma_as, value + 16, 0x1);
            resplen = 8;
            break;

        case 0x00038001: /* Set clock state */
            resplen = 8;
            break;

        case 0x00030002: /* Get clock rate */
        case 0x00030004: /* Get max clock rate */
        case 0x00030007: /* Get min clock rate */
            switch (ldl_phys(&s->dma_as, value + 12)) {
            case 1: /* EMMC */
                stl_phys(&s->dma_as, value + 16, 50000000);
                break;
            case 2: /* UART */
                stl_phys(&s->dma_as, value + 16, 3000000);
                break;
            default:
                stl_phys(&s->dma_as, value + 16, 700000000);
                break;
            }
            resplen = 8;
            break;

        case 0x00038002: /* Set clock rate */
        case 0x00038004: /* Set max clock rate */
        case 0x00038007: /* Set min clock rate */
            resplen = 8;
            break;

        /* Temperature */

        case 0x00030006: /* Get temperature */
            stl_phys(&s->dma_as, value + 16, 25000);
            resplen = 8;
            break;

        case 0x0003000A: /* Get max temperature */
            stl_phys(&s->dma_as, value + 16, 99000);
            resplen = 8;
            break;


        /* Frame buffer */

        case 0x00040001: /* Allocate buffer */
            stl_phys(&s->dma_as, value + 12, s->fbdev->base);
            stl_phys(&s->dma_as, value + 16, s->fbdev->size);
            resplen = 8;
            break;
        case 0x00048001: /* Release buffer */
            resplen = 0;
            break;
        case 0x00040002: /* Blank screen */
            resplen = 4;
            break;
        case 0x00040003: /* Get display width/height */
        case 0x00040004:
            stl_phys(&s->dma_as, value + 12, s->fbdev->xres);
            stl_phys(&s->dma_as, value + 16, s->fbdev->yres);
            resplen = 8;
            break;
        case 0x00044003: /* Test display width/height */
        case 0x00044004:
            resplen = 8;
            break;
        case 0x00048003: /* Set display width/height */
        case 0x00048004:
            xres = ldl_phys(&s->dma_as, value + 12);
            newxres = &xres;
            yres = ldl_phys(&s->dma_as, value + 16);
            newyres = &yres;
            resplen = 8;
            break;
        case 0x00040005: /* Get depth */
            stl_phys(&s->dma_as, value + 12, s->fbdev->bpp);
            resplen = 4;
            break;
        case 0x00044005: /* Test depth */
            resplen = 4;
            break;
        case 0x00048005: /* Set depth */
            bpp = ldl_phys(&s->dma_as, value + 12);
            newbpp = &bpp;
            resplen = 4;
            break;
        case 0x00040006: /* Get pixel order */
            stl_phys(&s->dma_as, value + 12, s->fbdev->pixo);
            resplen = 4;
            break;
        case 0x00044006: /* Test pixel order */
            resplen = 4;
            break;
        case 0x00048006: /* Set pixel order */
            pixo = ldl_phys(&s->dma_as, value + 12);
            newpixo = &pixo;
            resplen = 4;
            break;
        case 0x00040007: /* Get alpha */
            stl_phys(&s->dma_as, value + 12, s->fbdev->alpha);
            resplen = 4;
            break;
        case 0x00044007: /* Test pixel alpha */
            resplen = 4;
            break;
        case 0x00048007: /* Set alpha */
            alpha = ldl_phys(&s->dma_as, value + 12);
            newalpha = &alpha;
            resplen = 4;
            break;
        case 0x00040008: /* Get pitch */
            stl_phys(&s->dma_as, value + 12, s->fbdev->pitch);
            resplen = 4;
            break;
        case 0x00040009: /* Get virtual offset */
            stl_phys(&s->dma_as, value + 12, s->fbdev->xoffset);
            stl_phys(&s->dma_as, value + 16, s->fbdev->yoffset);
            resplen = 8;
            break;
        case 0x00044009: /* Test virtual offset */
            resplen = 8;
            break;
        case 0x00048009: /* Set virtual offset */
            xoffset = ldl_phys(&s->dma_as, value + 12);
            newxoffset = &xoffset;
            yoffset = ldl_phys(&s->dma_as, value + 16);
            newyoffset = &yoffset;
            /*
            stl_phys(&s->dma_as, value + 12, bcm2835_fb.xres);
            stl_phys(&s->dma_as, value + 16, bcm2835_fb.yres);
            */
            resplen = 8;
            break;
        case 0x0004000a: /* Get/Test/Set overscan */
        case 0x0004400a:
        case 0x0004800a:
            stl_phys(&s->dma_as, value + 12, 0);
            stl_phys(&s->dma_as, value + 16, 0);
            stl_phys(&s->dma_as, value + 20, 0);
            stl_phys(&s->dma_as, value + 24, 0);
            resplen = 16;
            break;

        case 0x0004800b: /* Set palette */
            offset = ldl_phys(&s->dma_as, value + 12);
            length = ldl_phys(&s->dma_as, value + 16);
            n = 0;
            while (n < length - offset) {
                color = ldl_phys(&s->dma_as, value + 20 + (n << 2));
                stl_phys(&s->dma_as,
                         s->fbdev->vcram_base + ((offset + n) << 2), color);
                n++;
            }
            stl_phys(&s->dma_as, value + 12, 0);
            resplen = 4;
            break;

        case 0x00060001: /* Get DMA channels */
            /* channels 2-5 */
            stl_phys(&s->dma_as, value + 12, 0x003C);
            resplen = 4;
            break;

        case 0x00050001: /* Get command line */
            resplen = 0;
            break;

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "bcm2835_property: unhandled tag %08x\n", tag);
            break;
        }

        if (tag == 0) {
            break;
        }

        stl_phys(&s->dma_as, value + 8, (1 << 31) | resplen);
        value += bufsize + 12;
    }

    if (newxres || newyres || newxoffset || newyoffset || newbpp || newpixo
        || newalpha) {
        bcm2835_fb_reconfigure(s->fbdev, newxres, newyres, newxoffset,
                               newyoffset, newbpp, newpixo, newalpha);
    }

    /* Buffer response code */
    stl_phys(&s->dma_as, s->addr + 4, (1 << 31));
}

static uint64_t bcm2835_property_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    BCM2835PropertyState *s = opaque;
    uint32_t res = 0;

    switch (offset) {
    case MBOX_AS_DATA:
        res = MBOX_CHAN_PROPERTY | s->addr;
        s->pending = false;
        qemu_set_irq(s->mbox_irq, 0);
        break;

    case MBOX_AS_PENDING:
        res = s->pending;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        return 0;
    }

    return res;
}

static void bcm2835_property_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    BCM2835PropertyState *s = opaque;

    switch (offset) {
    case MBOX_AS_DATA:
        if (!s->pending) {
            s->pending = true;
            bcm2835_property_mbox_push(s, value);
            qemu_set_irq(s->mbox_irq, 1);
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        return;
    }

}

static const MemoryRegionOps bcm2835_property_ops = {
    .read = bcm2835_property_read,
    .write = bcm2835_property_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_property = {
    .name = TYPE_BCM2835_PROPERTY,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(addr, BCM2835PropertyState),
        VMSTATE_BOOL(pending, BCM2835PropertyState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_property_init(Object *obj)
{
    BCM2835PropertyState *s = BCM2835_PROPERTY(obj);
    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_property_ops, s,
                          TYPE_BCM2835_PROPERTY, 0x10);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->mbox_irq);
}

static void bcm2835_property_reset(DeviceState *dev)
{
    BCM2835PropertyState *s = BCM2835_PROPERTY(dev);

    s->pending = false;
}

static void bcm2835_property_realize(DeviceState *dev, Error **errp)
{
    BCM2835PropertyState *s = BCM2835_PROPERTY(dev);
    Object *obj;
    Error *err = NULL;

    obj = object_property_get_link(OBJECT(dev), "bcm2835_fb", &err);
    if (obj == NULL) {
        error_setg(errp, "%s: required bcm2835_fb link not found: %s",
                   __func__, error_get_pretty(err));
        return;
    }

    s->fbdev = BCM2835_FB(obj);

    obj = object_property_get_link(OBJECT(dev), "dma_mr", &err);
    if (obj == NULL) {
        error_setg(errp, "%s: required dma_mr link not found: %s",
                   __func__, error_get_pretty(err));
        return;
    }

    s->dma_mr = MEMORY_REGION(obj);
    address_space_init(&s->dma_as, s->dma_mr, NULL);

    bcm2835_property_reset(dev);
}

static void bcm2835_property_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bcm2835_property_realize;
    dc->vmsd = &vmstate_bcm2835_property;
}

static TypeInfo bcm2835_property_info = {
    .name          = TYPE_BCM2835_PROPERTY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835PropertyState),
    .class_init    = bcm2835_property_class_init,
    .instance_init = bcm2835_property_init,
};

static void bcm2835_property_register_types(void)
{
    type_register_static(&bcm2835_property_info);
}

type_init(bcm2835_property_register_types)

/* hw/s3c24xx_iic.c
 *
 * Samsung S3C24XX i2c peripheral emulation
 *
 * Copyright 2006, 2007, 2008 Daniel Silverstone, Ben Dooks
 *  and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2
 */

#include "hw.h"
#include "i2c.h"

#include "s3c24xx.h"

/* i2c controller registers */
#define S3C_IICCON  (0x00)
#define S3C_IICSTAT (0x04)
#define S3C_IICADD  (0x08)
#define S3C_IICDS   (0x0C)
#define S3C_IICLC   (0x10)

#define S3C_IICCON_ACKEN	(1<<7)
#define S3C_IICCON_TXDIV_16	(0<<6)
#define S3C_IICCON_TXDIV_512	(1<<6)
#define S3C_IICCON_IRQEN	(1<<5)
#define S3C_IICCON_IRQPEND	(1<<4)
#define S3C_IICCON_SCALE(x)	((x)&15)
#define S3C_IICCON_SCALEMASK	(0xf)

#define S3C_IICSTAT_MASTER_RX	(2<<6)
#define S3C_IICSTAT_MASTER_TX	(3<<6)
#define S3C_IICSTAT_SLAVE_RX	(0<<6)
#define S3C_IICSTAT_SLAVE_TX	(1<<6)
#define S3C_IICSTAT_MODEMASK	(3<<6)

#define S3C_IICSTAT_START	(1<<5)
#define S3C_IICSTAT_BUSBUSY	(1<<5)
#define S3C_IICSTAT_TXRXEN	(1<<4)
#define S3C_IICSTAT_ARBITR	(1<<3)
#define S3C_IICSTAT_ASSLAVE	(1<<2)
#define S3C_IICSTAT_ADDR0	 (1<<1)
#define S3C_IICSTAT_LASTBIT	 (1<<0)

#define S3C_IICLC_SDA_DELAY0	 (0 << 0)
#define S3C_IICLC_SDA_DELAY5	 (1 << 0)
#define S3C_IICLC_SDA_DELAY10	 (2 << 0)
#define S3C_IICLC_SDA_DELAY15	 (3 << 0)
#define S3C_IICLC_SDA_DELAY_MASK (3 << 0)

#define S3C_IICLC_FILTER_ON      (1<<2)

/* IIC-bus serial interface */
struct s3c24xx_i2c_state_s {
    i2c_bus *bus;
    qemu_irq irq;

    uint8_t control;
    uint8_t status;
    uint8_t data;
    uint8_t addy;
    int busy;
    int newstart;
};

static void s3c24xx_i2c_irq(struct s3c24xx_i2c_state_s *s)
{
    s->control |= 1 << 4;

    if (s->control & (1 << 5)) {
        qemu_irq_raise(s->irq);
    }
}

static void s3c24xx_i2c_reset(struct s3c24xx_i2c_state_s *s)
{
    s->control = 0x00;
    s->status = 0x00;
    s->busy = 0;
    s->newstart = 0;
}


static void s3c_master_work(void *opaque)
{
    struct s3c24xx_i2c_state_s *s = (struct s3c24xx_i2c_state_s *) opaque;
    int start = 0, stop = 0, ack = 1;

    if (s->control & (1 << 4))				/* Interrupt pending */
        return;
    if ((s->status & 0x90) != 0x90)			/* Master */
        return;

    stop = ~s->status & (1 << 5);
    if (s->newstart && s->status & (1 << 5)) {		/* START */
        s->busy = 1;
        start = 1;
    }
    s->newstart = 0;

    if (!s->busy) {
        return;
    }

    if (start) {
        ack = !i2c_start_transfer(s->bus, s->data >> 1, (~s->status >> 6) & 1);
    } else if (stop) {
        i2c_end_transfer(s->bus);
    } else if (s->status & (1 << 6)) {
        ack = !i2c_send(s->bus, s->data);
    } else {
        s->data = i2c_recv(s->bus);

        if (!(s->control & (1 << 7)))			/* ACK */
            i2c_nack(s->bus);
    }

    if (!(s->status & (1 << 5))) {
        s->busy = 0;
        return;
    }

    s->status &= ~1;
    s->status |= !ack;

    if (!ack) {
        s->busy = 0;
    }
    s3c24xx_i2c_irq(s);
}

static uint32_t s3c24xx_i2c_read(void *opaque, target_phys_addr_t addr)
{
    struct s3c24xx_i2c_state_s *s = (struct s3c24xx_i2c_state_s *) opaque;

    switch (addr) {
    case S3C_IICCON:
        return s->control;

    case S3C_IICSTAT:
        return s->status & ~(1 << 5);			/* Busy signal */

    case S3C_IICADD:
        return s->addy;

    case S3C_IICDS:
        return s->data;

    default:
        printf("%s: Bad register 0x%lx\n", __func__, addr);
        break;
    }
    return 0;
}

static void s3c24xx_i2c_write(void *opaque, target_phys_addr_t addr,
                              uint32_t value)
{
    struct s3c24xx_i2c_state_s *s = (struct s3c24xx_i2c_state_s *) opaque;

    switch (addr) {
    case S3C_IICCON:
        s->control = (s->control | 0xef) & value;
        if (s->busy || ((s->control & (1<<4)) == 0))
            s3c_master_work(s);
        break;

    case S3C_IICSTAT:
        s->status &= 0x0f;
        s->status |= value & 0xf0;
        if (s->status & (1 << 5))
            s->newstart = 1;
        s3c_master_work(s);
        break;

    case S3C_IICADD:
        s->addy = value & 0x7f;
        break;

    case S3C_IICDS:
        s->data = value & 0xff;
        s->busy = 1;
        break;

    default:
        printf("%s: Bad register 0x%lx\n", __func__, addr);
        break;
    }
}

static CPUReadMemoryFunc * const s3c24xx_i2c_readfn[] = {
    s3c24xx_i2c_read,
    s3c24xx_i2c_read,
    s3c24xx_i2c_read
};

static CPUWriteMemoryFunc * const s3c24xx_i2c_writefn[] = {
    s3c24xx_i2c_write,
    s3c24xx_i2c_write,
    s3c24xx_i2c_write
};

static void s3c24xx_i2c_save(QEMUFile *f, void *opaque)
{
    struct s3c24xx_i2c_state_s *s = (struct s3c24xx_i2c_state_s *) opaque;
    qemu_put_8s(f, &s->control);
    qemu_put_8s(f, &s->status);
    qemu_put_8s(f, &s->data);
    qemu_put_8s(f, &s->addy);

    qemu_put_be32(f, s->busy);
    qemu_put_be32(f, s->newstart);

}

static int s3c24xx_i2c_load(QEMUFile *f, void *opaque, int version_id)
{
    struct s3c24xx_i2c_state_s *s = (struct s3c24xx_i2c_state_s *) opaque;
    qemu_get_8s(f, &s->control);
    qemu_get_8s(f, &s->status);
    qemu_get_8s(f, &s->data);
    qemu_get_8s(f, &s->addy);

    s->busy = qemu_get_be32(f);
    s->newstart = qemu_get_be32(f);

    return 0;
}


struct s3c24xx_i2c_state_s *
s3c24xx_iic_init(qemu_irq irq, target_phys_addr_t base_addr)
{
    int tag;
    struct s3c24xx_i2c_state_s *s;

    s = qemu_mallocz(sizeof(struct s3c24xx_i2c_state_s));

    s->irq = irq;
    s->bus = i2c_init_bus(NULL, "i2c");

    s3c24xx_i2c_reset(s);

    tag = cpu_register_io_memory(s3c24xx_i2c_readfn, s3c24xx_i2c_writefn, s);
    cpu_register_physical_memory(base_addr, 0xffffff, tag);
    register_savevm(NULL, "s3c24xx_i2c", 0, 0, s3c24xx_i2c_save, s3c24xx_i2c_load, s);

    return s;
}

i2c_bus *s3c24xx_i2c_bus(struct s3c24xx_i2c_state_s *s)
{
    return s->bus;
}

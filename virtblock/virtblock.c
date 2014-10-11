#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>

#define VIRTBLOCK_MAGIC_NMINROS 16
#define VIRTBLOCK_TO_BLK_LAYER (virtblock_hardsect_size / 512)

static char DRIVER_NAME[] = "virtblock";

static int virtblock_ndevices = -1;
module_param_named(ndevices, virtblock_ndevices, int, 0);
MODULE_PARM_DESC(ndevices, "number of virtblock devices to create");

static int virtblock_nsectors = -1;
module_param_named(nsectors, virtblock_nsectors, int, 0);
MODULE_PARM_DESC(nsectors, "number of sectors to create in each device");

static int virtblock_hardsect_size = -1;
module_param_named(hardsect_size, virtblock_hardsect_size, int, 0);
MODULE_PARM_DESC(hardsect_size, "size of each sector in virtual disk");

struct virtblock_dev {
	size_t size;
	u8 *data;
	spinlock_t lock;
	struct request_queue *queue;
	struct gendisk *gd;
};

static int virtblock_major;
static struct virtblock_dev *virtblock_devices;

static int virtblock_open(struct block_device *bdev, fmode_t mode)
{
	/* struct virtblock_dev *dev = bdev->bd_dev->private_data; */
	printk(KERN_INFO "%s: in %s\n", DRIVER_NAME, __func__);
	/* nothing to do here */
	return 0;
}

static int virtblock_release(struct gendisk *disk, fmode_t mode)
{
	/* struct virtblock_dev *dev = disk->private_data; */
	printk(KERN_INFO "%s: in %s\n", DRIVER_NAME, __func__);
	/* nothing to do here */
	return 0;
}

static int virtblock_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	/* struct virtblock_dev *dev = bdev->bd_dev->private_data; */
	/*
	 * since this is a virtual device, we have nothing to return.
	 * this function exists for the sole pupose of this printk.
	 * ENOTTY will make it look as if we didn't implement getgeo.
	 */
	printk("%s: in %s\n", DRIVER_NAME, __func__);
	return -ENOTTY;
}

static struct block_device_operations virtblock_ops = {
	.owner = THIS_MODULE,
	.open = virtblock_open,
	.release = virtblock_release,
	.getgeo = virtblock_getgeo,
};


static int virtblock_check_module_params(void) {
	int err = 0;
	if (virtblock_ndevices < 0) {
		printk(KERN_ERR "%s: virtblock_ndevices < 0. value = %d\n",
				DRIVER_NAME, virtblock_ndevices);
		err = -EINVAL;
	}
	if (virtblock_nsectors <= 0) {
		printk(KERN_ERR "%s: virtblock_nsectors <= 0. value = %d\n",
				DRIVER_NAME, virtblock_nsectors);
		err = -EINVAL;
	}
	if (virtblock_hardsect_size < 512) {
		printk(KERN_ERR "%s: virtblock_hardsect_size < 512. value = %d\n",
				DRIVER_NAME, virtblock_hardsect_size);
		err = -EINVAL;
	}
	/* virtblock_hardsect_size must be a power of two */
	if (virtblock_hardsect_size & (virtblock_hardsect_size -1)) {
		printk(KERN_ERR "%s: virtblock_hardsect_size is not a power of two. "
				"value = %d\n",
				DRIVER_NAME, virtblock_hardsect_size);
		err = -EINVAL;
	}
	return err;
}

static void virtblock_request(struct request_queue *q) {
	struct request *req;
	struct bio_vec *bv;
	struct req_iterator iter;
	struct virtblock_dev *dev;
	unsigned int write, sector, nsect, offset;
	size_t count;
	void *blkbuf; /* buffer received from the block layer */
	void *devbuf; /* buffer received from our own device */
	while ((req = blk_fetch_request(q)) != NULL) {
		dev = req->rq_disk->private_data;
		if (req->cmd_type != REQ_TYPE_FS) {
			printk(KERN_NOTICE "%s: skipping non-fs request\n",
					DRIVER_NAME);
			__blk_end_request_all(req, -EIO);
			continue;
		}
		write = rq_data_dir(req) == WRITE;
		sector = blk_rq_pos(req) / VIRTBLOCK_TO_BLK_LAYER;
		nsect = blk_rq_sectors(req) / VIRTBLOCK_TO_BLK_LAYER;
		printk(KERN_INFO "%s: processing request %p\n", DRIVER_NAME, req);
		printk(KERN_INFO "%s: \tdevice %s\n", DRIVER_NAME, dev->gd->disk_name);
		printk(KERN_INFO "%s: \twrite %u\n", DRIVER_NAME, write);
		printk(KERN_INFO "%s: \tsector %u\n", DRIVER_NAME, sector);
		printk(KERN_INFO "%s: \tnsect %u\n", DRIVER_NAME, nsect);
		offset = sector * virtblock_hardsect_size;
		count = nsect * virtblock_hardsect_size;
		if (offset + count > dev->size) {
			printk(KERN_ERR "%s: @%s offset + count > dev->size",
					DRIVER_NAME, __func__);
			__blk_end_request_all(req, -EIO);
		}
		rq_for_each_segment(bv, req, iter) {
			printk(KERN_INFO "%s: \tprocessing segment %p\n", DRIVER_NAME, bv);
			printk(KERN_INFO "%s: \t\tlen %u\n", DRIVER_NAME, bv->bv_len);
			blkbuf = page_address(bv->bv_page) + bv->bv_offset;
			devbuf = dev->data + offset;
			if (write) /* write to device */
				memcpy(devbuf, blkbuf, bv->bv_len);
			else /* read from device */
				memcpy(blkbuf, devbuf, bv->bv_len);
			offset += bv->bv_len;
		}
		__blk_end_request_all(req, 0);
	}
}

/* Note:
 * This function does not call add_disk(dev->gd) to allow the module to call
 it for all devices at once upon completion of initialization. this way the
 module can safely unwind existing device before the end of the initialization
 process.
 */
static int __init virtblock_dev_setup(struct virtblock_dev *dev,
		int index)
{
	int err;
	dev->size = virtblock_nsectors * virtblock_hardsect_size;
	dev->data = vmalloc(dev->size);
	if (!dev->data) {
		err = -ENOMEM;
		printk(KERN_ERR "%s: failed to allocate data for virtual disk",
				DRIVER_NAME);
		goto fail_vmalloc_devdata;
	}
	memset(dev->data, 0, dev->size);
	spin_lock_init(&dev->lock);
	dev->queue = blk_init_queue(virtblock_request, &dev->lock);
	if (!dev->queue) {
		err = -ENOMEM;
		printk(KERN_ERR "%s: blk_init_queue failed", DRIVER_NAME);
		goto fail_blk_init_queue;
	}
	blk_queue_logical_block_size(dev->queue, virtblock_hardsect_size);
	dev->gd = alloc_disk(VIRTBLOCK_MAGIC_NMINROS);
	if (!dev->gd) {
		err = -ENOMEM;
		printk(KERN_ERR "%s: alloc_disk failed", DRIVER_NAME);
		goto fail_alloc_disk;
	}
	dev->gd->major = virtblock_major;
	dev->gd->first_minor = index * VIRTBLOCK_MAGIC_NMINROS;
	dev->gd->fops = &virtblock_ops;
	dev->gd->queue = dev->queue;
	dev->gd->private_data = dev;
	snprintf(dev->gd->disk_name, sizeof(dev->gd->disk_name),
			"%s%c", DRIVER_NAME, index + 'a');
	set_capacity(dev->gd, virtblock_nsectors * VIRTBLOCK_TO_BLK_LAYER);
	printk(KERN_INFO "%s: initialized device %s successfully\n",
			DRIVER_NAME, dev->gd->disk_name);
	return 0;
fail_alloc_disk:
	blk_cleanup_queue(dev->queue);
fail_blk_init_queue:
	vfree(dev->data);
fail_vmalloc_devdata:
	return err;
}

static void virtblock_dev_cleanup(struct virtblock_dev *dev)
{
	printk(KERN_INFO "%s: cleaning up device %s\n",
			DRIVER_NAME, dev->gd->disk_name);
	del_gendisk(dev->gd);
	blk_cleanup_queue(dev->queue);
	vfree(dev->data);
}

static int __init virtblock_init(void)
{
	int err;
	int i;
	printk(KERN_INFO "%s: in %s\n", DRIVER_NAME, __func__);
	err = virtblock_check_module_params();
	if (err)
		return err;
	virtblock_devices = kzalloc(
			sizeof(virtblock_devices[0]) * virtblock_ndevices,
			GFP_KERNEL);
	if (!virtblock_devices) {
		err = -ENOMEM;
		printk(KERN_ERR "%s: failed to allocate virtblock_devices\n",
				DRIVER_NAME);
		goto fail_kzalloc_virtblock_devices;
	}
	/* register_blkdev with 0 allocates a major for us */
	virtblock_major = register_blkdev(0, DRIVER_NAME);
	if (virtblock_major < 0) {
		err = virtblock_major;
		printk(KERN_ERR "%s: register_blkdev failed. err = %d\n",
				DRIVER_NAME, err);
		goto fail_register_blkdev;
	}
	for (i = 0; i < virtblock_ndevices; i++) {
		err = virtblock_dev_setup(&virtblock_devices[i], i);
		if (err) {
			printk(KERN_ERR "%s: virtblock_dev_setup failed. i = %d, "
			"err = %d\n", DRIVER_NAME, i, err);
			goto fail_virtblock_dev_setup_loop;
		}
	}
	/*
	 * call add_disk() on all devices at once at the end of the
	 * initialization process
	 */
	for (i = 0; i < virtblock_ndevices; i++)
		add_disk(virtblock_devices[i].gd);
	printk(KERN_INFO "%s: initialized successfully\n", DRIVER_NAME);
	return 0;
fail_virtblock_dev_setup_loop:
	/* device at [i] isn't initialized */
	while (i--)
		virtblock_dev_cleanup(&virtblock_devices[i]);
	unregister_blkdev(virtblock_major, DRIVER_NAME);
fail_register_blkdev:
	kfree(virtblock_devices);
fail_kzalloc_virtblock_devices:
	return err;
}
module_init(virtblock_init)

static void __exit virtblock_exit(void)
{
	int i;
	printk(KERN_INFO "%s: in %s\n", DRIVER_NAME, __func__);
	for (i = 0; i < virtblock_ndevices; i++)
		virtblock_dev_cleanup(&virtblock_devices[i]);
	unregister_blkdev(virtblock_major, DRIVER_NAME);
	kfree(virtblock_devices);
	printk(KERN_INFO "%s: exited successfully\n", DRIVER_NAME);
}
module_exit(virtblock_exit)


MODULE_AUTHOR("Tal Shorer");
MODULE_DESCRIPTION("A simple block device residing in ram");
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");


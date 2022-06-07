/* Prototype module for second mandatory DM510 assignment */
#ifndef __KERNEL__
#define __KERNEL__
#endif
#ifndef MODULE
#define MODULE
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/wait.h>
/* #include <asm/uaccess.h> */
#include <linux/uaccess.h>
#include <linux/semaphore.h>
/* #include <asm/system.h> */
#include <asm/switch_to.h>
/* Prototypes - this would normally go in a .h file */
static int dm510_open(struct inode *, struct file *);
static int dm510_release(struct inode *, struct file *);
static ssize_t dm510_read(struct file *, char *, size_t, loff_t *);
static ssize_t dm510_write(struct file *, const char *, size_t, loff_t *);
long dm510_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

#define DEVICE_NAME "dm510_dev" /* Dev name as it appears in /proc/devices */
#define MAJOR_NUMBER 254
#define MIN_MINOR_NUMBER 0
#define MAX_MINOR_NUMBER 1
#define BUFFER_SIZE 1000

#define DEVICE_COUNT 2
/* end of what really should have been in a .h file */
struct buffer
{
	wait_queue_head_t inq, outq;
	char *buffer, *end;
	int buffersize;
	char *rp, *wp;
	int nreaders, nwriters;
	struct fasync_struct *async_queue;
	struct mutex mutex;
}

struct device
{
	struct buffer *readbuf;
	struct buffer *writebuf;
	struct cdev cdev;
}

struct device *devices;
int buffer_size = BUFFER_SIZE;

/* file operations struct */
static struct file_operations dm510_fops = {
	.owner = THIS_MODULE,
	.read = dm510_read,
	.write = dm510_write,
	.open = dm510_open,
	.release = dm510_release,
	.unlocked_ioctl = dm510_ioctl};

/* called when module is loaded */
int dm510_init_module(void)
{
	dev_t dev;
	int i, result;
	struct buffer *buf0;
	struct buffer *buf1;
	/* initialization code belongs here */
	//get dev
	if (MAJOR_NUMBER)
	{
		dev = MKDEV(SCULL_MAJOR, MIN_MINOR_NUMBER);
		result = register_chrdev_region(dev, DEVICE_COUNT, DEVICE_NAME);
	}
	else
	{
		result = alloc_chrdev_region(&dev, MIN_MINOR_NUMBER, DEVICE_COUNT, DEVICE_NAME);
		MAJOR_NUMBER = MAJOR(dev);
	}
	if (result < 0)
	{
		printk(KERN_WARNING "device: can't get major %d\n", MAJOR_NUMBER);
		return result;
	}
	printk(KERN_INFO "DM510: Hello from your device!\n");

	//allocate
	devices = kmalloc(DEVICE_COUNT * sizeof(struct device), GFP_KERNEL);
	if (!devices)
	{
		result = -ENOMEM;
		dm510_cleanup_module();
		return result;
	}
	memset(devices, 0, DEVICE_COUNT * sizeof(struct device));
	if (init_buffer(buf0) < 0)
	{
		dm510_cleanup_module();
		return -ENOMEM;
	}
	if (init_buffer(buf1) < 0)
	{
		dm510_cleanup_module();
		return -ENOMEM;
	}
	for (i = 0; i < DEVICE_COUNT; i++)
	{
		if (i == 0)
		{
			devices[i].readbuf = buf0;
			devices[i].writebuf = buf1;
			setup_cdev(&devices[i], i);
		}
		else
		{
			devices[i].writebuf = buf0;
			devices[i].readbuf = buf1;
			setup_cdev(&devices[i], i);
		}
	}

	return 0;
}

static void setup_cdev(struct device *dev, int index)
{
	int err, devno = MKDEV(MAJOR_NUMBER, MIN_MINOR_NUMBER + index);

	cdev_init(&dev->cdev, &dm510_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &dm510_fops;
	err = cdev_add(&dev->cdev, devno, 1);
	if (err)
	{
		printk(KERN_NOTICE "Error %d adding device%d", err, index);
	}
}

int init_buffer(struct buffer *buf)
{
	buf = kmalloc(sizeof(struct buffer), GFP_KERNEL);
	if (!buf)
	{
		return -ENOMEM;
	}

	init_waitqueue_head(&(buf->inq));
	init_waitqueue_head(&(buf->outq));
	mutex_init(&buf->mutex);
}
/* Called when module is unloaded */
void dm510_cleanup_module(void)
{

	/* clean up code belongs here */
	int i;
	dev_t devno = MKDEV(MAJOR_NUMBER, MIN_MINOR_NUMBER);
	if (devices)
	{
		if (devices[0]->readbuf)
		{
			if (devices[0]->readbuf->buffer)
			{
				kfree(devices[0]->readbuf->buffer)
			}
			kfree(devices[0] - readbuf)
		}

		if (devices[0]->writebuf)
		{
			if (devices[0]->writebuf->buffer)
			{
				kfree(devices[0]->writebuf->buffer)
			}
			kfree(devices[0]->writebuf)
		}

		for (i = 0; i < DEVICE_COUNT, i++)
		{
			cdev_del(&devices[i].cdev);
		}
		kfree(devices);
	}
	unregister_chrdev_region(devno, DEVICE_COUNT)
		printk(KERN_INFO "DM510: Module unloaded.\n");
}

/* Called when a process tries to open the device file */
static int dm510_open(struct inode *inode, struct file *filp)
{

	/* device claiming code belongs here */
	struct device *dev;

	dev = container_of(inode->i_cdev, struct device, cdev);
	filp->private_data = dev;

	if (filp->f_mode & FMODE_READ)
	{
		if (mutex_lock_interruptible(&dev->readbuf->mutex))
		{
			return -ERESTARTSYS;
		}
		if (!dev->readbuf->buffer)
		{
			if (allocate_buffer(dev->readbuf) < 0)
			{
				mutex_unlock(&dev->readbuf->mutex);
				return -ENOMEM;
			}
		}
		dev->readbuf->nreaders++;
		mutex_unlock(&dev->readbuf->mutex);
	}
	if (filp->f_mode & FMODE_WRITE)
	{
		if (mutex_lock_interruptible(&dev->writebuf->mutex))
		{
			return -ERESTARTSYS;
		}
		//only allow writer if there is no other writer
		while (dev->writebuf->nwriters > 0)
		{
			mutex_unlock(&dev->writebuf->mutex);
			if (filp->f_flags & O_NONBLOCK)
			{
				return -EAGAIN;
			}
			if (wait_event_interruptible(&dev->writebuf->outq, (dev->writebuf->nwriters == 0)))
			{
				return -ERESTARTSYS;
			}
			if (mutex_lock_interruptible(&dev->writebuf->mutex))
			{
				return -ERESTARTSYS;
			}
		}
	}
	if (!dev->writebuf->buffer)
	{
		if (allocate_buffer(dev->writebuf) < 0)
		{
			mutex_unlock(&dev->writebuf->mutex) return -ENOMEM;
		}
	}
	dev->writebuf->nwriters++;
	mutex_unlock(&dev->writebuf->mutex);
}

//maybe nonseekable_open?
return 0;
}

//needs lock for buffer before call
int allocate_buffer(struct buffer *buf)
{
	buf->buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!buf->buffer)
	{
		return -ENOMEM;
	}
	buf->buffersize = buffer_size;
	buf->end = buf->buffer + buf->buffersize;
	buf->rp = buf->wp = buf->buffer;
	return 0;
}

/* Called when a process closes the device file. */
static int dm510_release(struct inode *inode, struct file *filp)
{

	/* device release code belongs here */
	struct device *dev = filp->private_data;
	//maybe fasync?
	if (filp->f_mode & FMODE_READ)
	{
		if (mutex_lock_interruptible(&dev->readbuf->mutex))
		{
			return -ERESTARTSYS;
		}
		dev->readbuf->nreaders--;
		if (dev->readbuf->nreaders + dev->readbuf->nwriters == 0)
		{
			kfree(dev->readbuf->buffer);
			dev->readbuf->buffer = NULL;
		}
		mutex_unlock(&dev->readbuf->mutex);
	}
	if (filp->f_mode & FMODE_WRITE)
	{
		if (mutex_lock_interruptible(&dev->writebuf->mutex))
		{
			return -ERESTARTSYS;
		}
		dev->writebuf->nwriters--;
		if (dev->writebuf->nreaders + dev->writebuf->nwriters == 0)
		{
			kfree(dev->writebuf->buffer);
			dev->wriebuf->buffer = NULL;
		}
		mutex_unlock(&dev->writebuf->mutex);
		wake_up_interruptible(&dev->writebuf->outq);
	}
	return 0;
}

/* Called when a process, which already opened the dev file, attempts to read from it. */
static ssize_t dm510_read(struct file *filp,
						  char *buf,	 /* The buffer to fill with data     */
						  size_t count,  /* The max number of bytes to read  */
						  loff_t *f_pos) /* The offset in the file           */
{

	/* read code belongs here */
	struct device *dev = filp->private_data;

	if (mutex_lock_interruptible(&dev->readbuf->mutex))
	{
		return -ERESTARTSYS;
	}
	while (dev->readbuf->rp == dev->readbuf->wp)
	{
		mutex_unlock(&dev->readbuf->mutex);
		if (filp->f_flags & O_NONBLOCK)
		{
			return -EAGAIN;
		}
		//pdebug??
		if (wait_event_interruptible(dev->readbuf->inq, (dev->readbuf->rp != dev->readbuf->wp)))
		{
			return -ERESTARTSYS;
		}
		if (mutex_lock_interruptible(&dev->readbuf->mutex))
		{
			return -ERESTARTSYS;
		}
	}

	if (dev->readbuf->wp > dev->readbuf->rp)
	{
		count = min(count, (size_t)(dev->readbuf->wp - dev->readbuf->rp));
	}
	else
	{
		count = min(count, (size_t)(dev->readbuf->end - dev->readbuf->rp));
	}
	if (copy_to_user(buf, dev->readbuf->rp, count))
	{
		mutex_unlock(&dev->readbuf->mutex);
		return -EFAULT;
	}
	dev->readbuf->rp += count;
	if (dev->readbuf->rp == dev->readbuf->end)
	{
		dev->readbuf->rp = dev->readbuf->buffer;
	}
	mutex_unlock(&dev->readbuf->mutex);
	wake_up_interruptible(&dev->readbuf->outq);
	//pdebug??
	return count;
}

/* Called when a process writes to dev file */
static ssize_t dm510_write(struct file *filp,
						   const char *buf, /* The buffer to get data from      */
						   size_t count,	/* The max number of bytes to write */
						   loff_t *f_pos)   /* The offset in the file           */
{

	/* write code belongs here */
	struct device *dev = filp->private_data;
	int result;

	if (mutex_lock_interruptible(&dev->writebuf - mutex))
	{
		return -ERESTARTSYS;
	}

	result = getwritespace(dev, filp);
	if (result)
	{
		return result;
	}

	count = min(count, (size_t)(spacefree(dev->writebuf)));
	if (dev->writebuf->wp >= dev->writebuf->rp)
	{
		count = min(count, (size_t)(dev->writebuf->end - dev->writebuf->wp));
	}
	else
	{
		count = min(count, (size_t)(dev->writebuf->rp - dev->writebuf->wp - 1));
	}
	if (copy_from_user(dev->writebuf->wp, buf, count))
	{
		mutec_unlock(&dev->writebuf->mutex);
		return -EFAULT;
	}
	dev->writebuf->wp += count;

	if (dev->writebuf->wp == dev->writebuf->end)
	{
		dev->writebuf->wp = dev->writebuf->buffer;
	}

	mutex_unlock(&dev->writebuf->mutex);
	wake_up_interruptible(&dev->writebuf->inq);

	return count; //return number of bytes written
}

static int getwritespace(struct device *dev, struct file filp)
{
	while (spacefree(dev->writebuf == 0))
	{
		mutex_unlock(&dev->writebuf->mutex);
		if (filp->f_flags & O_NONBLOCK)
		{
			return -EAGAIN;
		}
		if (wait_event_interruptible(&dev->writebuf->outq, spacefree(dev->writebuf) != 0))
		{
			return -ERESTARTSYS;
		}
		if (mutex_lock_interruptible(&dev->writebuf->mutex))
		{
			return -ERESTARTSYS;
		}
	}
	return 0;
}
// returns how much space is empty of the buffer, returning -1 if empty.
static int spacefree(struct buffer *buf)
{
	if (buf->rp == buf->wp)
	{
		return -1;
	}
	return ((buf->rp + buf->buffersize - buf->wp) % buf->buffersize);
}

/* called by system call icotl */
long dm510_ioctl(
	struct file *filp,
	unsigned int cmd,  /* command passed from the user */
	unsigned long arg) /* argument of the command */
{
	/* ioctl code belongs here */
	printk(KERN_INFO "DM510: ioctl called.\n");

	return 0; //has to be changed
}

module_init(dm510_init_module);
module_exit(dm510_cleanup_module);

MODULE_AUTHOR("Sebastian Larsen");
MODULE_LICENSE("GPL");

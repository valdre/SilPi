/*******************************************************************************
*                                                                              *
*                         Simone Valdre' - 21/09/2022                          *
*                   based on previous code by Marcello Carla'
*                  distributed under GPL-3.0-or-later licence                  *
*                                                                              *
*******************************************************************************/

//module creation functions
#include <linux/module.h>
//device creation functions
#include <linux/device.h>
#include <linux/cdev.h>
//user space memory access
#include <linux/uaccess.h>
//interrupt handling functions
#include <linux/interrupt.h>
//GPIO access
#include <linux/gpio.h>
//time measurement and sleeping
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/delay.h>
//Data structure
#include "../include/SilStruct.h"

MODULE_LICENSE("GPL v2");

// Driver info
#define NAME "SilPi"
#define HERE NAME, (char *) __FUNCTION__
#define FATAL(frm,...) printk(KERN_ALERT "%s:%s - " frm, HERE, ##__VA_ARGS__ )
#define DEBUG(frm,...) if(debug) printk(KERN_ALERT "%s:%s - " frm, HERE, ##__VA_ARGS__ )
#define BASE_MINOR 0
#define EVENTSIZE sizeof(struct Silevent)

// FSM states
#define SILPI_IDLE 0
#define SILPI_DEAD 1
#define SILPI_EVNT 2
#define SILPI_HANG 3

// GPIO mapping
#define RUN 23                  /* OUT - RUN/STOP          (active high) */
#define ENB 24                  /* OUT - ADC enable        (active high) */
#define LVE 25                  /*  IN - live time monitor (active  low) */
#define RDY 26                  /*  IN - data ready        (active  low) */
#define ACK 27                  /* OUT - data accepted     (active high) */
static struct gpio gpios[] = {
	{  4, GPIOF_IN, "D00" },            /* pin  7 */
	{  5, GPIOF_IN, "D01" },            /* pin 29 */
	{  6, GPIOF_IN, "D02" },            /* pin 31 */
	{  7, GPIOF_IN, "D03" },            /* pin 26 */
	{  8, GPIOF_IN, "D04" },            /* pin 24 */
	{  9, GPIOF_IN, "D05" },            /* pin 21 */
	{ 10, GPIOF_IN, "D06" },            /* pin 19 */
	{ 11, GPIOF_IN, "D07" },            /* pin 23 */
	{ 12, GPIOF_IN, "D08" },            /* pin 32 */
	{ 13, GPIOF_IN, "D09" },            /* pin 33 */
	{ 16, GPIOF_IN, "D10" },            /* pin 36 */
	{ 18, GPIOF_IN, "D11" },            /* pin 12 */
	{ 19, GPIOF_IN, "D12" },            /* pin 35 */
	
	{RUN, GPIOF_OUT_INIT_LOW, "RUN" },  /* pin 16 - RUN/STOP */
	{ENB, GPIOF_OUT_INIT_LOW, "ENB" },  /* pin 18 - ENABLE */
	{LVE, GPIOF_IN, "LVE" },            /* pin 22 - LIVE TIME */
	{RDY, GPIOF_IN, "RDY" },            /* pin 37 - READY */
	{ACK, GPIOF_OUT_INIT_LOW, "ACK" },  /* pin 13 - ACCEPT */
};

// Device data
static dev_t device  = 0;
static struct cdev cdev;
static int cdev_flag = 0;
static struct class *dev_class   = NULL;
static struct device *dev_device = NULL;

struct driver_data {
	int rdy_irq, lve_irq, state;
	ktime_t lt1, lt2;
	uint16_t val, errcnt;
	atomic_t a_write_idx;
	atomic_t a_read_idx;
	struct Silevent events[SIZE];
};

// debug can be switched on/off with "echo 1/0 > /sys/modules/SilMod/parameters/debug"
static int debug = 0;
module_param (debug, int, S_IRUGO | S_IWUSR);

void read_event(struct driver_data *ddata) {
	int j, val=0;
	
	for(j=12; j>=0; j--) {
		val = (val<<1) + gpio_get_value(gpios[j].gpio);
	}
	if((val^=0x1fff) < 2) val = 2;
	
	// start of acknowledgement signal
	gpio_set_value(ACK, 1);
	
	udelay(1);
	
	// end of acknowledgement signal
	gpio_set_value(ACK, 0);
	
	ddata->val = (uint16_t)val;
	ddata->state = SILPI_EVNT;
	return;
}

void fill_event(struct driver_data *ddata) {
	int write_idx = atomic_read(&(ddata->a_write_idx));
	
	// filling data event structure
	ddata->events[write_idx].ts     = ktime_to_ns(ddata->lt1);
	ddata->events[write_idx].dt     = (uint32_t)(ktime_to_ns(ddata->lt2) - ktime_to_ns(ddata->lt1));
	ddata->events[write_idx].val    = ddata->val;
	ddata->events[write_idx].errcnt = ddata->errcnt;
	
	// setting new write index
	write_idx = (write_idx+1) % SIZE;
	atomic_set(&(ddata->a_write_idx), write_idx);
	
	// reset error counter
	ddata->errcnt = 0;
	return;
}

irqreturn_t irq_service(int irq, void *arg) {
	int gpio_lve, gpio_rdy;
	
	// getting IRQ timestamp as soon as possible
	ktime_t ts = ktime_get_real();
	
	// retrieving data structure
	struct driver_data *ddata = arg;
	
	DEBUG("handling IRQ %d\n", irq);
	
	//checking IRQ coherence and glitches
	gpio_lve = gpio_get_value(LVE);
	gpio_rdy = gpio_get_value(RDY);
	if(irq == ddata->lve_irq) {
		switch(ddata->state) {
			case SILPI_IDLE:
				if(gpio_lve) {
					FATAL("Bad LVE transition detected (state = %d, LVE = %d RDY = %d)\n", ddata->state, gpio_lve, gpio_rdy);
					ddata->errcnt++;
					break;
				}
				ddata->lt1 = ts;
				ddata->state = SILPI_DEAD;
				break;
				
			case SILPI_EVNT:
				if(gpio_lve == 0) {
					FATAL("Bad LVE transition detected (state = %d, LVE = %d RDY = %d)\n", ddata->state, gpio_lve, gpio_rdy);
					ddata->errcnt++;
					break;
				}
				ddata->lt2 = ts;
				fill_event(ddata);
				ddata->state = SILPI_IDLE;
				break;
				
			default:
				FATAL("Bad LVE transition detected (state = %d, LVE = %d RDY = %d)\n", ddata->state, gpio_lve, gpio_rdy);
				ddata->errcnt++;
				ddata->state = SILPI_IDLE;
		}
		return IRQ_HANDLED;
	}
	
	if(irq == ddata->rdy_irq) {
		switch(ddata->state) {
			case SILPI_DEAD:
				if(gpio_rdy == 1) {
					FATAL("Bad RDY transition detected (state = %d, LVE = %d RDY = %d)\n", ddata->state, gpio_lve, gpio_rdy);
					ddata->errcnt++;
					break;
				}
// 				ddata->cvt = ts;
				// checking if there is space inside circular buffer
				if((atomic_read(&(ddata->a_write_idx)) + 1)%SIZE == atomic_read(&(ddata->a_read_idx))) {
					DEBUG("buffer hanged\n");
					ddata->state = SILPI_HANG;
					break;
				}
				return IRQ_WAKE_THREAD;
				
			default:
				FATAL("Bad RDY transition detected (state = %d, LVE = %d RDY = %d)\n", ddata->state, gpio_lve, gpio_rdy);
				ddata->errcnt++;
				ddata->state = SILPI_IDLE;
		}
		return IRQ_HANDLED;
	}
	
	FATAL("IRQ %d not properly handled\n", irq);
	ddata->errcnt++;
	return IRQ_HANDLED;
}

//threaded IRQ function
irqreturn_t irq_thread(int irq, void *arg) {
	// retrieving data structure
	struct driver_data *ddata = arg;
	
	if(irq == ddata->rdy_irq) {
		if(ddata->state == SILPI_DEAD) {
			read_event(ddata);
			return IRQ_HANDLED;
		}
		else {
			FATAL("Bad threaded IRQ function call (state = %d)\n", ddata->state);
			ddata->errcnt++;
			return IRQ_HANDLED;
		}
	}
	
	FATAL("IRQ %d not properly handled (threaded function)\n", irq);
	ddata->errcnt++;
	return IRQ_HANDLED;
}

/*
 *    open
 *
 *    inode->i_cdev       pointer to cdev structure
 *    inode->i_rdev       device identifier
 *    filp->f_mode        open mode (FMODE_READ, FMODE_WRITE, etc.)
 *    filp->f_flags       open flags (O_RDONLY, O_WRDONLY, etc.)
 *    filp->owner         module identity (module structure address)
 *    filp->private_data  private driver data structure
 *
 */

int open(struct inode *inode, struct file *filp) {
	int status;
	int minor = MINOR(inode->i_rdev);
	static struct driver_data ddata;
	
	DEBUG("open device %p -> %d:%d  mode %x  flags %o  by %p\n", inode->i_cdev, MAJOR(inode->i_rdev), minor, filp->f_mode, filp->f_flags, filp->f_op->owner);
	
	// allocate gpio's - another process request fails here
	
	status = gpio_request_array(gpios, ARRAY_SIZE(gpios));
	if (status) {
		printk(KERN_ALERT"%s:%s - Unable to obtain gpios.\n", HERE);
		return status;
	}
	
	//ADC enabled but stopped
	gpio_set_value(RUN,0);
	gpio_set_value(ENB,1);
	
	/* create data structures for events on this pin */
	filp->private_data = &ddata;
	atomic_set (&ddata.a_write_idx, 0);
	atomic_set (&ddata.a_read_idx, 0);
	
	gpio_direction_input(RDY);
	ddata.rdy_irq = gpio_to_irq(RDY);
	
	gpio_direction_input(LVE);
	ddata.lve_irq = gpio_to_irq(LVE);
	
	ddata.state  = SILPI_IDLE;
	ddata.errcnt = 0;
	
	// everything is ready - register interrupt routines
	if(request_threaded_irq(ddata.rdy_irq, irq_service, irq_thread, IRQF_TRIGGER_FALLING, "silenardy", &ddata)) {
		printk(KERN_ALERT"%s:%s - gpib: can't register IRQ %d\n", HERE, ddata.rdy_irq);
		gpio_free_array(gpios, ARRAY_SIZE(gpios));
		return -1;
	}
	DEBUG("IRQ %d registered.\n", ddata.rdy_irq);
	
	if(request_threaded_irq(ddata.lve_irq, irq_service, NULL, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "silenalve", &ddata)) {
		printk(KERN_ALERT "%s:%s - gpib: can't register IRQ %d\n", HERE, ddata.lve_irq);
		free_irq(ddata.rdy_irq, &ddata);  // unregister interrupt routine
		gpio_free_array(gpios, ARRAY_SIZE(gpios));
		return -1;
	}
	DEBUG("IRQ %d registered.\n", ddata.lve_irq);
	
	return 0;
}

// release
int release(struct inode *inode, struct file *filp) {
	struct driver_data *ddata = filp->private_data;
	
	// stop acquisition
	gpio_set_value(RUN,0);
	gpio_set_value(ENB,0);
	
	free_irq(ddata->rdy_irq, ddata);  // unregister interrupt routine
	free_irq(ddata->lve_irq, ddata);  // unregister interrupt routine
	gpio_free_array(gpios, ARRAY_SIZE(gpios));
	
	DEBUG("IRQ %d and %d released. GPIO released\n",ddata->rdy_irq, ddata->lve_irq);
	return 0;
}

// non-blocking read
ssize_t read(struct file *filp, char *buf, const size_t count, loff_t *ppos) {
	int read_idx, write_idx;
	int retval, transfer, request, transfer_byte;
	int first_group, second_group;
	struct driver_data *ddata = filp->private_data;
	struct Silevent *events = ddata->events;
	
	DEBUG("requested %lu bytes.\n", (unsigned long)count);
	
	read_idx  = atomic_read(&(ddata->a_read_idx));
	write_idx = atomic_read(&(ddata->a_write_idx));
	
	// no data to transfer
	if(read_idx == write_idx) return 0;
	if(count < EVENTSIZE) return 0;
	
	// computing amount of data to be moved to user space
	transfer = (write_idx + SIZE - read_idx) % SIZE;
	request  = count / EVENTSIZE;
	if(transfer > request) transfer = request;
	
	// transferring data to user space
	transfer_byte = transfer * EVENTSIZE;
	if(read_idx + transfer <= SIZE) {
		retval = copy_to_user (buf, events + read_idx, transfer_byte);
		if(retval) goto copy_error;
	}
	else {
		first_group = (SIZE - read_idx) * EVENTSIZE;
		retval = copy_to_user (buf, events + read_idx, first_group);
		if(retval) goto copy_error;
		
		second_group = transfer_byte - first_group ;
		retval = copy_to_user (buf + first_group, events, second_group);
		if(retval) goto copy_error;
	}
	
	// updating read_idx
	read_idx = (read_idx + transfer) % SIZE;
	atomic_set (&(ddata->a_read_idx), read_idx);
	
	// if the buffer hanged, try to read now that buffer is empty
	if(ddata->state == SILPI_HANG) {
		if(gpio_get_value(RDY)==0) read_event(ddata);
		else {
			FATAL("Bad RDY value with hanged buffer\n");
			ddata->state = SILPI_IDLE;
		}
	}
	return transfer_byte;
	
	copy_error:
	FATAL("copy_to_user failed with error %d\n", retval);
	return -EFAULT;
}

// write
ssize_t write(struct file *filp, const char *user_buf, size_t count, loff_t *ppos) {
	
	DEBUG("write request for %d bytes\n", (int) count);
	
	if(count>0 && user_buf[0]=='1') gpio_set_value(RUN,1);
	if(count>0 && user_buf[0]=='0') gpio_set_value(RUN,0);
	
	return count;
}

//struct fops
static struct file_operations fops= {
	.owner   = THIS_MODULE,
	.open    = open,
	.release = release,
	.read    = read,
	.write   = write,
};

//      exit - cleanup and module removal
static void mod_exit(void) {
	int major;
	
	major = MAJOR(device);
	if(dev_device) device_destroy (dev_class,MKDEV(major, BASE_MINOR));
	if(dev_class) class_destroy (dev_class);
	if(cdev_flag) cdev_del (&cdev);
	if(device) unregister_chrdev_region(device, 1);
}

// init - module initialization
static int mod_init(void) {
	long status=0;
	int major;
	
	// obtain major device number or exit
	status = alloc_chrdev_region(&device, BASE_MINOR, 1, NAME);
	if(status < 0) {
		FATAL("can't get major\n");
		return status;
	}
	major = MAJOR(device);
	DEBUG("major is %d\n", major);
	
	// create and register the device
	cdev_init(&cdev, &fops);
	cdev.owner = THIS_MODULE;
	
	status = cdev_add (&cdev, device, 1);
	if(status) {
		FATAL("can't register device %d (status = %ld)\n", device, status);
		goto failure;
	}
	cdev_flag = 1;
	DEBUG("registered device: %d %p %p\n", device, &fops, &cdev);
	
	// create the /dev/silena node
	dev_class = class_create(THIS_MODULE, NAME);
	if(IS_ERR(dev_class)) {
		status = (long)dev_class;
		dev_class = NULL;
		FATAL("class_create failed\n");
		goto failure;
	}
	
	dev_device = device_create(dev_class, NULL, MKDEV(major, BASE_MINOR), NULL, "silena");
	
	DEBUG("created device %p\n", dev_device);
	
	if(IS_ERR(dev_device)) {
		FATAL("device_create failed\n");
		status = (long)dev_device;
		dev_device = NULL;
		goto failure;
	}
	
	DEBUG("installed by \"%s\" (pid %i) at %p\n", current->comm, current->pid, current);
	return 0;
	
	failure:
	FATAL("module load failed\n");
	mod_exit();
	return status;
}

module_init (mod_init);
module_exit (mod_exit);

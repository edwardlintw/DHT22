#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/time64.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/kobject.h>
#define _INCLUDE_DHT22_EXP_DECL
#include "dht22_exp.h"

static const int    high = 1;
static const int    low  = 0;

/*
 * module (comand line) parameters
 */
static int gpio = DEFAULT_GPIO;
module_param(gpio, int, S_IRUGO);
MODULE_PARM_DESC(gpio, "Assigned GPIO number of DHT22 data pin, default is 4");

static bool autoupdate = true;
module_param(autoupdate, bool, S_IRUGO);
MODULE_PARM_DESC(autoupdate, "automatically trigger or not, default is true");

static int autoupdate_sec = DEFAULT_AUTOUPDATE_SEC;
module_param(autoupdate_sec, int, S_IRUGO);
MODULE_PARM_DESC(autoupdate_sec, 
                 "Seconds between two trigger events, default is '10' second");

/*
 * module's attributes
 */
static struct kobj_attribute gpio_attr = __ATTR_RO(gpio);
static struct kobj_attribute autoupdate_attr = __ATTR_RW(autoupdate);
static struct kobj_attribute autoupdate_sec_attr = __ATTR_RW(autoupdate_sec);
static struct kobj_attribute humidity_attr = __ATTR_RO(humidity);
static struct kobj_attribute temperature_attr = __ATTR_RO(temperature);
static struct kobj_attribute trigger_attr = __ATTR_WO(trigger);

static struct attribute* dht22_attrs[] = {
    &gpio_attr.attr,
    &autoupdate_attr.attr,
    &autoupdate_sec_attr.attr,
    &humidity_attr.attr,
    &temperature_attr.attr,
    &trigger_attr.attr,
    NULL
};

static struct attribute_group attr_group = {
    .attrs = dht22_attrs,
};

/* 
 * other global static vars
 */
static int                  irq_number;
static struct hrtimer       autoupdate_timer;
static struct hrtimer       timeout_timer;
static struct timespec64    prev_time;
static const int            timeout_time = 1;       /* second */
static int                  low_irq_count = 0;
static int                  humidity = 0;
static int                  temperature = 0;
static struct kobject*      dht22_kobj;
#if 0
static int                  irq_count = 0;
#endif

/*
 * to record signal HIGH time duration, for calculating bit 0/1
 * DHT22 send out 2-byte humidity, 2-byte temperature and 1-byte parity(CRC)
 * total 40 bits
 */
static int                  irq_time[40] = { 0 };
static enum { dht22_idle, dht22_working } dht22_state = dht22_idle;

/* task queue for calculating humidity/temperature/crc(parity check) */
static DECLARE_WORK(process, process_results);

static int __init dht22_init(void)
{
    int     ret;

    pr_err("Loading dht22_exp module...\n");

    /*
     * setup GPIO
     */
    if (!gpio_is_valid(gpio))
    {
        pr_err("dht22_exp can't validate GPIO %d; unloaded\n", gpio);
        return -EINVAL;
    }
    ret = gpio_request(gpio, "sysfs");
    if (ret < 0)
    {
        pr_err("dht22_exp failed to request GPIO %d, unloaded\n",gpio);
        return ret;
    }

    gpio_export(gpio, true);
    gpio_direction_output(gpio, high);

    /*
     * setup interrupt handler
     */
    irq_number = gpio_to_irq(gpio);
    if (irq_number < 0)
    {
        pr_err("dht22_exp failed to get IRQ for GPIO %d, unloaded\n", gpio);
        ret = irq_number;
        goto free_gpio;
    }
    pr_err("dht22_exp assign IRQ %d to GPIO %d.\n", irq_number, gpio);
    ret = request_irq(irq_number,
            dht22_irq_handler,
            IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
            "dht22_irq_handler",
            NULL);
    if (ret < 0 )
    {
        pr_err("idht22_exp failed to request IRQ, unloaded.\n");
        goto free_gpio;
    }

    /*
     * kobject
     */
    dht22_kobj = kobject_create_and_add("dht22", kernel_kobj);
    if (NULL == dht22_kobj)
    {
        pr_err("DHT22 failed to create kobject mapping\n");
        ret = -EINVAL;
        goto free_gpio;
    }

    /*
     * sysfs attribute
     */
    ret = sysfs_create_group(dht22_kobj, &attr_group);
    if (ret)
    {
        pr_err("DHT22 failed to create sysfs group.\n");
        goto sysfs_err;
    }

    /*
     * wait for 2sec for the first trigger
     * no matter autoupdate is on or off
     * at least DHT22 will be triggered once
     */
    hrtimer_init(&autoupdate_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    autoupdate_timer.function = autoupdate_func;
    hrtimer_start(&autoupdate_timer, ktime_set(2,0), HRTIMER_MODE_REL);
    /*
     * setup timeout timer, but not start yet
     * it'll start when triggering DHT22 to send data
     */
    hrtimer_init(&timeout_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    timeout_timer.function = timeout_func;
    pr_err("dht22_exp loaded.\n");

    return 0;

sysfs_err:
    kobject_put(dht22_kobj);

free_gpio:
    gpio_unexport(gpio);
    gpio_free(gpio);
    return ret;
}

static void __exit dht22_exit(void)
{
    free_irq(irq_number, NULL);
    gpio_unexport(gpio);
    gpio_free(gpio);
    hrtimer_cancel(&autoupdate_timer);
    hrtimer_cancel(&timeout_timer);
    cancel_work_sync(&process);
    kobject_put(dht22_kobj);
    pr_err("dht22_exp unloaded.\n");
}

static void to_trigger_dht22(void)
{
    /*
     * DHT22 working in progress, ignore this event
     */
    if (dht22_working == dht22_state)
    {
        pr_info("DHT22 is busy, ignore trigger event.....\n");
        return;
    }
    /*
     * reset statictics counter and set state as 'dht22_working'
     */
    low_irq_count = 0;
    dht22_state   = dht22_working;
#if 0
    irq_count = 0;
#endif

    /*
     * start timeout_timer
     * if the host can't receive 86 interrupts, the timeout_func() will
     * reset state to 'dht22_idle'
     */
    hrtimer_start(&timeout_timer, ktime_set(timeout_time, 0), HRTIMER_MODE_REL);
    /*
     * trigger DHT22, then wait 10 more seconds to re-trigger again
     */
    getnstimeofday64(&prev_time);
    trigger_dht22();
}

static void trigger_dht22(void)
{
    /*
     * pull down bus at least 1ms
     * to signal DHT22 for preparing humidity/temperature data
     */
    gpio_direction_output(gpio, low);
    udelay(1000);

    /*
     * release bus (bus return to HIGH, due to pull-up resistor)
     * switch GPIO to input mode to receive data from DHT22
     * let the interrupt handler to process the followings
     */
    gpio_direction_input(gpio);
}

static enum hrtimer_restart timeout_func(struct hrtimer* hrtimer)
{
    if (dht22_idle == dht22_state)
    {
        /*
         * host received 86 interrupts
         * state is set to 'dht22_idle' by 'void process_results()'
         * even CRC error (no results were produced)
         */
        pr_info("....timeout, fetch DHT22 data successfully\n");
    }
    else
    {
        /*
         * host didn't receive 86 interrupts
         * no results were produced
         * reset state to 'dht22_idle' and wait for next trigger
         */
        pr_info("....timeout, failed to fetch DHT22 data\n");
        dht22_state = dht22_idle;
    }
    return HRTIMER_NORESTART;
}

static enum hrtimer_restart autoupdate_func(struct hrtimer *hrtimer)
{
    to_trigger_dht22();

    if (autoupdate)
    {
        hrtimer_forward(hrtimer, ktime_get(), ktime_set(autoupdate_sec, 0));
        return HRTIMER_RESTART;
    }
    else
        return HRTIMER_NORESTART;
}

static void process_results(struct work_struct* work)
{
    int data[5] = { 0 }; /* 2-byte humidity, 2-byte temperature, 1-byte CRC */
    int i;
    int raw_humidity;
    int raw_temp;
    int byte;

    /* 
     * determine bit value 0 or 1
     * 22-30us is 0, 68~75us is 1
     * since DHT22 may be not as precise as spec, threshoud 50 is used for
     * decision making
     */
    for (i = 0; i < 40; i++)
    {
        data[(byte = i/8)] <<= 1;
        data[byte        ]  |= (irq_time[i] > 50); 
    }

    pr_info("DHT22 raw data 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
            data[0], data[1], data[2], data[3], data[4] );

    raw_humidity = (data[0] << 8) + data[1];
    raw_temp     = (data[2] << 8) + data[3];
    /*
     * be aware of temperature below 0°C 
     */
#if 1
    raw_temp = (raw_temp & 0x7FFF) * ((1 == (data[2] >> 7)) ? -1 : 1);
#else
    temp *= -1;  /* for test purpose only */
#endif
   
    pr_info("humidity    = %d.%d\n", raw_humidity/10, raw_humidity%10);
    pr_info("temperature = %d.%d\n", raw_temp/10, abs(raw_temp)%10);
    
    if (data[4] == ((data[0]+data[1]+data[2]+data[3]) & 0x00FF))
    {
        humidity    = raw_humidity;
        temperature = raw_temp;
        pr_info("crc = 0x%04x correct\n", data[4] );
    }
    else
        pr_info("crc error\n");

    dht22_state = dht22_idle;

    /*
     * pull high, and wait for next trigger
     */
    gpio_direction_output(gpio, high);
}

static irqreturn_t dht22_irq_handler(int irq, void* data)
{
    int                         val = gpio_get_value(gpio);
    static const int            h_pos = 3;      /* begin humidity low */
    static const int            f_pos = 43;     /* begin finish low */
    struct timespec64           now;
    struct timespec64           diff;
#if 0
    int                         time_interval;
#endif

    getnstimeofday64(&now);
    diff = timespec64_sub(now, prev_time);
#if 0
    time_interval = (int)(diff.tv_nsec / NSEC_PER_USEC);
    pr_info("....interrupt %d, value(%d), time(%d)\n", irq_count++, val, 
             time_interval);
#endif

    /* 
     * capture falling-edge interrupt and calculating
     * previous one high signal time duration, write it down
     * for calculating individual bit is 0 or 1 later
     */
    if (0 == val)
    {
        if (low_irq_count >= h_pos && low_irq_count < f_pos)
        {
            /* 
             * to minimize IRQ CPU time,
             * only to record signal HIGH time duration;
             * calcalute bit 0/1 later via work queue
             * 22~30us is low (bit is 0), 68~75us is high (bit is 1)
             */
            irq_time[low_irq_count-h_pos] = (int)(diff.tv_nsec / NSEC_PER_USEC);

            /*
             * DHT22 begin to signal end of delivering data
             * calculating 40 bits' value (0 or 1)
             */
            if (low_irq_count == f_pos-1)
                queue_work(system_highpri_wq, &process);
        }
        ++low_irq_count;
    }
    prev_time = now;

    return IRQ_HANDLED;
}

/*
#define DECL_ATTR_SHOW(F)  ssize_t F ## _show (struct kobject*,\
                                               struct kobj_attribute*,\
                                               char*)
#define DECL_ATTR_STORE(F) ssize_t F ## _store(struct kobject*,\
                                               struct kobj_attribute*,\
                                               const char*,\
                                               size_t count)
*/

static DECL_ATTR_SHOW (gpio)
{
    return sprintf(buf, "%d\n", gpio);
}

static DECL_ATTR_SHOW (autoupdate)
{
    return sprintf(buf, "%d\n", autoupdate);
}

static DECL_ATTR_STORE(autoupdate)
{
    int tmp;
    int new_auto;

    sscanf(buf, "%d\n", &tmp);
    new_auto = 0 != tmp;

    if (new_auto != autoupdate)
    {
        autoupdate = new_auto;
        if (autoupdate && dht22_idle == dht22_state)
        {
            hrtimer_start(&autoupdate_timer, 
                          ktime_set(autoupdate_sec,0), 
                          HRTIMER_MODE_REL);
        }
    }

    return count;
}

static DECL_ATTR_SHOW (autoupdate_sec)
{
    return sprintf(buf, "%d\n", autoupdate_sec);
}

static DECL_ATTR_STORE(autoupdate_sec)
{
    int tmp;

    sscanf(buf, "%d\n", &tmp);

    if (tmp >= AUTOUPDATE_SEC_MIN && tmp <= AUTOUPDATE_SEC_MAX)
        autoupdate_sec = tmp;

    return count;
}

static DECL_ATTR_SHOW (humidity)
{
    return sprintf(buf, "%d.%d%%\n", humidity/10, humidity%10);
}

static DECL_ATTR_SHOW (temperature)
{
    return sprintf(buf, "%d.%d°C\n", temperature/10, abs(temperature)%10);
}

static DECL_ATTR_STORE(trigger)
{
    to_trigger_dht22();
    return count;
}


module_init(dht22_init);
module_exit(dht22_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Edward Lin");
MODULE_DESCRIPTION("A simple module for DHT22 humidity/temperature sensor");
MODULE_VERSION("0.1");


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
#define _INCLUDE_DHT22_DECL
#include "dht22.h"

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
                 "Seconds between two trigger events, default is '10' seconds");

/*
 * module's attributes
 */
static ATTR_RO(gpio);
static ATTR_RW(autoupdate);
static ATTR_RW(autoupdate_sec);
static ATTR_RO(humidity);
static ATTR_RO(temperature);
static ATTR_WO(trigger);
static ATTR_WO(debug);

static struct attribute* dht22_attrs[] = {
    &gpio_attr.attr,
    &autoupdate_attr.attr,
    &autoupdate_sec_attr.attr,
    &humidity_attr.attr,
    &temperature_attr.attr,
    &trigger_attr.attr,
    &debug_attr.attr,
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
static const int            timeout_time = 1;  /* 1 second */
static int                  low_irq_count = 0;
static int                  humidity = 0;      /* cache last humidity */
static int                  temperature = 0;   /* cache last temperature */
static struct kobject*      dht22_kobj;
static bool                 dbg_flag = false;  /* log more info if true */
/*
 * the following will be printed if dbg_flag is true
 */
static int                  dbg_fail_read  = 0;
static int                  dbg_total_read = 0;
static int                  dbg_irq_count  = 0;
#if 0
static int                  irq_count = 0;     /* for test purpose only */
#endif

/*
 * int[40] to record signal HIGH time duration, for calculating bit 0/1
 * 22~30us HIGH is 0, 68~75us HIGH is 1
 * DHT22 send out 2-byte humidity, 2-byte temperature and 1-byte parity(CRC)
 */
static int                  irq_time[40] = { 0 };
static enum { dht22_idle, dht22_working } dht22_state = dht22_idle;

/* 
 * queue work to calculate humidity/temperature/crc(parity check) 
 * make IRQ handler to return as soon as possible
 */
static DECLARE_WORK(process_work, process_results);

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
    if (ret < 0)
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
    init_dht22_timer(&autoupdate_timer, autoupdate_func, true, 2);
    /*
     * setup timeout timer, but not start yet
     * it'll start when triggering DHT22 to request data
     */
    init_dht22_timer(&timeout_timer, timeout_func, false, 0);

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
    cancel_work_sync(&process_work);
    kobject_put(dht22_kobj);
    pr_err("dht22_exp unloaded.\n");
}

static void init_dht22_timer(struct hrtimer* timer, 
                             enum hrtimer_restart (*func)(struct hrtimer*),
                             bool start_now,
                             int  wait_sec)
{
    hrtimer_init(timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    timer->function = func;
    if (start_now)
        hrtimer_start(timer, ktime_set(wait_sec,0), HRTIMER_MODE_REL);
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
    dbg_irq_count = 0;
    dht22_state   = dht22_working;
#if 0
    irq_count     = 0;
#endif

    /*
     * start timeout_timer
     * if the host receive fiewer than 86 interrupts, the timeout_func() will
     * reset state to 'dht22_idle'
     */
    hrtimer_start(&timeout_timer, ktime_set(timeout_time, 0), HRTIMER_MODE_REL);
    /*
     * trigger DHT22
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
    }
    else
    {
        /*
         * host receive fewer yhan 86 interrupts
         * no results were produced
         * reset state to 'dht22_idle' and wait for next trigger (if autoupdate)
         */
        pr_info("Failed to fetch DHT22 data\n");
        ++dbg_fail_read;
        dht22_state   = dht22_idle;
    }
    ++dbg_total_read;
    if (dbg_flag)
    {
        pr_info("total read %d, fail %d\n", dbg_total_read, dbg_fail_read);
        pr_info("last IRQ count (should be 86) %d\n", dbg_irq_count);
    }
    return HRTIMER_NORESTART;
}

static enum hrtimer_restart autoupdate_func(struct hrtimer *hrtimer)
{
    if (autoupdate)
        to_trigger_dht22();

    /*
     * let the timerr continue flying
     * only trigger DHT22 when 'autoupdate' is enabled
     */ 
    hrtimer_forward(hrtimer, ktime_get(), ktime_set(autoupdate_sec, 0));
    return HRTIMER_RESTART;
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
     * since DHT22's condition may be not as precise as spec, 
     * threshoud 50us is used for decision making
     */
    for (i = 0; i < 40; i++)
    {
        data[(byte = (i>>3))] <<= 1;
        data[byte           ]  |= (irq_time[i] > 50); 
    }

    pr_info("DHT22 raw data 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
            data[0], data[1], data[2], data[3], data[4]);

    raw_humidity = (data[0] << 8) | data[1];
    raw_temp     = (data[2] << 8) | data[3];
    /*
     * be aware of temperature below 0°C 
     */
#if 1
    if (1 == data[2] >> 7)
        raw_temp = (raw_temp & 0x7FFF) * -1;
#else
    raw_temp *= -1;  /* for test purpose only */
#endif
   
    pr_info("humidity    = %d.%d\n", raw_humidity/10, raw_humidity%10);
    pr_info("temperature = %d.%d\n", raw_temp/10, abs(raw_temp)%10);
    
    if (data[4] == ((data[0]+data[1]+data[2]+data[3]) & 0x00FF))
    {
        humidity    = raw_humidity;
        temperature = raw_temp;
        pr_info("correct crc\n");
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
    int               val = gpio_get_value(gpio);
    static const int  h_pos = 3;      /* begin humidity low */
    static const int  f_pos = 43;     /* begin finish low */
    struct timespec64 now;
    struct timespec64 diff;
#if 0
    int               time_interval;
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
             * calculating 40 bits' value (0 or 1) via queue work
             */
            if (low_irq_count == f_pos-1)
                queue_work(system_highpri_wq, &process_work);
        }
        ++low_irq_count;
    }
    ++dbg_irq_count;
    prev_time = now;

    return IRQ_HANDLED;
}

/*
 * sysfs attributes
 * the followings two declared in dht22_exp.h
 *
#define DECL_ATTR_SHOW(F)  ssize_t F ## _show (struct kobject* kobj,\
                                               struct kobj_attribute* attr,\
                                               char* buf)
#define DECL_ATTR_STORE(F) ssize_t F ## _store(struct kobject* kobj,\
                                               struct kobj_attribute* attr,\
                                               const char* buf,\
                                               size_t count)
*/

/*
 * cat gpio
 */
static DECL_ATTR_SHOW (gpio)
{
    return sprintf(buf, "%d\n", gpio);
}

/* 
 * cat autoupdate
 */
static DECL_ATTR_SHOW (autoupdate)
{
    return sprintf(buf, "%d\n", autoupdate);
}

/*
 * echo 1 > autoupdate
 */
static DECL_ATTR_STORE(autoupdate)
{
    int tmp;
    int new_auto;

    sscanf(buf, "%d\n", &tmp);
    new_auto = 0 != tmp;

    /*
     * the autoupdate timer is still flying
     * just update 'autoupdate' flag
     */
    if (new_auto != autoupdate)
        autoupdate = new_auto;

    return count;
}

/*
 * cat autoupdate_sec
 */
static DECL_ATTR_SHOW (autoupdate_sec)
{
    return sprintf(buf, "%d\n", autoupdate_sec);
}

/*
 * echo 10 > autoupdate_sec
 */
static DECL_ATTR_STORE(autoupdate_sec)
{
    int tmp;

    sscanf(buf, "%d\n", &tmp);

    if (tmp >= AUTOUPDATE_SEC_MIN && tmp <= AUTOUPDATE_SEC_MAX)
        autoupdate_sec = tmp;

    return count;
}

/*
 * cat humidity
 */
static DECL_ATTR_SHOW (humidity)
{
    return sprintf(buf, "%d.%d%%\n", humidity/10, humidity%10);
}

/*
 * cat temperature
 */
static DECL_ATTR_SHOW (temperature)
{
    return sprintf(buf, "%d.%d°C\n", temperature/10, abs(temperature)%10);
}

/*
 * echo 1 > trigger
 */
static DECL_ATTR_STORE(trigger)
{
    to_trigger_dht22();
    return count;
}

/*
 * echo 1 > debug
 */
static DECL_ATTR_STORE(debug)
{
    int tmp;

    sscanf(buf, "%d\n", &tmp);
    dbg_flag = 0 != tmp;

    return count;
}

module_init(dht22_init);
module_exit(dht22_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Edward Lin");
MODULE_DESCRIPTION("A simple module for DHT22 humidity/temperature sensor");
MODULE_VERSION("0.1");


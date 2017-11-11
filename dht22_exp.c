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

#define GPIO        4
#define HIGH        1
#define LOW         0

static void process_results(struct work_struct* work);

static irqreturn_t dht22_irq_handler(int irq, void* data); 
static enum hrtimer_restart wait_func(struct hrtimer *hrtimer);
static void trigger_dht22(void);

static int                  irq_number;
static struct hrtimer       wait_timer;
static struct timespec64    prev_time;

/* task queue for calculating humidity/temperature/crc(parity check) */
static DECLARE_WORK(process, process_results);

static int __init dht22_init(void)
{
    int     ret;
    ktime_t wait_time = ktime_set(10, 0); /* wait for 3sec, then trigger */

    pr_err("Loading dht22_irq module...\n");

    /*
     * setup GPIO
     */
    if (!gpio_is_valid(GPIO))
    {
        pr_err("dht22_irq can't validate GPIO 4; dht22_irq unloaded\n");
        return -EINVAL;
    }
    ret = gpio_request(GPIO, "sysfs");
    if (ret < 0)
    {
        pr_err("dht22_irq failed to request GPIO 4, dht22_irq unloaded\n");
        return ret;
    }

    gpio_export(GPIO, true);
    gpio_direction_output(GPIO, HIGH);

    /*
     * setup interrupt handler
     */
    irq_number = gpio_to_irq(GPIO);
    if (irq_number < 0)
    {
        pr_err("dht22_irq failed to get IRQ number for GPIO 4, unloaded\n");
        gpio_unexport(GPIO);
        gpio_free(GPIO);
        return irq_number;
    }
    pr_err("dht22_irq assign IRQ %d to GPIO GPIO.\n", irq_number);
    ret = request_irq(irq_number,
            dht22_irq_handler,
            IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
            "dht22_irq_handler",
            NULL);
    if (ret < 0 )
    {
        pr_err("idht22_irq failed to request IRQ, unloaded.\n");
        gpio_unexport(GPIO);
        gpio_free(GPIO);
        return ret;
    }

    pr_err("dht22_irq loaded.\n");

    /*
     * time
     */
    hrtimer_init(&wait_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    wait_timer.function = wait_func;
    hrtimer_start(&wait_timer, wait_time, HRTIMER_MODE_REL);

    return 0;
}

static void __exit dht22_exit(void)
{
    free_irq(irq_number, NULL);
    gpio_unexport(GPIO);
    gpio_free(GPIO);
    hrtimer_cancel(&wait_timer);
    pr_err("dht22_irq unloaded.\n");
}

static void trigger_dht22(void)
{
    /*
     * pull down bus at least 1ms
     * to signal DHT22 for preparing humidity/temperature data
     */
    gpio_direction_output(GPIO, LOW);
    udelay(1000);

    /*
     * release bus (bus return to HIGH, due to pull-up resistor)
     * switch GPIO to input mode to receive falling-edge interrupt
     * let the interrupt handler to process the followings
     */
    gpio_direction_input(GPIO);
}

static int low_count = 0;
static int irq_count = 0;

static enum hrtimer_restart wait_func(struct hrtimer *hrtimer)
{
    /*
     * reset statictics counter
     */
    low_count = 0;
    irq_count = 0;

    /*
     * trigger DHT22, then wait for 10 more seconds to re-trigger again
     */
    getnstimeofday64(&prev_time);
    trigger_dht22();
    hrtimer_forward(hrtimer, ktime_get(), ktime_set(10,0)); 
    return  HRTIMER_RESTART;
}

/*
 * to record signal HIGH time duration, for calculating bit 0/1
 */
static int irq_time[40] = { 0 };

static void process_results(struct work_struct* work)
{
    int data[5] = { 0 };
    int i;
    int humidity;
    int temp;
    int byte;

    for (i = 0; i < 40; i++)
    {
        data[(byte = i/8)] <<= 1;
        data[byte] |= (irq_time[i] > 50); /* 22~30us is low, 68~75us is high */
    }

    humidity = (data[0] << 8) + data[1];
    temp     = (data[2] << 8) + data[3];
    /*
     * be aware of temperature below 0°C 
     */
#if 1
    temp     = (temp & 0x7FFF) * ((1 == (data[2] >> 7)) ? -1 : 1);
#else
    temp    *= -1;  /* for test purpose only */
#endif
   
    pr_err("humidity    = %d.%d\n", humidity/10, humidity%10);
    pr_err("temperature = %d.%d\n", temp/10, abs(temp)%10);
    
    if (data[4] == ((data[0]+data[1]+data[2]+data[3]) & 0x00FF))
        pr_err("crc = 0x%04x correct\n", data[4] );
    else
        pr_err("crc error\n");

    /*
     * pull high, and wait for next trigger
     */
    gpio_direction_output(GPIO, HIGH);
}

static irqreturn_t dht22_irq_handler(int irq, void* data)
{
    int                         val = gpio_get_value(GPIO);
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
    pr_err("....interrupt %d, value(%d), time(%d)\n", irq_count++, val, 
            time_interval);
#endif

    if (0 == val)
    {
        if (low_count >= h_pos && low_count < f_pos)
        {
            /* 
             * to minimize IRQ CPU time,
             * only to record signal HIGH time duration;
             * calcalute bit 0/1 later via work queue
             * 22~30us is low (bit is 0), 68~75us is high (bit is 1)
             */
#if 1
            irq_time[low_count-h_pos] = (int)(diff.tv_nsec / NSEC_PER_USEC);
#else
            irq_time[low_count-h_pos] = time_interval;
#endif
            if (low_count == f_pos-1)
                queue_work(system_highpri_wq, &process);
        }
        ++low_count;
    }
    prev_time = now;

    return IRQ_HANDLED;
}

module_init(dht22_init);
module_exit(dht22_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Edward Lin");
MODULE_DESCRIPTION("A simple module for DHT22 humidity/temperature sensor");
MODULE_VERSION("0.1");

